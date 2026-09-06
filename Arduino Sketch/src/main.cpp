/*
ESP32 Pager Proof Of Concept
This code implements a basic pager, initially designed for DAPNET use, but it can be modified to suit other needs.

Additional files:
 - config.h contains the user configuration (frequency, offset, RIC, ringtones, etc)
 - periph.h contains pin assignment

Frequency offset must be configured for reliable decoding. At present time, there is no "cal" mode available, but it is planned.
*/

#include <Arduino.h>
#include "periph.h"
#include "config.h"
#include "pager_text.h"
#include "button_debounce.h"
#include "pager_time.h"
#include "pager_ui.h"
#include "pager_mailbox.h"
#include "battery_filter.h"
#include <esp_system.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_bt.h>

static_assert(BTN_UP != OLED_SDA && BTN_UP != OLED_SCL &&
              BTN_ENTER != OLED_SDA && BTN_ENTER != OLED_SCL &&
              BTN_DOWN != OLED_SDA && BTN_DOWN != OLED_SCL,
              "Buttons must not share OLED I2C pins");
static_assert(BATTERY_ADC_PIN < 0 ||
              (BATTERY_ADC_PIN >= 32 && BATTERY_ADC_PIN <= 39 &&
               BATTERY_ADC_PIN != LORA_DIO0 && BATTERY_ADC_PIN != LORA_DIO1 &&
               BATTERY_ADC_PIN != LORA_DIO2 && BATTERY_ADC_PIN != BTN_UP &&
               BATTERY_ADC_PIN != BTN_ENTER && BATTERY_ADC_PIN != BTN_DOWN &&
               BATTERY_ADC_PIN != OLED_SDA && BATTERY_ADC_PIN != OLED_SCL),
              "Battery ADC must use a free ADC1 pin, not a radio/button/display pin");

// -----------------------------------------------------------------------------
// Configuration helpers
// -----------------------------------------------------------------------------

// If not defined in config.h, we use a default display timeout of 15 seconds.
// 0 = always on, >0 = seconds until the display is turned off.
#ifndef DISPLAY_TIMEOUT_SECONDS
#define DISPLAY_TIMEOUT_SECONDS 15
#endif

// Path for the persistent inbox file in LittleFS


// -----------------------------------------------------------------------------
// Firmware version
// -----------------------------------------------------------------------------
const char* FW_VERSION = "v0.4.1";

// -----------------------------------------------------------------------------
// Optional battery measurement on a verified free ADC1 pin
// -----------------------------------------------------------------------------
#if defined(ESP32)
const int   PIN_BATTERY_ADC   = BATTERY_ADC_PIN;    // ADC pin for battery voltage


// Voltage divider ratio: VBAT / Vadc
// Example: 100k / 100k -> factor 2.0 (4.2V -> ~2.1V at ADC).
// Adjust if your board uses a different divider.
const float BAT_VDIV_RATIO    = BATTERY_DIVIDER_RATIO;

float batteryVoltage = 0.0f;
bool batteryValid = false;
unsigned long batteryMeasuredAt = 0;
unsigned long batteryQuietSince = 0;
uint32_t batterySamples[16] = {};
size_t batterySampleCount = 0;
unsigned long lastBatterySample = 0;
bool batteryDiscarded = false;
int lastBatteryRadioBytes = 0;
#endif

// -----------------------------------------------------------------------------
// Radio & pager instances
// -----------------------------------------------------------------------------
SX1278 radio = new Module(LORA_SS, LORA_DIO0, LORA_RST, LORA_DIO1);  // Radio module instance
PagerClient pager(&radio);                                           // Pager client instance

// -----------------------------------------------------------------------------
// Display setup
// -----------------------------------------------------------------------------
#define SCREEN_ADDRESS 0x3C  // 0x3D for 128x64, 0x3C for 128x32 (SSD1306 address)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, PAGER_OLED_RESET);

// Layout constants
const int STATUS_BAR_HEIGHT = 10;
const int SCREEN_W          = 128;
const int SCREEN_H          = 64;

// Display power-save
bool         displayIsOn               = true;
unsigned long displayLastActiveMillis  = 0;
int          displayTimeoutSeconds     = DISPLAY_TIMEOUT_SECONDS;

// Inbox state (0-based)


PagerUi ui;
const char* INBOX_MENU_ITEMS[] = { "Hauptmenue", "Nachr. loeschen", "Alle loeschen", "Zurueck" };
const int INBOX_MENU_ITEM_COUNT = 4;

// Persistent storage status
bool storageOk = false;

// -----------------------------------------------------------------------------
// Time structures and helpers
// -----------------------------------------------------------------------------
PagerTime     pagerTime            = {0, 0, 0, 0, 0, 0, false};
unsigned long lastTimeUpdateMillis = 0;

// UTC conversion is configured in config.h; local RICs bypass it.

// -----------------------------------------------------------------------------
// Reading VBat
// -----------------------------------------------------------------------------

void handleBatteryMeasurement();
void displayHome();
void drawCurrentScreen();

// -----------------------------------------------------------------------------
// Inbox structures
// -----------------------------------------------------------------------------


struct PageMessage {
  uint32_t  addr;
  String    ricName;
  String    text;
  PagerTime time;
  bool      valid;
};

PageMessage personalMessages[64], weatherMessages[32], warningMessages[16];
PagerMailbox<PageMessage> mailboxes[] = {
  {personalMessages, 64}, {weatherMessages, 32}, {warningMessages, 16}
};
int activeInbox = PersonalInbox;
PagerMailbox<PageMessage>& currentInbox() { return mailboxes[activeInbox]; }
const char* inboxPath() {
  static const char* paths[] = {"/inbox.log", "/weather.log", "/warnings.log"};
  return paths[activeInbox];
}
const char* folderName(int folder) {
  static const char* names[] = {"Nachrichten", "Wetter/Pegel", "Warnmeldungen"};
  return names[folder];
}
unsigned long lastWeatherSave = 0;

