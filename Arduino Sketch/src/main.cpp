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
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_bt.h>

// -----------------------------------------------------------------------------
// Configuration helpers
// -----------------------------------------------------------------------------

// If not defined in config.h, we use a default display timeout of 15 seconds.
// 0 = always on, >0 = seconds until the display is turned off.
#ifndef DISPLAY_TIMEOUT_SECONDS
#define DISPLAY_TIMEOUT_SECONDS 15
#endif

// Path for the persistent inbox file in LittleFS
const char* INBOX_FILE_PATH = "/inbox.log";

// -----------------------------------------------------------------------------
// Firmware version
// -----------------------------------------------------------------------------
const char* FW_VERSION = "v0.1d";

// -----------------------------------------------------------------------------
// Radio & pager instances
// -----------------------------------------------------------------------------
SX1278 radio = new Module(LORA_SS, LORA_DIO0, LORA_RST, LORA_DIO1);  // Radio module instance
PagerClient pager(&radio);                                           // Pager client instance

// -----------------------------------------------------------------------------
// Display setup
// -----------------------------------------------------------------------------
#define SCREEN_ADDRESS 0x3C  // 0x3D for 128x64, 0x3C for 128x32 (SSD1306 address)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

// Layout constants
const int STATUS_BAR_HEIGHT = 10;
const int SCREEN_W          = 128;
const int SCREEN_H          = 64;

// Display power-save
bool         displayIsOn               = true;
unsigned long displayLastActiveMillis  = 0;
int          displayTimeoutSeconds     = DISPLAY_TIMEOUT_SECONDS;

// Inbox state (0-based)
int inboxCurrent = 0;  // currently selected/visible inbox message
int inboxTotal   = 0;  // total number of messages in inbox (logical count)

// Persistent storage status
bool storageOk = false;

// -----------------------------------------------------------------------------
// Time structures and helpers
// -----------------------------------------------------------------------------
struct PagerTime {
  int  year;
  int  month;
  int  day;
  int  hour;
  int  minute;
  int  second;
  bool valid;
};

PagerTime     pagerTime            = {0, 0, 0, 0, 0, 0, false};
unsigned long lastTimeUpdateMillis = 0;

// Time offset in minutes relative to UTC
// Example for Europe/Berlin: winter = 60, summer = 120
int timeOffsetMinutes = 60;

// -----------------------------------------------------------------------------
// Inbox structures
// -----------------------------------------------------------------------------
const int INBOX_SIZE = 64;

struct PageMessage {
  uint32_t  addr;
  String    ricName;
  String    text;
  PagerTime time;
  bool      valid;
};

PageMessage inbox[INBOX_SIZE];
int         inboxCount      = 0;  // number of valid entries
int         inboxWriteIndex = 0;  // next write position (ring buffer)

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
void onUpPressed();
void onDownPressed();
void onEnterPressed();
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

