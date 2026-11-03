# Changelog

## 0.5.0 — 3. November 2026

### Deutsch

Einige Pegel- und Wettermeldungen kamen bisher abgeschnitten oder mit falschen
Zeichen an. Der Empfang ist deshalb neu aufgebaut: Nachrichten werden über
Blockgrenzen hinweg zusammengesetzt und erst nach einem erkannten Ende
übernommen. Die POCSAG-Prüfbits werden jetzt ausgewertet; bis zu zwei Bitfehler
pro Codewort lassen sich korrigieren. Nicht korrigierbare Nachrichten werden
verworfen.

- Auf der Hauptseite zeigt `RX max` die Signalstärke des letzten gültigen
  Empfangs in dBm. In Empfangspausen bleibt der Wert stehen.
- Es gibt einen optionalen Schlafmodus: Der SX1278 hört weiter und weckt den
  ESP32 bei einer Präambel. Diese Variante bleibt experimentell und wird mit
  `pio run -e preamble-sleep` gebaut. Im normalen Build bleibt der ESP32 wach.
- Die PWM-Initialisierung des Summers ist korrigiert. Die Fehlermeldung beim
  ersten Benachrichtigungston sollte damit ausbleiben.

Der Nachrichtenspeicher bleibt beim Update erhalten. Ein LittleFS-Upload ist
nicht nötig. Bereits gespeicherte kaputte Wettertexte werden beim nächsten
Datenupdate ersetzt oder können von Hand gelöscht werden.

Getestet auf dem LILYGO T3 V1.6.1, mit Vergleich der empfangenen Texte gegen den
Pi-Star. Persönliche Testnachricht, Zeitabgleich und Pegelrunde kamen vollständig
an. Die automatisierten Tests prüfen unter anderem lange Nachrichten,
aufeinanderfolgende Rufe, Bitfehler, Postfächer und RSSI. Stromaufnahme und
Laufzeit sind noch nicht gemessen; schwache Signale sind noch nicht umfassend
getestet.

### English

Some weather and water-level reports were truncated or contained corrupted
text. The receive path has been rebuilt: messages are assembled across batch
boundaries and delivered only after their end is recognized. POCSAG check bits
are now validated, with correction of up to two bit errors per codeword.
Uncorrectable messages are discarded.

- `RX max` on the home screen shows the RSSI of the last valid reception in
  dBm. The value stays visible between transmissions.
- An optional sleep mode keeps the SX1278 listening and wakes the ESP32 on a
  preamble. It remains experimental; build it with `pio run -e preamble-sleep`.
  The normal build keeps the ESP32 awake.
- Buzzer PWM is initialized before use to avoid the diagnostic on the first
  notification tone.

Existing messages are preserved. No LittleFS upload is needed. Old corrupted
weather entries are replaced by later updates or can be deleted manually.

Tested on a LILYGO T3 V1.6.1 against the Pi-Star originals, including a personal
test message, time broadcasts and water-level reports. Automated tests cover
long and consecutive messages, bit errors, folders and RSSI. Current draw and
battery life have not been measured; weak-signal testing is still limited.

## Frühere Versionen / Earlier versions

- [0.4.1 — getrennte Postfächer und Skyper-Texte](RELEASE-v0.4.1.md)
- [0.4.0 — Akkuspannung und Bedienung](RELEASE-v0.4.0.md)