// -----------------------------------------------------------------------------
// New message reminder state
// -----------------------------------------------------------------------------

// New message reminder (LED blink every 30s until acknowledged)
bool         newMessagePending          = false;
unsigned long lastReminderBlinkMillis   = 0;
const unsigned long REMINDER_INTERVAL_MS = 30000;  // 30 seconds

bool         reminderPulseActive        = false;
unsigned long reminderPulseEndMillis    = 0;
const unsigned long REMINDER_PULSE_MS   = 50;      // 50ms LED pulse

// -----------------------------------------------------------------------------
// Non-blocking notification (buzzer + LED blink) state
// -----------------------------------------------------------------------------

struct NotifyState {
  bool         active;
  unsigned long lastStepMillis;
  int          step;
  int          ringToneChoice;
};

NotifyState notifyState = { false, 0, 0, 0 };

const unsigned long NOTIFY_STEP_MS   = 100; // 100ms per step
const int           NOTIFY_LED_STEPS = 40;  // 40 steps = 4 seconds total

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void drawClockBar();
void displayInbox();
void inboxShowNext();
void inboxShowPrev();
void handleButtons();
void handleDisplayPowerSave();
void handleNewMessageReminder();
void handleNotify();
void saveInboxToFS();
void loadInboxFromFS();
void resetInboxMemory();
void restorePushMessage(const PageMessage& msg);
void storageInit();
void displaySetOn(bool on);
void markDisplayActivity();
void displayInboxMenu();
void deleteCurrentMessage();
void deleteAllMessages();

// -----------------------------------------------------------------------------
// Display helpers
// -----------------------------------------------------------------------------

// Clear all content below the status bar
void clearContentArea() {
  display.fillRect(0, STATUS_BAR_HEIGHT, SCREEN_W, SCREEN_H - STATUS_BAR_HEIGHT, BLACK);
}

// Turn the OLED display on or off (hardware power-save)
void displaySetOn(bool on) {
  if (on == displayIsOn) {
    return;
  }

  displayIsOn = on;

  if (displayIsOn) {
    batteryQuietSince = millis();
    // Turn the OLED panel back on, keep buffer content
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.display();
  } else {
    // Turn the OLED panel off
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
}


// Mark user activity or display usage to reset the power-save timer
void markDisplayActivity() {
  displayLastActiveMillis = millis();

  // If the user interacts while the display is off, we wake it up again
  if (!displayIsOn) {
    displaySetOn(true);
  }
}

// Handle automatic display power-save based on displayTimeoutSeconds
void handleDisplayPowerSave() {
  if (displayTimeoutSeconds <= 0) {
    // 0 means "always on"
    if (!displayIsOn) {
      displaySetOn(true);
    }
    return;
  }

  // If already off, we do nothing here (buttons will wake it up via markDisplayActivity)
  if (!displayIsOn) {
    return;
  }

  unsigned long now       = millis();
  unsigned long timeoutMs = (unsigned long)displayTimeoutSeconds * 1000UL;

  if (now - displayLastActiveMillis > timeoutMs) {
    // Timeout reached → turn the display off
    displaySetOn(false);
  }
}

// -----------------------------------------------------------------------------
// Time helpers
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Inbox handling (RAM + LittleFS persistence)
// -----------------------------------------------------------------------------

// Reset all inbox entries in RAM
void resetInboxMemory() {
  currentInbox().clear();
}

// Push a message into the ring buffer without modifying the current time
// Used when restoring messages from LittleFS
void restorePushMessage(const PageMessage& msg) {
  currentInbox().push(msg);
}

// Save all valid inbox messages to LittleFS in logical chronological order
void saveInboxToFS() {
  if (!storageOk) {
    return;
  }

  File f = LittleFS.open(inboxPath(), FILE_WRITE);
  if (!f) {
    Serial.println(F("[FS] Failed to open inbox file for writing"));
    return;
  }

  if (currentInbox().count == 0) {
    // Empty inbox → create an empty file
    f.close();
    currentInbox().dirty = false;
    Serial.println(F("[FS] Saved empty inbox"));
    return;
  }

  // Find the oldest valid message in the ring buffer
  int oldestIndex = -1;

  for (int i = 0; i < currentInbox().capacity; ++i) {
    int idx = (currentInbox().writeIndex + i) % currentInbox().capacity;
    if (currentInbox().messages[idx].valid) {
      oldestIndex = idx;
      break;
    }
  }

  if (oldestIndex < 0) {
    // Should not happen, but we handle it gracefully
    f.close();
    Serial.println(F("[FS] No valid messages found while saving"));
    return;
  }

  // Write messages from oldest to newest
  int idx   = oldestIndex;
  int count = 0;

  while (count < currentInbox().count) {
    PageMessage& msg = currentInbox().messages[idx];
    if (msg.valid) {
      // Format: addr|ricName|YYYYMMDDHHMMSS|text\n
      f.print(msg.addr);
      f.print('|');
      f.print(msg.ricName);
      f.print('|');

      if (msg.time.valid) {
        char timeBuf[16];
        // YYYYMMDDHHMMSS
        snprintf(timeBuf, sizeof(timeBuf), "%04d%02d%02d%02d%02d%02d",
                 msg.time.year,
                 msg.time.month,
                 msg.time.day,
                 msg.time.hour,
                 msg.time.minute,
                 msg.time.second);
        f.print(timeBuf);
      } else {
        f.print('-');
      }
      f.print('|');

      String flatText = msg.text;
      flatText.replace('\n', ' ');
      flatText.replace('\r', ' ');
      // We could also escape '|' if needed; for now we just avoid newlines.
      f.print(flatText);
      f.print('\n');

      count++;
    }

    idx = (idx + 1) % currentInbox().capacity;
  }

  f.close();
  currentInbox().dirty = false;
  Serial.print(F("[FS] Saved inbox messages to LittleFS, count="));
  Serial.println(currentInbox().count);
}

// Load inbox messages from LittleFS into RAM
void loadInboxFromFS() {
  if (!storageOk) {
    return;
  }

  if (!LittleFS.exists(inboxPath())) {
    Serial.println(F("[FS] No inbox file found, starting with empty inbox"));
    resetInboxMemory();
    return;
  }

  File f = LittleFS.open(inboxPath(), FILE_READ);
  if (!f) {
    Serial.println(F("[FS] Failed to open inbox file for reading"));
    resetInboxMemory();
    return;
  }

  Serial.println(F("[FS] Loading inbox from LittleFS"));

  resetInboxMemory();

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }

    int p1 = line.indexOf('|');
    int p2 = (p1 >= 0) ? line.indexOf('|', p1 + 1) : -1;
    int p3 = (p2 >= 0) ? line.indexOf('|', p2 + 1) : -1;

    if (p1 < 0 || p2 < 0 || p3 < 0) {
      Serial.println(F("[FS] Malformed line in inbox file, skipping"));
      continue;
    }

    String sAddr = line.substring(0, p1);
    String sRic  = line.substring(p1 + 1, p2);
    String sTime = line.substring(p2 + 1, p3);
    String sText = line.substring(p3 + 1);

    PageMessage msg{};
    msg.addr    = (uint32_t)sAddr.toInt();
    msg.ricName = sRic;
    msg.text    = sText.substring(0, 1024);
    msg.valid   = true;

    if (sTime != "-" && sTime.length() >= 14) {
      msg.time.year   = sTime.substring(0, 4).toInt();
      msg.time.month  = sTime.substring(4, 6).toInt();
      msg.time.day    = sTime.substring(6, 8).toInt();
      msg.time.hour   = sTime.substring(8, 10).toInt();
      msg.time.minute = sTime.substring(10, 12).toInt();
      msg.time.second = sTime.substring(12, 14).toInt();
      msg.time.valid  = true;
    } else {
      msg.time.valid = false;
    }

    restorePushMessage(msg);

    // We stop if we reach the maximum inbox size
    if (currentInbox().count >= currentInbox().capacity) {
      break;
    }
  }

  f.close();

  // Set inboxCurrent to the newest message (last one we pushed)
  if (currentInbox().count > 0) {
    int newest = (currentInbox().writeIndex - 1 + currentInbox().capacity) % currentInbox().capacity;
    currentInbox().current = newest;
  }

  Serial.print(F("[FS] Restored "));
  Serial.print(currentInbox().count);
  Serial.println(F(" messages from LittleFS"));
}

