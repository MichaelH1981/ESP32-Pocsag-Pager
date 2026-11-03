#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "pager_power_policy.h"
#include "pager_rssi.h"

// Mode 1 observes the hardware detector with continuous bit capture running.
// Mode 2 additionally sleeps the ESP32; the SX1278 stays in continuous RX.
// The hardware signal is a preamble candidate, not a validated POCSAG call.
class PagerPower {
 public:
  PagerPower(SX1278& radio, void (*suspendCapture)(), void (*startCapture)(), int (*queued)())
      : radio_(radio), suspendCapture_(suspendCapture), startCapture_(startCapture), queued_(queued) {}
  void begin(int mode) {
    mode_ = mode;
    policy_.begin(millis());
    if (!mode_) return;
    auto* mod = radio_.getMod();
    const int16_t detector = mod->SPIsetRegValue(RADIOLIB_SX127X_REG_PREAMBLE_DETECT,
        RADIOLIB_SX127X_PREAMBLE_DETECTOR_ON | RADIOLIB_SX127X_PREAMBLE_DETECTOR_3_BYTE |
        RADIOLIB_SX127X_PREAMBLE_DETECTOR_TOL);
    const int16_t mapping = radio_.setDIOMapping(0, RADIOLIB_SX127X_DIO0_CONT_RSSI_PREAMBLE_DETECT);
    const int16_t select = radio_.setDIOPreambleDetect(true);
    if (detector || mapping || select) { fail("detector setup"); return; }
    // Clear only the W1C preamble flag, without disturbing other IRQ flags.
    clearPreamble();
    if (mode_ == 2) {
      const int pins[] = {LORA_DIO0, BTN_UP, BTN_ENTER, BTN_DOWN};
      for (int pin : pins) {
        if (gpio_wakeup_enable(static_cast<gpio_num_t>(pin),
            pin == LORA_DIO0 ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL) != ESP_OK) {
          fail("GPIO wake setup"); return;
        }
      }
      if (esp_sleep_enable_gpio_wakeup() != ESP_OK ||
          esp_sleep_enable_timer_wakeup(250000) != ESP_OK) {
        fail("sleep setup"); return;
      }
    }
    Serial.printf("[Power] mode=%d DIO0=%d preamble=0x%02x mapping=0x%02x/0x%02x\n",
        mode_, LORA_DIO0, mod->SPIreadRegister(RADIOLIB_SX127X_REG_PREAMBLE_DETECT),
        mod->SPIreadRegister(RADIOLIB_SX127X_REG_DIO_MAPPING_1),
        mod->SPIreadRegister(RADIOLIB_SX127X_REG_DIO_MAPPING_2));
  }

  void received() {
    policy_.activity(millis());
    rssi_.received(millis());
    if (rssi_.valid()) Serial.printf("[RSSI] Last checked reception peak: %.1f dBm\n", rssi_.last());
    if (mode_) {
      ++messages_;
      Serial.printf("[Power] received preamble-age-ms=%ld capturing=%d\n",
                    preambles_ ? (long)uint32_t(millis() - lastPreamble_) : -1L, capturing_);
    }
  }

  void activity() { policy_.activity(millis()); }
  const PagerRssi& rssi() const { return rssi_; }