// Add minutes to pagerTime and handle day/month/year overflow
void addMinutesToPagerTime(int deltaMin) {
  if (!pagerTime.valid || deltaMin == 0) {
    return;
  }

  // Convert everything to minutes
  long totalMin = pagerTime.hour * 60L + pagerTime.minute + deltaMin;

  // Extract day offset and hour/minute
  int dayOffset = 0;
  while (totalMin < 0) {
    totalMin += 24L * 60L;
    dayOffset--;
  }
  while (totalMin >= 24L * 60L) {
    totalMin -= 24L * 60L;
    dayOffset++;
  }

  pagerTime.hour   = totalMin / 60;
  pagerTime.minute = totalMin % 60;

  // Adjust date (very simple month logic, no leap years)
  if (dayOffset != 0) {
    pagerTime.day += dayOffset;

    while (true) {
      int daysInMonth = 31;
      switch (pagerTime.month) {
        case 4:
        case 6:
        case 9:
        case 11:
          daysInMonth = 30;
          break;
        case 2:
          daysInMonth = 28;  // we ignore leap years here
          break;
        default:
          daysInMonth = 31;
          break;
      }

      if (pagerTime.day > daysInMonth) {
        pagerTime.day -= daysInMonth;
        pagerTime.month++;
        if (pagerTime.month > 12) {
          pagerTime.month = 1;
          pagerTime.year++;
        }
      } else if (pagerTime.day <= 0) {
        pagerTime.month--;
        if (pagerTime.month < 1) {
          pagerTime.month = 12;
          pagerTime.year--;
        }

        switch (pagerTime.month) {
          case 4:
          case 6:
          case 9:
          case 11:
            daysInMonth = 30;
            break;
          case 2:
            daysInMonth = 28;
            break;
          default:
            daysInMonth = 31;
            break;
        }
        pagerTime.day += daysInMonth;
      } else {
        break;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Inbox handling (RAM + LittleFS persistence)
// -----------------------------------------------------------------------------

// Reset all inbox entries in RAM
void resetInboxMemory() {
  inboxCount      = 0;
  inboxWriteIndex = 0;
  inboxTotal      = 0;
  inboxCurrent    = 0;

  for (int i = 0; i < INBOX_SIZE; ++i) {
    inbox[i].valid = false;
  }
}

// Push a message into the ring buffer without modifying the current time
// Used when restoring messages from LittleFS
void restorePushMessage(const PageMessage& msg) {
  PageMessage& dst = inbox[inboxWriteIndex];
  dst             = msg;
  dst.valid       = true;

  inboxWriteIndex = (inboxWriteIndex + 1) % INBOX_SIZE;
  if (inboxCount < INBOX_SIZE) {
    inboxCount++;
  }

  inboxTotal = inboxCount;
}

// Save all valid inbox messages to LittleFS in logical chronological order
void saveInboxToFS() {
  if (!storageOk) {
    return;
  }

  File f = LittleFS.open(INBOX_FILE_PATH, FILE_WRITE);
  if (!f) {
    Serial.println(F("[FS] Failed to open inbox file for writing"));
    return;
  }

  if (inboxCount == 0) {
    // Empty inbox → create an empty file
    f.close();
    Serial.println(F("[FS] Saved empty inbox"));
    return;
  }

  // Find the oldest valid message in the ring buffer
  int oldestIndex = -1;

  for (int i = 0; i < INBOX_SIZE; ++i) {
    int idx = (inboxWriteIndex + i) % INBOX_SIZE;
    if (inbox[idx].valid) {
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

  while (count < inboxCount) {
    PageMessage& msg = inbox[idx];
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

    idx = (idx + 1) % INBOX_SIZE;
  }

  f.close();
  Serial.print(F("[FS] Saved inbox messages to LittleFS, count="));
  Serial.println(inboxCount);
}

// Load inbox messages from LittleFS into RAM
void loadInboxFromFS() {
  if (!storageOk) {
    return;
  }

  if (!LittleFS.exists(INBOX_FILE_PATH)) {
    Serial.println(F("[FS] No inbox file found, starting with empty inbox"));
    resetInboxMemory();
    return;
  }

  File f = LittleFS.open(INBOX_FILE_PATH, FILE_READ);
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

    PageMessage msg;
    msg.addr    = (uint32_t)sAddr.toInt();
    msg.ricName = sRic;
    msg.text    = sText;
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
    if (inboxCount >= INBOX_SIZE) {
      break;
    }
  }

  f.close();

  // Set inboxCurrent to the newest message (last one we pushed)
  inboxTotal = inboxCount;
  if (inboxCount > 0) {
    int newest = (inboxWriteIndex - 1 + INBOX_SIZE) % INBOX_SIZE;
    inboxCurrent = newest;
  }

  Serial.print(F("[FS] Restored "));
  Serial.print(inboxCount);
  Serial.println(F(" messages from LittleFS"));
}

// Initialize LittleFS storage and load inbox
void storageInit() {
  Serial.print(F("[FS] Initializing LittleFS... "));
  if (!LittleFS.begin()) {
    Serial.println(F("failed, trying to format..."));

    // Try to format the LittleFS partition
    if (!LittleFS.begin(true)) {
      Serial.println(F("[FS] Formatting LittleFS failed, disabling storage"));
      storageOk = false;
      return;
    } else {
      Serial.println(F("[FS] LittleFS formatted successfully"));
    }
  } else {
    Serial.println(F("success"));
  }

  storageOk = true;

  // Optional: if we want to start with a clean inbox after formatting,
  // we can check if the file exists and, if not, create an empty one.
  loadInboxFromFS();
}


// Store a message in the ring buffer inbox[] and persist it
void storeMessage(uint32_t addr, const String &ricName, const String &text) {
  PageMessage &msg = inbox[inboxWriteIndex];
  msg.addr         = addr;
  msg.ricName      = ricName;
  msg.text         = text;
  msg.valid        = true;

  if (pagerTime.valid) {
    msg.time = pagerTime;
  } else {
    msg.time.year   = 0;
    msg.time.month  = 0;
    msg.time.day    = 0;
    msg.time.hour   = 0;
    msg.time.minute = 0;
    msg.time.second = 0;
    msg.time.valid  = false;
  }

  int storedIndex = inboxWriteIndex;

  // Advance write index (ring buffer)
  inboxWriteIndex = (inboxWriteIndex + 1) % INBOX_SIZE;

  if (inboxCount < INBOX_SIZE) {
    inboxCount++;
  }

  // Update inbox status
  inboxTotal   = inboxCount;
  inboxCurrent = storedIndex;  // newest message becomes the current one

  Serial.print(F("[Inbox] Stored message #"));
  Serial.print(storedIndex);
  Serial.print(F(" (total="));
  Serial.print(inboxCount);
  Serial.println(F(")"));

  // Persist the entire inbox to LittleFS
  saveInboxToFS();

  // Set reminder flag: we have at least one new/unacknowledged message
  newMessagePending        = true;
  lastReminderBlinkMillis  = millis();
}

// Debug helper: dump complete inbox to serial
void dumpInboxToSerial() {
  Serial.println(F("====== INBOX DUMP ======"));
  for (int i = 0; i < INBOX_SIZE; i++) {
    if (!inbox[i].valid) {
      continue;
    }
    Serial.print('#');
    Serial.print(i);
    Serial.print(F(" RIC="));
    Serial.print(inbox[i].addr);
    Serial.print(F(" ("));
    Serial.print(inbox[i].ricName);
    Serial.print(F(") "));
    if (inbox[i].time.valid) {
      Serial.print('[');
      Serial.print(inbox[i].time.day);
      Serial.print('.');
      Serial.print(inbox[i].time.month);
      Serial.print('.');
      Serial.print(inbox[i].time.year % 100);
      Serial.print(' ');
      Serial.print(inbox[i].time.hour);
      Serial.print(':');
      Serial.print(inbox[i].time.minute);
      Serial.print(']');
    } else {
      Serial.print("[no time]");
    }
    Serial.print(F(" -> "));
    Serial.println(inbox[i].text);
  }
  Serial.println(F("========================"));
}

// -----------------------------------------------------------------------------
// Time message parsing (DAPNET time RICs)
// -----------------------------------------------------------------------------

// Parse time from DAPNET string (RIC 216/224, format "YYYYMMDDHHMMSS251203200600")
void handleTimeMessage(uint32_t addr, const String &str) {
  // We currently evaluate only RIC 216 and 224 with the pattern "YYYYMMDDHHMMSS"
  if (addr == 216 || addr == 224) {
    int idx = str.indexOf("YYYYMMDDHHMMSS");
    if (idx >= 0 && str.length() >= idx + 14 + 12) {
      String d = str.substring(idx + 14, idx + 14 + 12);
      int yy   = d.substring(0, 2).toInt();
      int mm   = d.substring(2, 4).toInt();
      int dd   = d.substring(4, 6).toInt();
      int hh   = d.substring(6, 8).toInt();
      int mi   = d.substring(8, 10).toInt();
      int ss   = d.substring(10, 12).toInt();

      pagerTime.year   = 2000 + yy;
      pagerTime.month  = mm;
      pagerTime.day    = dd;
      pagerTime.hour   = hh;
      pagerTime.minute = mi;
      pagerTime.second = ss;
      pagerTime.valid  = true;

      // Convert from UTC to local time
      addMinutesToPagerTime(timeOffsetMinutes);

      lastTimeUpdateMillis = millis();

      Serial.print(F("[Time] Set (local) from addr "));
      Serial.print(addr);
      Serial.print(F(": "));
      Serial.print(pagerTime.day);
      Serial.print('.');
      Serial.print(pagerTime.month);
      Serial.print('.');
      Serial.print(pagerTime.year);
      Serial.print(' ');
      Serial.print(pagerTime.hour);
      Serial.print(':');
      Serial.println(pagerTime.minute);
    } else {
      Serial.println(F("[Time] Time pattern found but string too short"));
    }
  }

  // Optional later:
  // - addr == 208 / 2000 (XTIME / #ZEIT)
  // - addr == 2504 (HHMMSS   DDMMYY)
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
      // Advance date (simple month logic, no leap years)
      pagerTime.day++;
      int daysInMonth = 31;
      switch (pagerTime.month) {
        case 4:
        case 6:
        case 9:
        case 11:
          daysInMonth = 30;
          break;
        case 2:
          daysInMonth = 28;  // no leap year handling here
          break;
        default:
          daysInMonth = 31;
          break;
      }
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
  if (inboxCount > 0) {
    int logicalPos = 0;
    int seen       = 0;

    for (int i = 0; i < INBOX_SIZE; ++i) {
      if (!inbox[i].valid) {
        continue;
      }
      ++seen;
      if (i == inboxCurrent) {
        logicalPos = seen;
        break;
      }
    }

    char inboxBuf[12];
    snprintf(inboxBuf, sizeof(inboxBuf), "%d/%d", logicalPos, inboxCount);

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

// Startup screen: simple DAPNET logo (left) + "DAPNET" text + version below
void drawStartupScreen() {
  display.clearDisplay();

  // Simple icon on the left, moved a bit to the left
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

  display.display();
}

void displayInit() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      // Halt
    }
  }

  display.clearDisplay();
  drawStartupScreen();
  display.display();

  displayIsOn             = true;
  displayLastActiveMillis = millis();
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

struct ButtonState {
  uint8_t       pin;
  bool          lastStableState;  // HIGH = not pressed (pull-up)
  unsigned long lastChange;
};

const unsigned long DEBOUNCE_MS = 30;

ButtonState btnUp    = { BTN_UP,    HIGH, 0 };
ButtonState btnEnter = { BTN_ENTER, HIGH, 0 };
ButtonState btnDown  = { BTN_DOWN,  HIGH, 0 };

void processButton(ButtonState &btn, void (*onPress)()) {
  bool          raw = digitalRead(btn.pin);
  unsigned long now = millis();

  if (raw != btn.lastStableState && (now - btn.lastChange) > DEBOUNCE_MS) {
    btn.lastChange      = now;
    btn.lastStableState = raw;

    // FALLING edge: HIGH -> LOW => button pressed
    if (raw == LOW) {
      onPress();
    }
  }
}

void handleButtons() {
  processButton(btnUp,    onUpPressed);
  processButton(btnEnter, onEnterPressed);
  processButton(btnDown,  onDownPressed);
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
  markDisplayActivity();

  if (!displayIsOn) {
    return;
  }

  display.clearDisplay();
  drawClockBar();
  clearContentArea();

  display.setTextColor(WHITE);
  display.setTextSize(1);

  int y = STATUS_BAR_HEIGHT + 2;

  if (inboxCount == 0) {
    display.setCursor(0, y);
    display.print(F("Inbox empty"));
    display.display();
    return;
  }

  // Make sure inboxCurrent points to a valid message
  if (inboxCurrent < 0 || inboxCurrent >= INBOX_SIZE || !inbox[inboxCurrent].valid) {
    for (int i = INBOX_SIZE - 1; i >= 0; --i) {
      if (inbox[i].valid) {
        inboxCurrent = i;
        break;
      }
    }
  }

  PageMessage &msg = inbox[inboxCurrent];

  // Small header: index + RIC name
  display.setCursor(0, y);
  display.print(F("#"));
  display.print(inboxCurrent);
  display.print(F(" "));
  display.print(msg.ricName);
  y += 10;

  // Time, if present
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

  // Message text (same wrapping as in drawMessageScreen)
  const int maxCharsPerLine = 21;
  int       len             = msg.text.length();
  int       pos             = 0;

  while (pos < len && y <= SCREEN_H - 8) {
    int    remaining = len - pos;
    int    lineLen   = (remaining > maxCharsPerLine) ? maxCharsPerLine : remaining;
    String line      = msg.text.substring(pos, pos + lineLen);

    display.setCursor(0, y);
    display.print(line);

    y += 8;
    pos += lineLen;
  }

  display.display();
}

// Show next newer message in the ring buffer
void inboxShowNext() {
  if (inboxCount == 0) {
    return;
  }

  int idx = inboxCurrent;

  for (int i = 0; i < INBOX_SIZE; ++i) {
    idx = (idx + 1) % INBOX_SIZE;
    if (inbox[idx].valid) {
      inboxCurrent = idx;
      displayInbox();
      return;
    }
  }

  // If nothing else was found, keep the current one
  displayInbox();
}

// Show older message in the ring buffer
void inboxShowPrev() {
  if (inboxCount == 0) {
    return;
  }

  int idx = inboxCurrent;

  for (int i = 0; i < INBOX_SIZE; ++i) {
    idx = (idx - 1 + INBOX_SIZE) % INBOX_SIZE;
    if (inbox[idx].valid) {
      inboxCurrent = idx;
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
  notifyState.ringToneChoice = ringToneChoice;
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
    if (now >= reminderPulseEndMillis) {
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

void onUpPressed() {
  // Any key press acknowledges pending messages
  newMessagePending = false;

  // One message "up" (older message)
  markDisplayActivity();
  inboxShowPrev();
}

void onDownPressed() {
  newMessagePending = false;

  // One message "down" (newer message)
  markDisplayActivity();
  inboxShowNext();
}

void onEnterPressed() {
  newMessagePending = false;

  // ENTER always shows the inbox (from any screen)
  markDisplayActivity();
  displayInbox();
}

// -----------------------------------------------------------------------------
// Setup & main loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

#if defined(ESP32)
  // Reduce CPU frequency to save power (80 MHz is plenty for this use case)
  setCpuFrequencyMhz(80);

  // Disable WiFi and Bluetooth to save power
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  btStop();
  esp_bt_controller_disable();
#endif

  displayInit();
  buttonsInit();
  storageInit();   // Initialize LittleFS and restore inbox
  pocsagInit();
  pocsagStartRx();
}

void loop() {
  // Advance internal pager clock
  tickPagerClock();

  // Poll buttons
  handleButtons();

  // Handle display power-save
  handleDisplayPowerSave();

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

  // Wait for at least 2 POCSAG batches to fit short/medium messages
  if (pager.available() >= 2) {
    Serial.print(F("[Pager] Received pager data, decoding ... "));

    String   str;
    uint32_t addr  = 0;
    int      state = pager.readData(str, 0, &addr);

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println(F("success!"));

      Serial.print(F("[Pager] Address:\t"));
      Serial.print(String(addr));
      Serial.print(F(" [Pager] Data:\t"));
      Serial.println(str);

      // Evaluate time messages
      handleTimeMessage(addr, str);

      // Check RIC list
      for (int i = 0; i < RICNUMBER; i++) {
        if (addr == ric[i].ricvalue) {
          // Store in inbox (RAM + LittleFS)
          storeMessage(addr, ric[i].name, str);

          // Show on display and start notification
          displayPage(ric[i].name, str);
          ringBuzzer(ric[i].ringtype);
        }
      }
    } else {
      Serial.print(F("failed, code "));
      Serial.println(state);
    }
  }

  // For debugging we can call:
  // dumpInboxToSerial();
}