// Initialize LittleFS storage and load inbox
void storageInit() {
  Serial.print(F("[FS] Initializing LittleFS... "));
  if (!LittleFS.begin()) {
    Serial.println(F("failed; inbox preserved, storage disabled"));
    storageOk = false;
    return;
  }

  Serial.println(F("success"));
  storageOk = true;

  // Preserve the existing inbox; a new filesystem must be provisioned explicitly.
  for (activeInbox = 0; activeInbox < 3; ++activeInbox) {
    loadInboxFromFS();
    currentInbox().dirty = false;
  }
  activeInbox = PersonalInbox;
}


// Store a message in the ring buffer inbox[] and persist it
void storeMessage(uint32_t addr, const String &ricName, const String &text) {
  PageMessage msg{};
  msg.addr = addr;
  msg.ricName = ricName;
  msg.text = text;
  msg.time = pagerTime;
  msg.valid = true;
  if (activeInbox == WeatherInbox && addr == 4520 && currentInbox().update(msg)) {
    // Keep the selected message stable when a station updates.
  } else {
    currentInbox().push(msg);
  }
  if (activeInbox != WeatherInbox) saveInboxToFS();
  Serial.printf("[Inbox] %s: %d/%d\n", folderName(activeInbox),
                currentInbox().count, currentInbox().capacity);
}

// Weather writes are coalesced to reduce flash wear; newest data can be lost
// on power failure during the last 30 seconds. Radio decoding always runs first.
void flushWeatherInbox() {
  if (!mailboxes[WeatherInbox].dirty || millis() - lastWeatherSave < 30000 ||
      radio.available() >= 4) return;
  const int previous = activeInbox;
  activeInbox = WeatherInbox;
  saveInboxToFS();
  activeInbox = previous;
  lastWeatherSave = millis();
}

// Debug helper: dump complete inbox to serial
void dumpInboxToSerial() {
  Serial.println(F("====== INBOX DUMP ======"));
  for (int i = 0; i < currentInbox().capacity; i++) {
    if (!currentInbox().messages[i].valid) {
      continue;
    }
    Serial.print('#');
    Serial.print(i);
    Serial.print(F(" RIC="));
    Serial.print(currentInbox().messages[i].addr);
    Serial.print(F(" ("));
    Serial.print(currentInbox().messages[i].ricName);
    Serial.print(F(") "));
    if (currentInbox().messages[i].time.valid) {
      Serial.print('[');
      Serial.print(currentInbox().messages[i].time.day);
      Serial.print('.');
      Serial.print(currentInbox().messages[i].time.month);
      Serial.print('.');
      Serial.print(currentInbox().messages[i].time.year % 100);
      Serial.print(' ');
      Serial.print(currentInbox().messages[i].time.hour);
      Serial.print(':');
      Serial.print(currentInbox().messages[i].time.minute);
      Serial.print(']');
    } else {
      Serial.print("[no time]");
    }
    Serial.print(F(" -> "));
    Serial.println(currentInbox().messages[i].text);
  }
  Serial.println(F("========================"));
}

