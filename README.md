# ESP32 DAPNET Pager

[Deutsch](README.md) · [English](README.en.md)

Ein kleiner DAPNET- und POCSAG-Pager für das LILYGO T3 V1.6.1. Das Projekt
basiert auf dem [ESP32-Pocsag-Pager von ManoDaSilva](https://github.com/ManoDaSilva/ESP32-Pocsag-Pager)
und ist aus dem Wunsch entstanden, aus dem ursprünglichen Versuchsaufbau einen
Pager zu machen, den man tatsächlich im Alltag benutzen kann.

Die aktuelle Firmware empfängt persönliche Rufe, Wetter- und Pegeldaten sowie
konfigurierbare Warnmeldungen. Sie zeigt Uhrzeit und Akkuspannung an und speichert
Nachrichten dauerhaft im Flash.

## Entwicklung: Version 0.5

Auf diesem Zweig entsteht **0.5.0-dev** mit einem stromsparenden Empfangstreiber.
Der SX1278 bleibt im Empfang und weckt den ESP32 bei einer erkannten Präambel aus
dem Light-Sleep. Erste Tests mit Zeit-, Skyper- und persönlichen Nachrichten
funktionieren. Stromaufnahme und Laufzeit sind noch nicht gemessen.

Der Schlafmodus wird ausdrücklich mit `pio run -e preamble-sleep` gebaut; der
Standard-Build verwendet weiterhin den bisherigen Empfang ohne Light-Sleep.
Den Aufbau, die Testergebnisse und offene Punkte beschreibt die
[Treiber-Dokumentation](docs/PREAMBLE-WAKE.md).

## Was die Firmware kann

- DAPNET/POCSAG-Empfang über den SX1278
- persönliche Nachrichten mit Rufsignal
- getrennte Postfächer für Nachrichten, Wetter/Pegel und Warnmeldungen
- Dekodierung der Skyper-Nachrichten auf RIC 4520
- Uhrzeitsynchronisation über die DAPNET-Zeit-RICs
- Akkuspannungsanzeige über den internen Spannungsteiler des Boards
- Bedienung mit drei Tasten und Rücksprung zum Hauptbildschirm durch langes ENTER
- dauerhafte Speicherung mit LittleFS
- Schutz vor zu langen Empfangsdaten, die in älteren Versionen einen Neustart
  auslösen konnten

## Hardware

Die Firmware ist für das **LILYGO T3 V1.6.1** mit SX1278 geschrieben und auf
diesem Board getestet.

> **Wichtig beim Umstieg von älteren Versionen:** Die zusätzliche Verbindung
> zwischen dem LoRa-Modul und GPIO35 muss entfernt werden. GPIO35 gehört beim
> T3 V1.6.1 zum internen Spannungsteiler für die Akkumessung. DIO1 wird ab
> Version 0.4 über den boardinternen GPIO33 geführt. Die bestehende Verbindung
> von DIO2 zu GPIO34 bleibt erhalten.

| Funktion | GPIO |
| --- | --- |
| LoRa SCK / MISO / MOSI / CS | 5 / 19 / 27 / 18 |
| LoRa RESET / DIO0 / DIO1 | 23 / 26 / 33 |
| LoRa DIO2 | 34, externe Verbindung |
| Akkumessung | 35, interner 100k/100k-Spannungsteiler |
| OLED SDA / SCL | 21 / 22 |
| Tasten UP / ENTER / DOWN | 12 / 13 / 15 |
| Summer / LED | 14 / 25 |

Die Tasten werden gegen Masse geschaltet. Eine SD-Karte kann mit dieser
Pinbelegung nicht gleichzeitig verwendet werden, weil mehrere Pins gemeinsam
genutzt werden. Bei anderen Boardrevisionen bitte zuerst den Schaltplan prüfen.

## Installation

Das Projekt wird mit PlatformIO gebaut:

1. Den Ordner `Arduino Sketch` in VS Code mit PlatformIO öffnen.
2. `src/config_local.h.example` nach `src/config_local.h` kopieren.
3. Eigene RIC und Rufzeichen eintragen.
4. Nach dem Entfernen der alten GPIO35-Verbindung die Zeile
   `#define BATTERY_ADC_PIN 35` aktivieren.
5. Firmware bauen und über PlatformIO auf das Board laden.

Die persönliche `config_local.h` wird von Git ignoriert. Frequenz und
Empfänger-Offset stehen in `src/config.h`. Ein normales Firmware-Update löscht
den Nachrichtenspeicher nicht und benötigt keinen separaten LittleFS-Upload.

## Bedienung

Auf dem Hauptbildschirm wählt man mit UP und DOWN eines der drei Postfächer aus.
ENTER öffnet das gewählte Fach. Innerhalb eines Fachs blättern UP und DOWN durch
die Nachrichten; ENTER öffnet das Menü zum Löschen oder für den Rücksprung.
Langes Drücken auf ENTER führt von jeder Ansicht zurück zum Hauptbildschirm.

Wetter- und Pegeldaten werden still gespeichert. Ein neuer Wert mit derselben
Skyper-Rubrik und Eintragsnummer ersetzt den vorherigen Wert. Identische
Warnmeldungen werden nicht mehrfach abgelegt. Welche RICs und Skyper-Rubriken in
welchem Fach landen, lässt sich in `config_local.h` anpassen.

| Postfach | Voreinstellung | Plätze |
| --- | --- | ---: |
| Nachrichten | persönliche RIC und weitere konfigurierte Rufe | 64 |
| Wetter/Pegel | Skyper 61, 63, 80 und RIC 1080 | 32 |
| Warnmeldungen | Skyper 39 und RIC 1040 | 16 |

Der Warnton für Warnmeldungen ist absichtlich ausgeschaltet, bis die verwendete
RIC oder Rubrik vor Ort eindeutig geprüft wurde.

## Hinweise zum Empfang

Im seriellen Monitor zeigt `[Pager] Raw` die tatsächlich empfangenen Rohdaten.
Die lesbare Skyper-Fassung steht darunter in der Zeile `[Skyper]`. Das OLED nutzt
wegen seines Zeichensatzes `Ae`, `Oe`, `Ue` und `ss` anstelle der deutschen
Sonderzeichen.

Unterstützt werden die Zeitformate auf RIC 200/208 und 216/224. UTC-Nachrichten
werden mit der eingestellten Zeitzone und der europäischen Sommerzeit
umgerechnet; lokale Zeitnachrichten werden direkt übernommen.

Die Akkumessung läuft nur in ruhigen Empfangsphasen. Mehrere Messwerte werden
gemittelt, damit kurze Lastwechsel nicht sofort als falsche Spannung auf dem
Display erscheinen. Bei Bedarf kann die Anzeige in `config_local.h` mit einem
Multimeterwert abgeglichen werden.

## Stand und Grenzen

Version 0.4.1 wurde auf einem LILYGO T3 V1.6.1 getestet. Geprüft wurden Start,
Empfang, Zeitabgleich, Akkumessung, Tastenbedienung und die Zuordnung von Wetter-
und Skyper-Nachrichten. Automatisierte Tests decken zusätzlich Parser,
Kalenderlogik, Postfächer und die Begrenzung des Empfangspuffers ab.

Die Firmware verwirft Nachrichten, die größer als der Empfangspuffer sind. Eine
BCH-Fehlerkorrektur und ein grundlegender Umbau der RadioLib-Nachrichtenmontage
gehören derzeit nicht zum Projekt.

Weitere Details stehen in den [Hinweisen zu Version 0.4.1](RELEASE-v0.4.1.md)
und in der [Testanleitung](tests/README.md).

## Bibliotheken und Herkunft

- [RadioLib](https://github.com/jgromes/RadioLib)
- Adafruit SSD1306, GFX und BusIO
- ursprüngliches Pager-Projekt von [ManoDaSilva](https://github.com/ManoDaSilva/ESP32-Pocsag-Pager)

Lizenz: [LICENSE](LICENSE)
