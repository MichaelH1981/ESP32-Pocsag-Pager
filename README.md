# ESP32 DAPNET Pager — v0.4.1

POCSAG pager for the LILYGO T3 V1.6.1, based on
[ManoDaSilva's ESP32-Pocsag-Pager](https://github.com/ManoDaSilva/ESP32-Pocsag-Pager).
Receives DAPNET messages with RadioLib and provides a persistent inbox,
clock, battery voltage display and three-button navigation.

## Hardware and upgrade from earlier versions

**Remove the external LoRa bridge to GPIO35 before enabling battery measurement.**
GPIO35 is connected to the board's internal battery voltage divider. Older
firmware also used it for the radio clock, which interfered with voltage readings.
Version 0.4 uses the board's internal DIO1 connection on GPIO33 instead.
Keep the existing DIO2-to-GPIO34 bridge.

| Function | GPIO |
| --- | --- |
| LoRa SPI SCK / MISO / MOSI / CS | 5 / 19 / 27 / 18 |
| LoRa RESET / DIO0 / DIO1 | 23 / 26 / 33 |
| LoRa DIO2 data | 34 (existing external bridge) |
| Battery ADC | 35 (internal 100k/100k divider) |
| OLED SDA / SCL | 21 / 22 |
| OLED hardware reset | Disabled |
| UP / ENTER / DOWN | 12 / 13 / 15 |
| Buzzer / LED | 14 / 25 |

Connect buttons to ground. Do not use an SD card with this pin assignment:
its pins overlap with the buttons and buzzer. Other board revisions require
verification of the actual schematic and wiring.

## Build and configuration

1. Open `Arduino Sketch` in VS Code with PlatformIO.
2. Copy `src/config_local.h.example` to `src/config_local.h`.
3. Set your personal RIC and callsign. After removing the GPIO35 bridge,
   uncomment `#define BATTERY_ADC_PIN 35` to enable the internal battery ADC.
4. Build with `pio run` from `Arduino Sketch`, then use the PlatformIO upload task.

Personal settings are ignored by Git. Without a local configuration the project
builds, but personal RIC 0 and battery measurement are disabled. Frequency and
receiver offset are configured in `src/config.h`.

The project pins Espressif32 6.9.0 and RadioLib 5.6.0. A checked build script
applies a small, reproducible receive-buffer bounds fix to the pinned RadioLib
source; do not remove the `extra_scripts` setting from `platformio.ini`.

Firmware updates do not require a filesystem upload or a full flash erase.
A LittleFS mount failure disables persistence for that boot rather than
formatting the existing message store. A blank filesystem needs explicit setup.

## Operation

- Home: date/time, battery voltage in V, three folders with message counts.
- UP/DOWN on home: select Nachrichten, Wetter/Pegel or Warnmeldungen.
- Short ENTER: open inbox; from the inbox, open the action menu.
- Hold ENTER for 800 ms: return to home from any screen.
- UP/DOWN: browse messages or choose a menu item.
- Action menu: home, delete message, delete all **in the selected folder**, back.
- Deletion requires confirmation, with **No** selected by default.
- A short key press while the screen is off wakes it without executing a hidden action.

## Reception and time

Skyper news on RIC 4520 and rubric labels on 4512 are decoded without shifting
their protocol headers. Ordinary messages stay unchanged. Serial `[Pager] Raw`
lines intentionally show wire data; `[Skyper]` lines show decoded UTF-8 German
text. OLED/storage use Ae/Oe/Ue/ae/oe/ue/ss because the default font is not UTF-8.
Unexpected seven-bit controls in Skyper payloads become spaces; invalid headers
and non-seven-bit bytes are rejected. Unsupported symbols cannot be recovered
unambiguously from the sender's seven-bit encoding.

| Folder | Default routing | Capacity | Behavior |
| --- | --- | --- | --- |
| Nachrichten | Personal RIC and other configured calls | 64 | Existing notification tone |
| Wetter/Pegel | Skyper rubrics 61, 63, 80; RIC 1080 | 32 | Silent, no automatic screen switch |
| Warnmeldungen | Skyper rubric 39; RIC 1040 | 16 | Screen + LED reminder; optional separate tone |

Weather entries on 4520 replace the previous value for the same rubric/item.
Repeated identical warnings on the same RIC are suppressed even across item
numbers. Unselected Skyper rubrics remain terminal-only. `SKYPER_NEWS_INBOX=1`
additionally routes them to Nachrichten with normal notification.

Configure `WEATHER_SKYPER_RUBRICS`, `WARNING_SKYPER_RUBRICS`, `WARNING_RIC`,
`WARNING_AUDIBLE` and `WARNING_RINGTONE` in `config_local.h`. Defaults reflect the
observed local feed, not a universal emergency classification. Audible warning
alarms default off until the feed is verified. Set `WARNING_RIC=0` to disable
that address. Rubric lists may use 0 to select no rubric.

Each folder has independent storage and eviction. Existing `/inbox.log` remains
in Nachrichten; new folders use `/weather.log` and `/warnings.log`. Old records
are not reclassified. Weather writes are batched at 30-second intervals (pending
updates can be lost on power failure); other folders save immediately.

Both DAPNET time formats are supported:

| RIC | Format | Time basis |
| --- | --- | --- |
| 200 / 208 | `XTIME=HHmmddMMyy` | UTC / local |
| 216 / 224 | `YYYYMMDDHHMMSS` followed by `yyMMddHHmmss` | UTC / local |

UTC messages use the configured standard offset plus optional EU summer time.
Local messages are adopted as sent. Invalid dates leave the clock unchanged.
XTIME has minute precision; seconds are set to zero. Summer-time conversion is
performed when a UTC message arrives, not autonomously without synchronization.

## Battery voltage

The firmware samples calibrated ADC millivolts without stopping reception,
after a quiet settling period and outside notifications. It discards the first
reading, takes 16 samples and averages the middle 12 to reject outliers.
Measurements normally update every 30 seconds when conditions permit.

`-- V` means disabled, not yet measured or invalid. The displayed value is the
last plausible battery terminal voltage; USB charging can affect this voltage.
For a meter-based calibration use `BATTERY_CALIBRATION_GAIN` and
`BATTERY_CALIBRATION_OFFSET` in the local configuration.

## Verification and limitations

Version 0.4 was built and tested on a T3 V1.6.1: boot, battery display around
4.18 V, button navigation, reception on GPIO33 and RIC208 synchronization were
confirmed. Host tests cover the actual RadioLib receive function, buffer limits,
UI transitions, battery filtering, Skyper decoding and time/calendar conversion.
See [release notes](RELEASE-v0.4.1.md) and [test instructions](tests/README.md).

The reproduced heap overflow is fixed by checking every received character
against a fixed 512-byte payload capacity; oversized messages are discarded.
This does not redesign RadioLib's message assembly or add BCH correction.
Long/back-to-back messages and prolonged high traffic still need endurance
coverage. The existing inbox rewrites the file on message changes.

## Libraries and credits

- [RadioLib](https://github.com/jgromes/RadioLib)
- Adafruit SSD1306, GFX and BusIO
- Original pager concept by ManoDaSilva; extended firmware by this repository.

See [LICENSE](LICENSE).