void deleteCurrentMessage() {
  if (currentInbox().count == 0) {
    return;
  }

  if (currentInbox().current < 0 || currentInbox().current >= currentInbox().capacity || !currentInbox().messages[currentInbox().current].valid) {
    return;
  }

  int oldIdx = currentInbox().current;

  // Aktuelle Nachricht ungültig machen
  currentInbox().messages[oldIdx] = PageMessage{};

  // Inbox neu zählen und neue aktuelle Position wählen
  int newCount = 0;
  int newCurrent = -1;
  int bestDist = currentInbox().capacity + 1;

  for (int i = 0; i < currentInbox().capacity; ++i) {
    if (!currentInbox().messages[i].valid) {
      continue;
    }
    newCount++;

    // möglichst nahe an der alten Position bleiben
    int dist = abs(i - oldIdx);
    if (dist < bestDist) {
      bestDist = dist;
      newCurrent = i;
    }
  }

  currentInbox().count = newCount;

  if (currentInbox().count == 0) {
    currentInbox().current = 0;
  } else if (newCurrent >= 0) {
    currentInbox().current = newCurrent;
  }

  // Änderungen in LittleFS speichern
  saveInboxToFS();

  Serial.print(F("[Inbox] Deleted message at index "));
  Serial.print(oldIdx);
  Serial.print(F(", remaining="));
  Serial.println(currentInbox().count);
}
void deleteAllMessages() {
  Serial.println(F("[Inbox] Deleting all messages"));

  // RAM-Inbox zurücksetzen
  resetInboxMemory();

  saveInboxToFS();

}

// -----------------------------------------------------------------------------
// Time message parsing (DAPNET time RICs)
// -----------------------------------------------------------------------------

void handleTimeMessage(uint32_t addr, const String &str) {
  if (!pagerTimeRic(addr)) return;
  PagerTime parsed{};
  if (!parsePagerTime(addr, str.c_str(), TIME_UTC_OFFSET_MINUTES, TIME_EU_DST, parsed)) {
    Serial.printf("[Time] Invalid or incomplete time on RIC %lu; clock unchanged\n",
                  static_cast<unsigned long>(addr));
    return;
  }
  pagerTime = parsed;
  lastTimeUpdateMillis = millis();
  Serial.printf("[Time] RIC %lu (%s) -> local %04d-%02d-%02d %02d:%02d:%02d\n",
                static_cast<unsigned long>(addr),
                (addr == 200 || addr == 216) ? "UTC" : "local",
                pagerTime.year, pagerTime.month, pagerTime.day,
                pagerTime.hour, pagerTime.minute, pagerTime.second);
  if (displayIsOn) {
    drawClockBar();
    display.display();
  }
}

// Simple software clock based on millis()
void tickPagerClock() {
  if (!pagerTime.valid) {
    return;
  }

  unsigned long now = millis();

  // Catch up missing second ticks
  while (now - lastTimeUpdateMillis >= 1000) {
    lastTimeUpdateMillis += 1000;
    pagerTime.second++;

    if (pagerTime.second >= 60) {
      pagerTime.second = 0;
      pagerTime.minute++;
    }
    if (pagerTime.minute >= 60) {
      pagerTime.minute = 0;
      pagerTime.hour++;
    }
    if (pagerTime.hour >= 24) {
      pagerTime.hour = 0;
      pagerTime.day++;
      const int daysInMonth = pagerDaysInMonth(pagerTime.year, pagerTime.month);
      if (pagerTime.day > daysInMonth) {
        pagerTime.day = 1;
        pagerTime.month++;
        if (pagerTime.month > 12) {
          pagerTime.month = 1;
          pagerTime.year++;
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Status bar (clock + inbox info)
// -----------------------------------------------------------------------------

// Draw the top status bar with clock (left) and inbox position (right)
void drawClockBar() {
  // Clear status bar area
  display.fillRect(0, 0, SCREEN_W, STATUS_BAR_HEIGHT, BLACK);
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Left: date + time
  display.setCursor(0, 0);
  if (pagerTime.valid) {
    char timeBuf[20];
    snprintf(timeBuf, sizeof(timeBuf), "%02d.%02d.%02d %02d:%02d",
             pagerTime.day,
             pagerTime.month,
             pagerTime.year % 100,
             pagerTime.hour,
             pagerTime.minute);
    display.print(timeBuf);
  } else {
    display.print(F("No Time"));
  }

  // Right: inbox "x/n" (logical position among all valid messages)
  if (currentInbox().count > 0) {
    int logicalPos = 0;
    int seen       = 0;

    for (int i = 0; i < currentInbox().capacity; ++i) {
      if (!currentInbox().messages[i].valid) {
        continue;
      }
      ++seen;
      if (i == currentInbox().current) {
        logicalPos = seen;
        break;
      }
    }

    char inboxBuf[12];
    snprintf(inboxBuf, sizeof(inboxBuf), "%d/%d", logicalPos, currentInbox().count);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(inboxBuf, 0, 0, &x1, &y1, &w, &h);

    display.setCursor(SCREEN_W - w, 0);
    display.print(inboxBuf);
  }
}

// -----------------------------------------------------------------------------
// Radio (POCSAG) setup
// -----------------------------------------------------------------------------

void pocsagInit() {
  // Initialize SX1278 with default settings
  Serial.print(F("[SX1278] Initializing ... "));
  int state = radio.beginFSK();

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) {
      // Halt
    }
  }

  // Initialize Pager client
  Serial.print(F("[Pager] Initializing ... "));
  state = pager.begin(frequency + offset, 1200);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) {
      // Halt
    }
  }
}

void pocsagStartRx() {
  // Start receiving POCSAG messages
  Serial.print(F("[Pager] Starting to listen ... "));
  int state = pager.startReceive(LORA_DIO2, 200, 0);  // Interrupt on DIO2
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) {
      // Halt
    }
  }
}

