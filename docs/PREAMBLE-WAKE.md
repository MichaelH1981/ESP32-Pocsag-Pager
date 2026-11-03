# Experimental POCSAG wake driver (0.5.0-dev)

This branch is not a release. Default builds retain continuous CPU reception.

The SX1278 stays in synchronous continuous FSK reception at 1200 baud. DIO1 on
GPIO33 remains the bit clock and DIO2 on GPIO34 the data input. DIO0 on GPIO26
is mapped to hardware PreambleDetect (RegDioMapping1 bits 7:6 = 01,
RegDioMapping2 bit 0 = 1). No additional wire is required on the tested T3.
The detector uses three bytes and tolerance 10 (RegPreambleDetect = 0xCA).
Only the preamble W1C flag is cleared; there is no restart of the SX1278 RX path.

Reference: Semtech SX1276/77/78/79 datasheet rev 7, tables 29 and FSK register
descriptions (RegPreambleDetect, RegIrqFlags1, RegDioMapping1/2).
https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf

## Build environments

- `ttgo-lora32-v21`: baseline, no sleeping (`PAGER_POWER_MODE=0`).
- `preamble-observe`: detector diagnostics with original capture running (mode 1).
- `preamble-sleep`: experimental Light-sleep and GPIO wake (mode 2).

Build with `pio run -e preamble-observe` or `pio run -e preamble-sleep`.
Personal settings remain in the ignored `src/config_local.h`.

## Sleep sequence

Sleeping requires display off, no notification/reminder pulse, no ADC sequence,
released buttons, no data meeting the normal two-batch decoder threshold,
eight seconds since activity,
and three seconds without RSSI above `PAGER_SLEEP_BUSY_RSSI_DBM` (-90 dBm).
RSSI only vetoes sleep; it is not
the wake trigger. A strong continuous interferer therefore reduces savings.
RSSI is read with `getRSSI(true)`: RadioLib's default argument would call
`startReceive()` and silently leave direct mode. The mock test asserts this.
Adjust the veto for the site and test weak, long transmissions before treating
reception as reliable.

The driver detaches only the ESP's bit ISR and clears partial software capture.
Before sleeping, the application drains complete words left in a final shorter
batch after the quiet/hold window. The regular two-batch receive threshold is
unchanged during a transmission. Only an incomplete final word is discarded.
It then enters Light-sleep, waking on DIO0 high, any button low, or a 250 ms timer
for housekeeping. It does not remap the radio clock or stop the receiver.
On RF/button wake, capture resumes before serial output, UI or ADC work.
GPIO/setup/sleep errors fall back to continuous CPU reception and are logged.

RadioLib is patched with an additive `resetDirectBuffer()` method. This may
only be called with the capture ISR detached. It clears both byte indices,
partial-byte state and software synchronisation before reacquiring a sync word.
The existing received-character bounds patch remains in place.

## What the tests must establish

1. Back up the full device flash, including LittleFS, before experiments.
2. Observe preamble candidates while receiving ordinary real POCSAG broadcasts.
3. Verify sleep entry, RF wake, valid decoding after wake, heap and reset reason.
4. Exercise buttons, clock and ADC after repeated timer/button/RF wake cycles.
5. Compare missed/corrupt messages against baseline using known transmissions.
6. Measure current at the battery with USB disconnected to establish savings.

PreambleDetect is not a validated POCSAG address or complete message. False
candidates can wake the ESP. Joining an already ongoing transmission and weak
signals require hardware testing. No guarantee of message delivery or battery
runtime follows from a successful build or a sleep-time counter.

If testing is inconclusive, leave the device in the baseline or observer build.
Do not publish as a stable release or enable sleep in default builds yet.

## Initial hardware results, 2026-11-03

The original complete 4 MiB flash was read before flashing any experiment. Both
the full backup and an app0-only rollback image are kept locally under the
ignored `artifacts/preamble-wake-2026-11-03` directory. The extracted ESP32 image
checksum and SHA-256 validation passed. LittleFS was not formatted.

Mode 1 detected a real preamble about 904 ms before decoding a time broadcast
on RIC 2000. Mode 2 then demonstrated four separate RF wake events,
correct time decoding on RIC 216 and 208 (12:12, 12:14 and 12:16 local), and
readable Skyper rubric labels on 4512. In the corrected
tail-draining build, a quiet 60-second interval added 59,795 ms to the sleep
counter. The timer-wake loop continued ADC readings and returned to sleep after
subsequent traffic. A rubric-39 warning woke the display and was persisted in the
warning folder. The free heap stayed around 211 kB (211,668 bytes initially,
211,468 after storing that warning); no unexpected reset was logged.

The functional driver run used the initial 0.4.2-dev label. Per the project
owner's request, this work is now versioned 0.5.0-dev; only the version string
changed between that tested binary and the renamed build.

Two issues found during testing were fixed before the final run: the default
RSSI accessor leaves continuous reception (use `getRSSI(true)`), and a single
trailing batch could keep the CPU awake forever when the consumer waits for two.
Initial logs before those corrections are not evidence of RF performance.

This is a short functional hardware test, not a measured battery-life result or
proof of lossless reception. Button wake, weak-signal comparison against known
transmissions, long messages and an endurance run remain to be checked. The
sleep counter measures elapsed time around the sleep API, including transition
overhead; it is not a current meter.
