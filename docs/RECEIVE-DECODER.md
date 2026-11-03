# Empfangsfehler / receive investigation (0.5.0)

## Deutsch

Der Abendmitschnitt und der ausgelesene Nachrichtenspeicher enthalten sowohl
abgeschnittene Texte als auch tatsächlich beschädigte Wetter- und Warnmeldungen.
Das ist von der beabsichtigten Skyper-Kodierung zu unterscheiden: Auf RIC 4520
ist der Rohtext um eins verschoben; erst die Ausgabe `[Skyper] ... Text:` ist
lesbarer Text. Auch diese dekodierten Texte waren teilweise beschädigt.

Im bisher verwendeten RadioLib-5.6.0-Empfangspfad wurden folgende Fehler gefunden:

- `readData()` meldet Erfolg, wenn der Puffer leer ist, obwohl das Ende einer
  Nachricht noch nicht empfangen wurde. Zeichenreste gehen beim nächsten Aufruf
  verloren. Das erklärt abgeschnittene Meldungen.
- Eine folgende Adresse beendet die Nachricht nicht, sondern wird ebenfalls als
  Text dekodiert. Direkt aufeinanderfolgende Nachrichten können sich vermischen.
- Der zweite Codewortplatz eines Rahmens wird dem nächsten Rahmen zugeordnet;
  zusätzlich beginnt der Rahmenzähler bei jedem Aufruf neu. Die unteren RIC-Bits
  stimmen dann nicht mehr.
- Der direkte Empfangspuffer verwendet denselben Zähler als Schreibposition
  und verfügbare Länge, während der Leser ihn herunterzählt. Lesen setzt außerdem
  standardmäßig die Synchronisation zurück. Das ist kein nebenläufig nutzbarer
  Ringpuffer; neue Synchronisation setzt beide Positionen zurück.
- BCH-Prüfbits und Gesamtparität werden beim Empfang nicht ausgewertet.
  Beschädigte Wörter können dadurch als Adresse oder Text durchgehen.

Die Montage- und Pufferfehler wurden mit den tatsächlich installierten RadioLib-
Methoden reproduziert. Sie bestehen unabhängig vom Schlafmodus. Die genaue
Aufteilung aller bisherigen Funkfehler auf diese Ursachen lässt sich ohne
damals gespeicherte Bitfolgen nicht nachträglich bestimmen.

Der neue Anwendungscode ersetzt diesen Empfangspfad. RadioLib übernimmt weiter
die SX1278-Konfiguration. Ein eigener Interrupt liest GPIO34 zur steigenden
Flanke an GPIO33; seine gesamte Aufrufkette und der GPIO-Interruptdienst liegen
im internen RAM. Der 256-Wort-Puffer hat getrennte atomare Lese- und
Schreibpositionen. Ein Überlauf überschreibt keine ungelesenen Daten, sondern
verwirft die unvollständige Nachricht und erzwingt neue Synchronisation.

Der fortlaufende Decoder erhält Zeichenreste und Rahmenposition über Aufrufe
und Blöcke hinweg. Nur ein geprüftes Leerlauf- oder Adresswort schließt eine
Nachricht ab. Zeitüberschreitung, Synchronisationsverlust, nicht korrigierbare
Codewörter oder mehr als 512 Zeichen verwerfen die betroffene Nachricht.
Er korrigiert bis zu zwei Bitfehler je Codewort und erkennt alle Drei-Bit-Fehler.
Wie bei begrenzter BCH-Fehlerkorrektur üblich können stärkere Fehler teilweise
zu einem anderen gültigen Codewort führen; dies ist keine kryptografische
Integritätsprüfung. Die zusätzlichen Tabellen belegen etwa 8 KiB RAM,
der Bitpuffer etwa 1 KiB. Der Schlafmodus bleibt optional.

Die Firmware meldet vollständige Nachrichten als `Complete BCH-checked message`.
Die `[RX]`-Statistik zählt gültige Wörter, korrigierte Bits, verworfene Wörter,
Synchronisationsverluste, verworfene Nachrichten und Pufferüberläufe getrennt.
Die bisherigen Dateien auf LittleFS werden nicht bereinigt; alte beschädigte
Einträge können bis zum nächsten Update oder manuellen Löschen sichtbar bleiben.

## English

Stored messages showed both premature endings and corrupted decoded text. Skyper
raw text on RIC 4520 is intentionally shifted by one; that encoding does not
explain corruption in the decoded `[Skyper] ... Text:` output.

The legacy RadioLib 5.6.0 receive path returned unfinished messages as successful,
decoded subsequent address words as message payload, and assigned incorrect
frame bits to some RICs. Its direct buffer shared a producer index with a count
decremented by the consumer, and ordinary reads dropped synchronization. Receive
code did not validate BCH or parity. The assembly and buffer defects are reproduced
against the installed method bodies. They are independent of Light-sleep; old
logs alone cannot attribute every historical error to a particular defect.

The application now owns capture and streaming assembly. RadioLib still
configures the radio. An IRAM GPIO handler and service feed a 256-word SPSC queue
with separate atomic indices. Overflow drops the incomplete page instead of
overwriting unread data. The decoder preserves frame and character state across
batches, validates BCH and even parity, corrects up to two bit errors per word,
and detects all three-bit errors. Stronger corruption can exceed the code's
detection capability. Pages finish only at a checked idle or next address word.
Timeout, loss of sync, uncorrectable words and pages exceeding 512 characters
discard the affected page. Existing stored messages are preserved.