// -----------------------------------------------------------------------------
// Display init & startup screen
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Display init
// -----------------------------------------------------------------------------
void displayInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, true, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      // Halt
    }
  }

  display.clearDisplay();
  display.display();

  displayIsOn             = true;
  displayLastActiveMillis = millis();
}

/// Startup screen: simple DAPNET logo (left) + "DAPNET" text + version + battery
void drawStartupScreen() {
  display.clearDisplay();

  // Simple icon on the left
  display.drawCircle(14, 38, 12, WHITE);
  display.drawCircle(20, 18, 6, WHITE);
  display.drawCircle(38, 26, 8, WHITE);

  display.drawLine(20, 18, 38, 26, WHITE);
  display.drawLine(20, 18, 14, 38, WHITE);
  display.drawLine(14, 38, 38, 26, WHITE);

  // "DAPNET" text on the right
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(52, 20);
  display.print("DAPNET");

  // Version string below
  display.setTextSize(1);
  display.setCursor(52, 38);
  display.print(FW_VERSION);

  // Battery voltage at the bottom
#if defined(ESP32)
  display.setCursor(0, 54);  // bottom line of 64px display
  display.print(F("Bat: "));
  if (batteryValid) {
    display.print(batteryVoltage, 2);
    display.print(F("V"));
  } else {
    display.print(F("--"));
  }
#endif

  display.display();
}



// -----------------------------------------------------------------------------
// Button handling
// -----------------------------------------------------------------------------

// We assume buttons are wired to GND and use the internal pull-up resistors.
void buttonsInit() {
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
}

KeyTracker upKey, downKey, enterKey;

void dispatchKey(UiKey key) {
  const bool wasOff = !displayIsOn;
  markDisplayActivity();
  newMessagePending = false;
  if (wasOff && key != UiKey::Back) { drawCurrentScreen(); return; } // Wake only; long ENTER may always go home.
  const UiAction action = ui.press(key);
  activeInbox = ui.folder;
  switch (action) {
    case UiAction::Previous: inboxShowPrev(); return;
    case UiAction::Next: inboxShowNext(); return;
    case UiAction::DeleteMessage: deleteCurrentMessage(); break;
    case UiAction::DeleteAll: deleteAllMessages(); break;
    default: break;
  }
  drawCurrentScreen();
}

void handleButtons() {
  const uint32_t now = millis();
  const auto up = upKey.update(digitalRead(BTN_UP) == HIGH, now);
  const auto down = downKey.update(digitalRead(BTN_DOWN) == HIGH, now);
  const auto enter = enterKey.update(digitalRead(BTN_ENTER) == HIGH, now);
  if (enter == KeyEvent::Long) dispatchKey(UiKey::Back);
  else if (enter == KeyEvent::Short) dispatchKey(UiKey::Enter);
  if (up == KeyEvent::Short || up == KeyEvent::Long) dispatchKey(UiKey::Up);
  if (down == KeyEvent::Short || down == KeyEvent::Long) dispatchKey(UiKey::Down);
}

// -----------------------------------------------------------------------------
// Screen drawing helpers
// -----------------------------------------------------------------------------

// Helper to draw a message including clock bar, header and wrapped text
void drawMessageScreen(const String &header, const String &text) {
  markDisplayActivity();

  if (!displayIsOn) {
    return;
  }

  display.clearDisplay();
  drawClockBar();
  clearContentArea();

  display.setTextColor(WHITE);

  int y = STATUS_BAR_HEIGHT + 1;

  // Header (RIC name)
  display.setTextSize(1);
  display.setCursor(0, y);
  display.print(header);
  y += 10;

  // Message text in TextSize 1 → maximum content per screen
  const int maxCharsPerLine = 21;  // ~128px / 6px per character
  int       len             = text.length();
  int       pos             = 0;

  while (pos < len && y <= SCREEN_H - 8) {
    int    remaining = len - pos;
    int    lineLen   = (remaining > maxCharsPerLine) ? maxCharsPerLine : remaining;
    String line      = text.substring(pos, pos + lineLen);

    display.setCursor(0, y);
    display.print(line);

    y += 8;  // TextSize-1 line height
    pos += lineLen;
  }

  display.display();
}

// Used when a new message is received
void displayPage(const String &address, const String &text) {
  // address = RIC name
  // We always wake the display for a new message.
  // The regular power-save timeout will turn it off again.
  displaySetOn(true);
  drawMessageScreen(address, text);
}

