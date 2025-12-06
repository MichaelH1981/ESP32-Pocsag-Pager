# ESP32-Pocsag-Pager
 
# Libraries
 * [RadioLib](https://github.com/jgromes/RadioLib)
 * Adafruit GFX (I plan to move tu U8G2 at some point)
 * Adafruiy SSD1306
 
 
# Hardware
Uses an ESP32 LORA32 TTGO. You must bridge DIO1 to pin 35 and DIO2 to pin 34 (broken out as LORA1 and LORA2 on the headers). 
I added a buzzer on pin 14, and plan to add buttons in the near future

# Setup
Ideally, you should calibrate your SX1278 as it probably has an offset. Right now, best way would be to use a TXCO SDR and use another RadioLib sketch example to transmit a continuous signal and determine the offset from there.
In config.h, change the RIC with yours, fiddle with the tones, and enjoy!

## ESP32 DAPNET Pager – Extended Version

This version is based on the original ESP32 Pager Proof of Concept, but significantly extends and refactors the codebase.  
The goal is to transform the simple demo pager into a fully usable handheld device with message history, persistent storage, a clock, UI navigation, and power-saving features.

### New Features and Improvements

- **Persistent Inbox (LittleFS)**
  - Received messages are stored in a ring buffer (`INBOX_SIZE`).
  - Inbox is saved to LittleFS at `/inbox.log`.
  - All messages are restored on startup.
  - Displays message index and timestamp.

- **Time Synchronization via DAPNET**
  - Supports parsing of DAPNET time RICs (e.g. 216/224).
  - Internal software clock based on `millis()`.
  - Configurable timezone offset (`timeOffsetMinutes`).
  - Clock shown in the top status bar.

- **Status Bar & Updated Display Layout**
  - Top bar shows date/time (left) and inbox position (right).
  - New startup screen with drawn DAPNET-style logo and firmware version.
  - Message view with automatic word wrapping for optimal readability.

- **Display Power-Save Mode**
  - Configurable timeout using `DISPLAY_TIMEOUT_SECONDS` (0 = always on).
  - Display automatically turns off after inactivity.
  - Wakes on button press or new message.
  - Uses correct OLED power commands (`SSD1306_DISPLAYOFF/ON`).

- **Button Input & Inbox Navigation**
  - Three debounced buttons: UP / ENTER / DOWN.
  - ENTER opens inbox from any screen.
  - UP/DOWN navigate through older/newer messages.
  - Any keypress acknowledges new-message reminders.

- **Non-Blocking Notification System**
  - LED and buzzer operate via a state machine (`handleNotify()`).
  - No blocking `delay()` calls.
  - Main loop stays responsive.

- **New Message Reminder**
  - LED pulse every 30 seconds until acknowledged.
  - Efficient and non-intrusive.

- **ESP32 Power Optimizations**
  - CPU clock reduced to 80 MHz.
  - WiFi and Bluetooth are fully disabled at startup.
  - Reduced idle power consumption.

### Compatibility

The original POCSAG functionality (RadioLib, `pager.begin()`, decoding, RIC filtering) remains intact and compatible.  
All added features integrate seamlessly with the original concept while significantly expanding functionality and user experience.

### ToDo


## ESP32 DAPNET Pager – Extended Version Deutsch

Diese Version basiert auf dem ursprünglichen ESP32-Pager-Proof-of-Concept, erweitert den Code aber deutlich um folgende Funktionen:

- **Persistente Inbox (LittleFS)**
  - Empfangene Nachrichten werden in einem Ringspeicher (`INBOX_SIZE`) gehalten.
  - Die Inbox wird zusätzlich in LittleFS unter `/inbox.log` gespeichert.
  - Beim Start werden vorhandene Nachrichten wiederhergestellt.
  - Anzeige der Nachrichten inkl. Index und Zeitstempel.

- **Zeit-Synchronisation über DAPNET**
  - Auswertung spezieller Zeit-RICs (z.B. 216/224) im DAPNET-Format.
  - Interne Software-Uhr (basierend auf `millis()`).
  - Konfigurierbarer Zeitzonen-Offset (`timeOffsetMinutes`, z.B. +60 für CET).

- **Statusleiste & neues Display-Layout**
  - Obere Statusbar mit Datum/Uhrzeit sowie Inbox-Position (`x/n`).
  - Neue Startseite mit gezeichnetem DAPNET-Logo und Firmware-Version.
  - Nachrichtenanzeige mit Wort-/Zeilenumbruch, optimiert für 128×64 OLED.

- **Display-Powersave**
  - Konfigurierbarer Timeout über `DISPLAY_TIMEOUT_SECONDS` (0 = immer an).
  - Automatisches Abschalten des OLEDs nach Inaktivität.
  - Automatisches Aufwachen bei Tastenbetätigung oder neuen Nachrichten.

- **Button-Steuerung & Inbox-Navigation**
  - Drei Tasten (UP / ENTER / DOWN) mit Debounce.
  - ENTER: zeigt jederzeit die Inbox.
  - UP/DOWN: blättern durch ältere/jüngere Nachrichten.
  - Jede Tastenbetätigung quittiert ausstehende „New Message“-Reminder.

- **Nicht-blockierende Benachrichtigung**
  - LED + Buzzer laufen über einen Zustandsautomaten (`handleNotify()`).
  - Keine langen `delay()`-Blöcke mehr – die `loop()` bleibt reaktionsfähig.

- **New-Message-Reminder**
  - Sobald eine neue Nachricht empfangen wurde, wird ein Flag gesetzt.
  - Solange die Nachricht nicht durch Tastendruck „wahrgenommen“ wurde,
    sendet der Pager alle 30 Sekunden einen kurzen LED-Puls.

- **Energiesparoptimierungen (ESP32)**
  - CPU-Frequenz auf 80 MHz reduziert.
  - WiFi und Bluetooth bei Start deaktiviert.

Die Funk- und POCSAG-Grundlogik (RadioLib, `pager.begin()`, `pager.readData()`, RIC-Filterung) bleibt kompatibel mit dem Originalcode, wurde aber in ein erweitertes Gesamtkonzept mit Inbox, Zeit-Handling und UI integriert.