## Verification

- Host tests cover all frame positions, adjacent pages, 512-character pages,
  partial input, loss, timeout, queue wrapping/overflow and bit errors.
- An independent reference test uses RadioLib's actual GF encoder for 50,000
  codewords; it also reproduces the legacy assembly defects.
- ESP32 disassembly confirms that the bit handler has no calls into flash;
  the GPIO interrupt service is allocated with `ESP_INTR_FLAG_IRAM`.
- Standard and Light-sleep builds pass. The `v0.5.0-dev-rx1` hardware run compares
  serial output against the Pi-Star DAPNET activity list. This is development
  firmware, not a stable release. Hardware results are recorded separately below.

Protocol reference: [ITU-R M.584-2, Annex 1](https://www.itu.int/dms_pubrec/itu-r/rec/m/r-rec-m.584-2-199711-i%21%21pdf-e.pdf).
Test commands: [tests/README.md](../tests/README.md).

## RSSI display (rx2)

The home screen adds `RX max: … dBm` for the last checked reception; before the
first measurement it shows `--`. This is the peak of up to 16 recent register
samples no older than 500 ms, avoiding a noise-floor reading if the carrier ends
just before the final word is consumed. It is a receiver indication, not a
calibrated RF measurement or a per-message measurement of averaged power.

Mode 1/2 reuse the existing 20 ms RSSI polling. Mode 0 performs the same read-only
poll without enabling preamble detection or sleep. Every access explicitly uses
`getRSSI(true)`: no receiver restart, standby or DIO remapping is requested for
the display. Only the foreground loop accesses SPI; the bit interrupt is
unchanged. The level is cached on a checked message and remains visible through
idle periods. The display is not woken merely to show RSSI.

`rx3` preserves shifted Skyper control bytes inside a message. ETX/EOT padding
is removed only at the checked end, rather than terminating on those values in
the middle of a payload. A regression uses a full APRS weather text from the
Pi-Star and a control byte inserted between words, including 512-character
pages followed by padding.

## Hardware comparison, 2026-11-03

The Pi-Star DAPNET activity list was read every ten seconds while serial output
was captured locally. Flash images and logs are kept in the ignored artifacts
directory; they are not release assets. Only app0 was written during updates.

From 19:14 to 19:22 CET, `rx1` completed 14 messages. All 14 matched a Pi-Star
original exactly, including the owner's 79-character personal test from its
beginning through its ending. That page was saved and restored after the next
firmware update. The last report recorded seven RF wakes, eight corrected bits,
one rejected codeword, no discarded pages and no buffer overruns. The peak idle
heap was about 202 kB; after the first personal notification the heap remained
about 195 kB (the existing Arduino tone task allocates on first use).

`rx2` completed six further matching messages before the next update. Received
RSSI peaks were -39.5 dBm; idle readings were roughly -100 dBm. Sleep and
subsequent RF wake continued with RSSI sampling enabled. None of these continuous
captures logged a heap assertion, panic, LEDC initialization error or unexpected
reset. Opening the USB serial port and updating firmware reset this board as
expected; those resets are not evidence of an application boot loop.

From 19:29 to 19:37 CET, `rx3` completed 23 further messages, all matching the
Pi-Star originals. This includes six decoded Skyper rubric-61 entries on RIC
4520 (source, Perl, Trier Up, Speyer, Koblenz and Andernach), along with their
ordinary-text equivalents. In particular, the previously corrupted Speyer
entry was replaced with the complete current text. The final counters were
542 checked words, five corrected bits, two rejected codewords, zero discarded
pages, zero loss of sync during an incomplete page, and zero buffer overruns.
The free heap remained about 201.7 kB after the weather updates. RSSI peaks
were -39.5 to -40.0 dBm. All three firmware build environments passed.

After the coalesced weather write completed, LittleFS was read from the device
without writing or formatting it. A read-only analysis of a RAM copy confirmed
all six current rubric-61 texts exactly, as well as the complete personal test.
The former corrupted Speyer record had been replaced by the current valid text.

Across the three runs, 43 completed messages matched the Pi-Star originals.
These checks do not establish weak-signal reliability or long-term endurance.
A full warning-broadcast round has not yet been
observed with this decoder; its message assembly and routing are covered by
host tests. Previously stored corrupted messages are not automatically deleted.

## Release check, 0.5.0

Before publication, the complete host test set passed again, including the
independent 50,000-word BCH reference. All three build environments passed with
the owner's configuration and again from a separate copy without private
`config_local.h`.

The final `v0.5.0` image was flashed to the T3 and checked from 22:33 to 22:37 CET.
All 18 completed messages matched the Pi-Star originals, including all six
Skyper rubric-61 entries from the 22:35 water-level round. The final report
showed 463 checked words, four corrected bits, one rejected word, no discarded
pages, no lost sync during a page, and no capture overrun. Received RSSI peaks
were -39.5 to -40.0 dBm. The capture contained no unexpected reset or heap error.
Version 0.5.0 keeps Light-sleep opt-in; these checks do not measure battery life.

`rx1` counted a missing next-batch sync even after a complete idle-terminated
transmission as `lost-sync`. `rx2` counts it only while a page is incomplete;
ordinary transmission endings are not failures. The decoding rule is unchanged.

The final LittleFS readback confirmed all six 22:35 rubric-61 texts exactly,
and the existing personal test was retained. No filesystem upload was used.