// Inbox view
void displayInbox() {
  // Any display activity resets the power-save timer
  markDisplayActivity();

  if (!displayIsOn) {
    return;   // If display is off, do nothing (buttons will wake it)
  }

  display.clearDisplay();
  drawClockBar();       // Draw top bar: date/time left, message index right
  clearContentArea();   // Clear area below the status bar

  display.setTextColor(WHITE);
  display.setTextSize(1);

  int y = STATUS_BAR_HEIGHT + 2;

  // ─────────────────────────────────────────────
  // If no messages are stored, show a simple text
  // ─────────────────────────────────────────────
  if (currentInbox().count == 0) {
    display.setCursor(0, y);
    display.print(folderName(activeInbox));
    display.setCursor(0, y + 12);
    display.print(F("Keine Nachrichten"));
    display.display();
    return;
  }

  // ─────────────────────────────────────────────
  // Ensure inboxCurrent points to a valid entry
  // ─────────────────────────────────────────────
  if (currentInbox().current < 0 || currentInbox().current >= currentInbox().capacity || !currentInbox().messages[currentInbox().current].valid) {
    for (int i = currentInbox().capacity - 1; i >= 0; --i) {
      if (currentInbox().messages[i].valid) {
        currentInbox().current = i;
        break;
      }
    }
  }

  PageMessage &msg = currentInbox().messages[currentInbox().current];

  // ─────────────────────────────────────────────
  // FIRST LINE under the status bar:
  // Left  : Sender (RIC name)
  // Right : Battery voltage, right-aligned (e.g. "4.05V")
  // ─────────────────────────────────────────────

#if defined(ESP32)
  // Prepare battery voltage string
  char batBuf[12];
  if (batteryValid) {
    snprintf(batBuf, sizeof(batBuf), "%.2fV", batteryVoltage);
  } else {
    snprintf(batBuf, sizeof(batBuf), "Bat: --");
  }

  // Measure text width so we can right-align it
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(batBuf, 0, 0, &bx, &by, &bw, &bh);

  // Right-aligned battery voltage
  display.setCursor(SCREEN_W - bw, y);
  display.print(batBuf);
#endif

  // Sender/ric name on the left side of the same line
  display.setCursor(0, y);
  display.print(msg.ricName);
  y += 10;

  // ─────────────────────────────────────────────
  // SECOND LINE: Timestamp (if valid)
  // ─────────────────────────────────────────────
  if (msg.time.valid) {
    char tbuf[20];
    snprintf(tbuf, sizeof(tbuf), "%02d.%02d.%02d %02d:%02d",
             msg.time.day,
             msg.time.month,
             msg.time.year % 100,
             msg.time.hour,
             msg.time.minute);

    display.setCursor(0, y);
    display.print(tbuf);
    y += 10;
  }

  // ─────────────────────────────────────────────
  // MESSAGE BODY: 21 characters per line wrapping
  // ─────────────────────────────────────────────
  const int maxCharsPerLine = 21;
  int       len             = msg.text.length();
  int       pos             = 0;

  // Draw the text line by line until we run out of screen space
  while (pos < len && y <= SCREEN_H - 8) {
    int    remaining = len - pos;
    int    lineLen   = (remaining > maxCharsPerLine) ? maxCharsPerLine : remaining;
    String line      = msg.text.substring(pos, pos + lineLen);

    display.setCursor(0, y);
    display.print(line);

    y += 8;     // 8px line height for text size 1
    pos += lineLen;
  }

  display.display();
}

// Show next newer message in the ring buffer
void inboxShowNext() {
  if (currentInbox().count == 0) {
    return;
  }

  int idx = currentInbox().current;

  for (int i = 0; i < currentInbox().capacity; ++i) {
    idx = (idx + 1) % currentInbox().capacity;
    if (currentInbox().messages[idx].valid) {
      currentInbox().current = idx;
      displayInbox();
      return;
    }
  }

  // If nothing else was found, keep the current one
  displayInbox();
}
void displayInboxMenu() {
  markDisplayActivity();

  if (!displayIsOn) {
    return;
  }

  display.clearDisplay();
  drawClockBar();
  clearContentArea();

  display.setTextColor(WHITE);
  display.setTextSize(1);

  int y = STATUS_BAR_HEIGHT + 4;

  display.setCursor(0, y);
  display.print(folderName(activeInbox));
  y += 10;

  for (int i = 0; i < INBOX_MENU_ITEM_COUNT; ++i) {
    display.setCursor(0, y);
    if (i == ui.selected) {
      display.print('>');   // Markierung für die aktuelle Auswahl
    } else {
      display.print(' ');
    }
    display.print(' ');
    display.print(INBOX_MENU_ITEMS[i]);
    y += 9;
  }

  display.display();
}

// Show older message in the ring buffer
void inboxShowPrev() {
  if (currentInbox().count == 0) {
    return;
  }

  int idx = currentInbox().current;

  for (int i = 0; i < currentInbox().capacity; ++i) {
    idx = (idx - 1 + currentInbox().capacity) % currentInbox().capacity;
    if (currentInbox().messages[idx].valid) {
      currentInbox().current = idx;
      displayInbox();
      return;
    }
  }

  // If nothing else was found, keep the current one
  displayInbox();
}

// -----------------------------------------------------------------------------
// Buzzer and LED notification (non-blocking)
// -----------------------------------------------------------------------------

void handleNotify() {
  if (!notifyState.active) {
    return;
  }

  unsigned long now = millis();
  if (now - notifyState.lastStepMillis < NOTIFY_STEP_MS) {
    return;
  }

  notifyState.lastStepMillis = now;

  // LED blink pattern: toggle every step
  if (notifyState.step < NOTIFY_LED_STEPS) {
    if ((notifyState.step % 2) == 0) {
      digitalWrite(LED, HIGH);
    } else {
      digitalWrite(LED, LOW);
    }

    // Tone pattern for the first NOTENUMBER steps
    if (notifyState.step < NOTENUMBER) {
      int note = beepTones[notifyState.ringToneChoice][notifyState.step];
      tone(BUZZER, note, 130); // 130ms non-blocking
    }

    notifyState.step++;
  } else {
    // End of notification pattern
    notifyState.active = false;
    digitalWrite(LED, LOW);
  }
}

