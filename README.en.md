# ESP32 DAPNET Pager

[Deutsch](README.md) · [English](README.en.md)

A small DAPNET and POCSAG pager for the LILYGO T3 V1.6.1. It is based on
[ManoDaSilva's ESP32-Pocsag-Pager](https://github.com/ManoDaSilva/ESP32-Pocsag-Pager)
and grew from a proof of concept into a pager intended for everyday use.

The current firmware receives personal calls, weather and water-level data, and
configurable warning messages. It shows the time and battery voltage and keeps
received messages in flash storage.

## Version 0.5

**0.5.0** fixes truncated and corrupted received text. The new decoder assembles
messages across batch boundaries and checks the POCSAG codewords. The home
screen also shows the RSSI peak of the last valid reception. See the
[changelog](CHANGELOG.md).

The optional sleep mode remains experimental. The SX1278 keeps listening and
wakes the ESP32 on a preamble. Build it with `pio run -e preamble-sleep`. The default
build uses the same decoder with the ESP32 continuously awake. See the
[driver notes](docs/PREAMBLE-WAKE.md) for wiring, test results and remaining work.
Current draw and battery life have not yet been measured.

## Features

- DAPNET/POCSAG reception using the SX1278
- personal messages with an audible alert
- separate folders for messages, weather/water levels, and warnings
- decoding of Skyper news on RIC 4520
- clock synchronisation from DAPNET time RICs
- battery voltage display using the board's internal voltage divider
- three-button navigation; hold ENTER to return to the home screen
- persistent message storage using LittleFS
- receive-buffer limits to prevent the reboot seen with long messages in older
  versions

## Hardware

Version 0.5 also shows the cached RSSI peak of the last checked
reception on the home screen, in dBm. It reuses read-only RSSI sampling; details
are in the [receive investigation](docs/RECEIVE-DECODER.md#rssi-display-rx2).

The firmware targets the **LILYGO T3 V1.6.1** with an SX1278 and has been tested
on that board.

> **Important when upgrading from an older version:** Remove the additional
> connection between the LoRa module and GPIO35. On the T3 V1.6.1, GPIO35 is
> connected to the internal battery voltage divider. Since version 0.4, DIO1
> uses the board's internal GPIO33 connection. Keep the existing DIO2-to-GPIO34
> connection.

| Function | GPIO |
| --- | --- |
| LoRa SCK / MISO / MOSI / CS | 5 / 19 / 27 / 18 |
| LoRa RESET / DIO0 / DIO1 | 23 / 26 / 33 |
| LoRa DIO2 | 34, external connection |
| Battery measurement | 35, internal 100k/100k divider |
| OLED SDA / SCL | 21 / 22 |
| UP / ENTER / DOWN buttons | 12 / 13 / 15 |
| Buzzer / LED | 14 / 25 |

Connect the buttons to ground. An SD card cannot be used with this pin layout
because several pins are shared. Check the schematic before using another board
revision.

## Installation

The project is built with PlatformIO:

1. Open the `Arduino Sketch` folder in VS Code with PlatformIO.
2. Copy `src/config_local.h.example` to `src/config_local.h`.
3. Enter your personal RIC and callsign.
4. After removing the old GPIO35 connection, enable
   `#define BATTERY_ADC_PIN 35`.
5. Build and upload the firmware using PlatformIO.

Git ignores the personal `config_local.h` file. Frequency and receiver offset
are set in `src/config.h`. A normal firmware update keeps the message store and
does not require a separate LittleFS upload.

## Controls

Use UP and DOWN on the home screen to select one of the three folders. ENTER
opens it. Inside a folder, UP and DOWN browse the messages and ENTER opens the
delete/back menu. Holding ENTER returns to the home screen from any view.

Weather and water-level reports are stored silently. A new value with the same
Skyper rubric and item number replaces the previous value. Identical warning
messages are not stored twice. The RIC and Skyper routing can be changed in
`config_local.h`.

| Folder | Default routing | Capacity |
| --- | --- | ---: |
| Messages | personal RIC and other configured calls | 64 |
| Weather/levels | Skyper 61, 63, 80 and RIC 1080 | 32 |
| Warnings | Skyper 39 and RIC 1040 | 16 |

The warning alert sound is disabled by default until the selected RIC or rubric
has been verified for the local feed.

## Reception notes

In the serial monitor, `[Pager] Raw` is the data received over the air. The
decoded Skyper text is printed on the following `[Skyper]` line. Because of the
OLED font, German characters are shown as `Ae`, `Oe`, `Ue`, and `ss` on the
display.

The firmware accepts the time formats on RIC 200/208 and 216/224. UTC messages
use the configured time zone and European daylight saving rules. Local-time
messages are used as sent.

Battery readings are taken during quiet receive periods. Several samples are
averaged so that short load changes do not immediately appear as a false voltage
on the display. The reading can be calibrated against a multimeter in
`config_local.h`.

## Current status and limitations

Version 0.5.0 was tested on a LILYGO T3 V1.6.1. Boot, reception, time sync,
battery readings, button navigation, and the routing of weather and Skyper
messages were checked on the device. Automated tests also cover the parsers,
calendar handling, folders, and receive-buffer limit.

Messages larger than the receive buffer are discarded. Version 0.5 uses a
streaming decoder with BCH error correction and checked message boundaries.
Personal messages, time broadcasts and water-level reports were compared with
the Pi-Star originals. Weak-signal and longer endurance testing remain limited.
See the [receive investigation](docs/RECEIVE-DECODER.md).

See the [changelog](CHANGELOG.md) and the
[test instructions](tests/README.md) for more detail.

## Libraries and credits

- [RadioLib](https://github.com/jgromes/RadioLib)
- Adafruit SSD1306, GFX, and BusIO
- original pager project by [ManoDaSilva](https://github.com/ManoDaSilva/ESP32-Pocsag-Pager)

License: [LICENSE](LICENSE)