  void poll() {
    const uint32_t now = millis();
    if (uint32_t(now - sampledAt_) >= 20) {
      sampledAt_ = now;
      // RSSI only prevents sleeping during an ongoing carrier. It is NOT the
      // wake threshold, so it does not gate reception of weak preambles.
      // skipReceive MUST be true: the default getRSSI() calls startReceive()
      // in FSK and silently switches out of our continuous direct mode.
      const float rssi = radio_.getRSSI(true);
      currentRssi_ = rssi;
      rssi_.sample(rssi, now);
      if (rssi < minRssi_) minRssi_ = rssi;
      if (rssi > maxRssi_) maxRssi_ = rssi;
      if (mode_ && rssi >= PAGER_SLEEP_BUSY_RSSI_DBM) policy_.signal(now);
      if (mode_ && digitalRead(LORA_DIO0)) {
        ++preambles_;
        lastPreamble_ = now;
        policy_.activity(now);
        resumeCapture();
        clearPreamble();
      }
    }
    if (!mode_) return;
    if (uint32_t(now - reportedAt_) >= 60000) {
      reportedAt_ = now;
      Serial.printf("[Power] mode=%d preambles=%lu messages=%lu RF-wakes=%lu sleep-ms=%llu heap=%u RSSI=%.1f range=%.1f..%.1f queued=%d\n",
          mode_, (unsigned long)preambles_, (unsigned long)messages_,
          (unsigned long)rfWakes_, (unsigned long long)(sleptUs_ / 1000), ESP.getFreeHeap(),
          currentRssi_, minRssi_, maxRssi_, queued_());
      minRssi_ = 0;
      maxRssi_ = -150;
    }
  }

  void idle(bool uiIdle, bool completeBatchPending) {
    if (mode_ != 2) return;
    const uint32_t now = millis();
    const bool buttonsReleased = digitalRead(BTN_UP) && digitalRead(BTN_ENTER) && digitalRead(BTN_DOWN);
    if (!policy_.maySleep(now, uiIdle && buttonsReleased, completeBatchPending)) {
      resumeCapture();
      return;
    }
    // Check the candidate before suspending the bit ISR. A level-latched DIO0
    // also covers a preamble that arrives while entering Light-sleep.
    if (digitalRead(LORA_DIO0)) { policy_.activity(now); resumeCapture(); return; }
    if (capturing_) {
      suspendCapture_();
      capturing_ = false;
    }
    Serial.flush();
    const int64_t start = esp_timer_get_time();
    const esp_err_t error = esp_light_sleep_start();
    sleptUs_ += esp_timer_get_time() - start;
    // Restore bit capture BEFORE serial logging, UI or ADC work.
    if (digitalRead(LORA_DIO0) || !digitalRead(BTN_UP) ||
        !digitalRead(BTN_ENTER) || !digitalRead(BTN_DOWN)) {
      resumeCapture();
      policy_.activity(millis());
      if (digitalRead(LORA_DIO0)) ++rfWakes_;
    }
    if (error == ESP_ERR_SLEEP_REJECT) {
      // A level may assert during sleep entry. Treat this as activity, not a
      // broken power configuration, and immediately continue bit capture.
      resumeCapture();
      policy_.activity(millis());
    } else if (error != ESP_OK) {
      fail("Light-sleep entry");
    }
  }

 private:
  SX1278& radio_;
  void (*suspendCapture_)();
  void (*startCapture_)();
  int (*queued_)();
  PagerPowerPolicy policy_;
  PagerRssi rssi_;
  int mode_ = 0;
  bool capturing_ = true;
  uint32_t sampledAt_ = 0, reportedAt_ = 0, lastPreamble_ = 0;
  uint32_t preambles_ = 0, messages_ = 0, rfWakes_ = 0;
  uint64_t sleptUs_ = 0;
  float currentRssi_ = -150, minRssi_ = 0, maxRssi_ = -150;
  void clearPreamble() {
    radio_.getMod()->SPIwriteRegister(RADIOLIB_SX127X_REG_IRQ_FLAGS_1,
                                     RADIOLIB_SX127X_FLAG_PREAMBLE_DETECT);
  }
  void resumeCapture() {
    if (capturing_) return;
    startCapture_();
    capturing_ = true;
  }
  void fail(const char* reason) {
    resumeCapture();
    const int pins[] = {LORA_DIO0, BTN_UP, BTN_ENTER, BTN_DOWN};
    for (int pin : pins) gpio_wakeup_disable(static_cast<gpio_num_t>(pin));
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    mode_ = 0;
    Serial.printf("[Power] %s failed; continuing with continuous CPU reception\n", reason);
  }
};