void ringBuzzer(int ringToneChoice) {
  // Start non-blocking notification pattern
  notifyState.active         = true;
  notifyState.lastStepMillis = millis();
  notifyState.step           = 0;
  notifyState.ringToneChoice = (ringToneChoice >= 0 && ringToneChoice < RINGTONE) ? ringToneChoice : 0;
}

// -----------------------------------------------------------------------------
// New message reminder (LED pulse every 30s until acknowledged)
// -----------------------------------------------------------------------------

void handleNewMessageReminder() {
  if (!newMessagePending) {
    // No pending messages -> ensure LED is off if no notify is active
    if (!notifyState.active && !reminderPulseActive) {
      digitalWrite(LED, LOW);
    }
    return;
  }

  // While main notification is running, we do not run the reminder
  if (notifyState.active) {
    return;
  }

  unsigned long now = millis();

  if (reminderPulseActive) {
    // We are currently in a short LED pulse
    if (static_cast<int32_t>(now - reminderPulseEndMillis) >= 0) {
      digitalWrite(LED, LOW);
      reminderPulseActive = false;
    }
  } else {
    // Wait until interval elapsed, then start a new pulse
    if (now - lastReminderBlinkMillis >= REMINDER_INTERVAL_MS) {
      lastReminderBlinkMillis = now;
      digitalWrite(LED, HIGH);
      reminderPulseActive    = true;
      reminderPulseEndMillis = now + REMINDER_PULSE_MS;
    }
  }
}

// -----------------------------------------------------------------------------
// Button event handlers
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Setup & main loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[Pager] Firmware %s\n", FW_VERSION);
  Serial.printf("[Boot] Reset reason=%d, free heap=%u\n", int(esp_reset_reason()), ESP.getFreeHeap());
  Serial.flush(); // Complete UART output before changing the CPU clock.
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

#if defined(ESP32)
  // Reduce CPU frequency to save power (80 MHz is fine for this use case)
  setCpuFrequencyMhz(80);

  // Disable WiFi and Bluetooth to save power
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  btStop();
  esp_bt_controller_disable();
#endif

  Serial.println(F("[Boot] Power setup complete; initializing OLED"));
  Serial.flush();
  displayInit();
  Serial.println(F("[Boot] OLED ready"));

#if defined(ESP32)
  if (PIN_BATTERY_ADC >= 0) {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    batteryQuietSince = millis();
  } else {
    Serial.println(F("[Battery] Disabled: configure a verified free ADC pin"));
  }
#endif

  // Show startup screen with battery voltage
  drawStartupScreen();
  delay(1500);   // keep splash screen for 1.5s

  Serial.println(F("[Boot] Initializing buttons and storage"));
  buttonsInit();
  storageInit();   // Initialize LittleFS and restore inbox
  pocsagInit();
  pocsagStartRx();
  displayHome();
}

void handlePagerReceive() {
  // Wait for at least 2 POCSAG batches to fit short/medium messages
  if (pager.available() >= 2) {
    Serial.print(F("[Pager] Received pager data, decoding ... "));

    static uint8_t received[513];
    size_t receivedLength = sizeof(received) - 1;
    uint32_t addr = 0;
    int state = pager.readData(received, &receivedLength, &addr);
    received[receivedLength < sizeof(received) ? receivedLength : sizeof(received)-1] = 0;
    String str;
    if (state == RADIOLIB_ERR_NONE) {
      str = receivedLength ? reinterpret_cast<const char*>(received) : "<tone>";
    }

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println(F("success!"));

      Serial.print(F("[Pager] Address:\t"));
      Serial.print(addr);
      Serial.print(F(" [Pager] Raw:\t"));
      Serial.println(str);

      SkyperHeader skyper{false, 0, 0, 0};
#if SKYPER_DECODE
      skyper = decodeSkyper(addr, str.begin(), str.length());
      if (skyper.valid) {
        str.remove(0, skyper.headerLength);
        str.trim();
        Serial.printf("[Skyper] Rubric=%u Item=%u Text: ", skyper.rubric, skyper.item);
        for (unsigned char c : str) {
          const char* glyph = pagerGermanGlyph(c, true);
          if (glyph) Serial.print(glyph); else Serial.write(c);
        }
        Serial.println();
        String readable;
        readable.reserve(str.length() * 2);
        for (unsigned char c : str) {
          const char* glyph = pagerGermanGlyph(c, false);
          if (glyph) readable += glyph;
          else readable += static_cast<char>(c);
        }
        str = readable;
      } else if (addr == 4520 || addr == 4512) {
        Serial.println(F("[Skyper] Invalid header/payload; not stored"));
        return;
      }
#endif
      handleTimeMessage(addr, str);
      int folder = -1;
      int ringtone = 2;
      String name;
      if (addr == 4520 && skyper.valid) {
        const unsigned weatherRubrics[] = {WEATHER_SKYPER_RUBRICS};
        const unsigned warningRubrics[] = {WARNING_SKYPER_RUBRICS};
        folder = skyperFolder(skyper.rubric, weatherRubrics,
                             sizeof(weatherRubrics)/sizeof(weatherRubrics[0]),
                             warningRubrics, sizeof(warningRubrics)/sizeof(warningRubrics[0]));
        char label[16];
        snprintf(label, sizeof(label), "R%u:%u", skyper.rubric, skyper.item);
        name = label;
#if SKYPER_NEWS_INBOX
        if (folder < 0) folder = PersonalInbox;
#endif
      } else if (WARNING_RIC && addr == WARNING_RIC) {
        folder = WarningInbox;
        name = "Warnung";
      } else {
        for (size_t i = 0; i < RICNUMBER; ++i) {
          if (ric[i].ricvalue && addr == ric[i].ricvalue) {
            folder = addr == 1080 ? WeatherInbox : PersonalInbox;
            name = ric[i].name;
            ringtone = ric[i].ringtype;
            break;
          }
        }
      }
      if (folder >= 0 && str.length()) {
        const int previousFolder = activeInbox;
        activeInbox = folder;
        PageMessage candidate{};
        candidate.addr = addr;
        candidate.text = str;
        if (folder == WarningInbox && currentInbox().containsText(candidate)) {
          activeInbox = previousFolder;
          return; // Repeated broadcasts do not ring or consume additional slots.
        }
        const int previousCurrent = currentInbox().current;
        storeMessage(addr, name, str);
        if (folder == WeatherInbox) {
          // Background updates must not retarget a pending delete confirmation.
          if (folder == previousFolder) {
            currentInbox().current = previousCurrent;
            if (ui.screen == Screen::ConfirmMessage || ui.screen == Screen::ConfirmAll)
              ui.showMessage();
          }
          activeInbox = previousFolder;
          if (displayIsOn && ui.screen == Screen::Home) displayHome();
        } else {
          ui.folder = folder;
          ui.showMessage();
          newMessagePending = true;
          lastReminderBlinkMillis = millis();
          markDisplayActivity();
          displayInbox();
          if (folder == PersonalInbox) ringBuzzer(ringtone);
          else if (WARNING_AUDIBLE) ringBuzzer(WARNING_RINGTONE);
        }
      }
    } else {
      Serial.print(F("failed, code "));
      Serial.println(state);
    }
  }

}

