# v0.4.0

This release adds a home screen with battery voltage and a reliable return path
from the inbox, and fixes a reproduced heap overflow during POCSAG decoding.

## Changes

- Home screen with clock, battery volts and message count; long ENTER returns
  home. Short ENTER opens messages/actions. Deletion requires confirmation.
- Stable key debouncing and distinct short/long presses; wake-only short presses.
- T3 V1.6.1 battery support: calibrated ADC readings on GPIO35, 16-sample trimmed
  averaging, delayed/non-blocking sampling and configurable calibration.
- Radio clock moved to native GPIO33, freeing the battery ADC pin.
- Project-local RadioLib patch enforces the caller's capacity before every
  decoded character and reads only complete codewords. Application uses a fixed
  512-byte message buffer. Oversized messages return an error instead of corrupting heap.
- Skyper payload decoding on RIC 4520/4512 with separate protocol headers.
- XTIME and AlphaPOC time formats; UTC/local distinction, valid calendar checks,
  leap years and configurable EU summer-time conversion.
- Preserve LittleFS content on mount failure; explicit personal configuration,
  accurate subscription count and compile-time pin-conflict checks.
- Keep OLED hardware reset disabled; report reset reason at startup.

## Required hardware migration

For the T3 V1.6.1, remove the old external LoRa-to-GPIO35 bridge before enabling
battery measurement. DIO1 uses the board's native GPIO33 connection; keep the
DIO2-to-GPIO34 bridge. Enable `BATTERY_ADC_PIN 35` in local configuration only
after this change. Older firmware using DIO1=35 needs wiring/code adaptation
before it can serve as a rollback.

## Validation

- The original RadioLib 5.6.0 out-of-bounds write was reproduced using the actual
  receive function with a simulated radio under AddressSanitizer. The patched
  function passes oversized, short-message and incomplete-codeword tests.
- Sanitizer-enabled UI, key, battery, Skyper and time/calendar tests pass.
- ESP32 build succeeds; device boot, battery display, navigation and live RIC208
  reception/time synchronization were confirmed on the T3 V1.6.1.

Remaining coverage: prolonged heavy traffic, long/back-to-back message behavior,
and precision calibration against a multimeter. This release does not add BCH
error correction or replace RadioLib's message assembly.

## References

- [LILYGO T3 V1.6.1 pin map](https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/docs/en/t3_v161_sx1276/t3_v161_sx1276_hw.md)
- [DAPNET Skyper/time formats](https://github.com/DecentralizedAmateurPagingNetwork/Core/blob/master/src/main/java/org/dapnet/core/transmission/SkyperProtocol.java)