void loop() {
  handlePagerReceive();

  // Advance internal pager clock
  tickPagerClock();

  // Poll buttons
  handleButtons();

  // Handle display power-save
  handleDisplayPowerSave();
  handleBatteryMeasurement();
  flushWeatherInbox();

  // Handle non-blocking notification pattern
  handleNotify();

  // Handle LED reminder for new/unacknowledged messages
  handleNewMessageReminder();

  // Update clock bar once per second (only if we have time and display is on)
  static unsigned long lastClockDraw = 0;
  unsigned long        now           = millis();
  if (pagerTime.valid && displayIsOn && (now - lastClockDraw > 1000)) {
    lastClockDraw = now;
    drawClockBar();
    display.display();
  }


  // For debugging we can call:
  // dumpInboxToSerial();
}


void displayHome() {
  if (!displayIsOn) return;
  display.clearDisplay();
  drawClockBar();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 13);
  display.print(F("Akku: "));
  if (batteryValid) { display.print(batteryVoltage, 2); display.print(F(" V")); }
  else display.print(F("-- V"));
  for (int folder = 0; folder < 3; ++folder) {
    display.setCursor(0, 29 + folder * 11);
    display.print(ui.folder == folder ? '>' : ' ');
    display.print(folderName(folder));
    display.print(' ');
    display.print(mailboxes[folder].count);
  }
  display.display();
}

void drawCurrentScreen() {
  if (!displayIsOn) return;
  switch (ui.screen) {
    case Screen::Home: displayHome(); break;
    case Screen::Inbox: displayInbox(); break;
    case Screen::Actions: displayInboxMenu(); break;
    default:
      display.clearDisplay();
      drawClockBar();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(0, 16);
      display.print(ui.screen == Screen::ConfirmAll ? F("Alle loeschen?") : F("Nachricht loeschen?"));
      display.setCursor(0, 32);
      display.print(ui.confirmYes ? F("  Nein     > Ja") : F("> Nein       Ja"));
      display.setCursor(0, 53);
      display.print(F("ENTER: bestaetigen"));
      display.display();
      break;
  }
}

void handleBatteryMeasurement() {
  if (PIN_BATTERY_ADC < 0) return;
  const unsigned long now = millis();
  // Never sample during a notification, raw receive activity or immediately
  // after a screen/power change. Do not stop the receiver to measure voltage.
  const int radioBytes = radio.available();
  const bool radioChanged = radioBytes != lastBatteryRadioBytes;
  lastBatteryRadioBytes = radioBytes;
  if (notifyState.active || reminderPulseActive || radioChanged ||
      now - displayLastActiveMillis < 500) {
    batteryQuietSince = now;
    batterySampleCount = 0;
    batteryDiscarded = false;
    return;
  }
  if (now - batteryQuietSince < 500) return;
  if (batteryMeasuredAt && now - batteryMeasuredAt < 30000) return;
  if (now - lastBatterySample < 5) return;
  lastBatterySample = now;
  const uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
  if (!batteryDiscarded) { batteryDiscarded = true; return; }
  batterySamples[batterySampleCount++] = mv;
  if (batterySampleCount < 16) return;
  float measured = 0;
  batteryValid = batteryVolts(batterySamples, BAT_VDIV_RATIO,
                              BATTERY_CALIBRATION_GAIN, BATTERY_CALIBRATION_OFFSET, measured);
  if (batteryValid) batteryVoltage = measured;
  batteryMeasuredAt = now;
  batterySampleCount = 0;
  batteryDiscarded = false;
  if (batteryValid) Serial.printf("[Battery] %.3f V (calibrated ADC, divider %.2f)\n", batteryVoltage, BAT_VDIV_RATIO);
  else Serial.println(F("[Battery] Invalid measurement; check wiring/calibration"));
  if (ui.screen == Screen::Home && displayIsOn) displayHome();
}
