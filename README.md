# G502 Bluetooth Mod

[![Build](https://github.com/dosenfisch1/g502-bluetooth-mod/actions/workflows/build.yml/badge.svg)](https://github.com/dosenfisch1/g502-bluetooth-mod/actions/workflows/build.yml)

Macht aus einer **Logitech G502** (HERO) eine Bluetooth-LE-Maus, ohne Dongle.
Ein ESP32-S3 Super Mini arbeitet als USB-Host, liest die Maus über USB aus und gibt Maus und
Tastatur (G-Tasten) per BLE an den PC weiter. Gedacht war ein Einbau in die Maus mit Schalter.

## Projektstatus: pausiert
**Die Firmware funktioniert.** Getestet mit einer G502 Lightspeed, die per Kabel am ESP hängt.
Der geplante Einbau in die Maus ist **nicht abgeschlossen**.

**Offenes Problem:** Die G502 Lightspeed hat einen eigenen Akku und läuft ohne Spannung an VBUS
im Funkmodus. Der ESP kann sie dann nicht per USB auslesen. Gemessen:
- ohne VBUS: Maus bleibt im Funkmodus, auch bei angeschlossenen D+/D−
- 3,3 V an VBUS: keine Reaktion
- 3,8 V (Akkuspannung) an VBUS: Maus erkennt USB, lädt dabei aber ihren internen Akku

Gewünscht war, den USB-Modus ohne Step-up und ohne Laden des internen Akkus auszulösen, und
die Maus soll bei ausgeschaltetem ESP weiter normal per USB laufen. Dafür habe ich keinen Weg
mit vertretbarem Aufwand gefunden, daher ruht das Projekt.

**Bei der kabelgebundenen G502 (HERO) tritt das Problem nicht auf.** Sie hat keinen Akku und ist
immer im USB-Modus, sobald sie 5 V bekommt. Dort bleibt nur die 5-V-Versorgung über einen
Boost-Wandler (siehe Hardware).

### Ideen zum Weitermachen (ungetestet)
Das sind Hypothesen, keine geprüften Lösungen:
1. **Akku direkt auf VBUS.** Liegen VBUS und der interne Akku an derselben Zelle, hat der Lader
   vermutlich keinen Spannungsabstand und lädt nicht, die Erkennung könnte trotzdem ansprechen.
   Test: mA-Meter mit Sicherung zwischen Akku+ und VBUS-Pad der Micro-USB-Buchse, dann Strom messen.
2. **Ideal-Diode mit Enable** (z. B. TI LM66100, Datenblatt prüfen) zwischen Akku und VBUS. Sie
   sperrt den Rückstrom, wenn ein PC-Kabel 5 V liefert. Den Enable-Pin kann der ESP schalten.
   Die Firmware hat dafür die Option `MOUSE_VBUS_EN_GPIO` (Standard `-1`, aus).
3. **Analogschalter für D+/D−** (z. B. TS3USB221A oder FSUSB42), damit der unbestromte ESP die
   Datenleitungen bei Kabelbetrieb nicht stört. Auf Sperren bei VCC = 0 im Datenblatt achten.
4. **Erkennungspunkt der VBUS-Erkennung auf der Platine finden** und dort nur das Signal auslösen.
   Dazu fehlen Schaltplan und lesbare Bauteilaufdrucke.

## Funktionen
- Maus mit 16 Tasten, 16-Bit-Auflösung, Rad und horizontalem Scrollen (Rad kippen)
- G-Tasten als Tastatur, z. B. Strg+Bild hoch/runter für Tabwechsel
  (Belegung muss im G HUB im Modus **Integrierter Speicher** liegen)
- Bewegungen werden gesammelt und alle 8 ms gesendet, kurze Klicks gehen nicht verloren
- Akkustand über den BLE-Battery-Service (optional, ADC-Messung)
- On-board-RGB-LED (GPIO48) wird beim Start ausgeschaltet
- Hot-Plug: Maus lässt sich abstecken und wieder anstecken, die Firmware startet die Schnittstellen neu

## Fertige Firmware flashen
Unter [Releases](https://github.com/dosenfisch1/g502-bluetooth-mod/releases) liegt `g502-ble-merged.bin`.
Die Datei enthält Bootloader, Partitionstabelle und App und gehört an Adresse `0x0`.

Geflasht wird über **UART** (GPIO43 = TX, GPIO44 = RX), weil der USB-C-Port der Maus gehört.
Du brauchst dafür einen 3,3-V-USB-UART-Adapter (CP2102, CH340 oder FTDI):

| Super Mini | Adapter |
|------------|---------|
| GPIO43 (TX) | RX |
| GPIO44 (RX) | TX |
| GND | GND |

Das Board versorgst du separat, nicht über den Adapter.

    pip install esptool
    python -m esptool --chip esp32s3 -p COM3 -b 460800 write_flash 0x0 g502-ble-merged.bin

`COM3` durch den Port deines Adapters ersetzen. Download-Modus: BOOT gedrückt halten,
RESET tippen, BOOT loslassen. Danach das Board einmal neu starten.

Alternativ im Browser mit dem Esptool-JS-Webflasher von Espressif (Chrome oder Edge).

Danach am PC in den Bluetooth-Einstellungen nach **G502 BLE** suchen und koppeln.

## Das Firmware-Image selbst bauen

### Mit einem Befehl (Ergebnis landet in `firmware/`)
Im ESP-IDF-Terminal (VS Code: `F1` → **ESP-IDF: Open ESP-IDF Terminal**) im Projektordner:

    powershell -ExecutionPolicy Bypass -File .\tools\build-firmware.ps1

Das Skript baut das Projekt, führt Bootloader, Partitionstabelle und App zusammen und legt
`firmware/g502-ble-merged.bin` ab (plus `firmware/g502-ble-app.bin`). Am Ende zeigt es
Größe, SHA256 und den Flash-Befehl. Danach ins Repo:

    git add firmware
    git commit -m "Firmware-Image"
    git push

### Einzelne Schritte in VS Code
1. Projekt bauen: `Strg+E B`
2. `F1` → **ESP-IDF: Open ESP-IDF Terminal** (nur dort ist `esptool` bekannt)
3. Im Terminal:

       idf.py merge-bin

   Ergebnis: `build/merged-binary.bin`. Das ist das fertige Image für Adresse `0x0`.

Falls `merge-bin` nicht bekannt ist:

    cd build
    python -m esptool --chip esp32s3 merge_bin -o g502-ble-merged.bin "@flash_args"

Die Anführungszeichen um `@flash_args` sind in PowerShell nötig.

Zum normalen Entwickeln reicht `Strg+E F` (Flash über UART), das Image brauchst du nur zum Weitergeben.

### Automatisch über GitHub
Der Workflow `.github/workflows/build.yml` baut bei jedem Push mit ESP-IDF 5.5.6 und legt
die Dateien unter **Actions → Artifacts** ab. Ein Release mit angehängter `g502-ble-merged.bin`
entsteht, sobald du einen Tag pushst:

    git tag v1.1
    git push origin v1.1

Der Workflow baut mit `sdkconfig.defaults`. Einstellungen aus `menuconfig` mit
`idf.py save-defconfig` dorthin übernehmen, sonst fehlen sie im GitHub-Build.

## Hardware
| Teil | Hinweis |
|------|---------|
| ESP32-S3 Super Mini | D+/D− direkt am USB-C-Anschluss anlöten (GPIO19/20 sind nicht herausgeführt) |
| LiPo 1S mit Schutzschaltung | Platz in der Maus beachten |
| Ladeplatine (MCP73831 oder TP4056 mit Schutz) | hängt vor dem Schalter am Akku |
| Schiebeschalter (≥ 1 A) | zwischen Akku+ und Boost-Wandler |
| 5-V-Boost-Wandler (≥ 1 A) | versorgt Maus und ESP (kabelgebundene G502 HERO) |
| 2 × 100 kΩ + 100 nF | Akkumessung an GPIO1 |

### Verkabelung
| Maus-Ader | Anschluss |
|-----------|-----------|
| D+ (grün) | D+ am USB-C-Anschluss des Super Mini (GPIO20) |
| D− (weiß) | D− am USB-C-Anschluss des Super Mini (GPIO19) |
| 5 V (rot) | 5-V-Ausgang des Boost-Wandlers, ebenso 5V-Pin des Super Mini |
| GND | GND |

```
LiPo ── Ladeplatine ── Schalter ── Boost 5 V ──┬── Maus
                                               └── ESP32-S3 (5V-Pin)
Akkumessung: Schalter-Ausgang ── 100k ──┬── 100k ── GND
                                        └── GPIO1 (+ 100 nF nach GND)
```
Bei der G502 Lightspeed liegt VBUS stattdessen an einer Spannung ≥ ca. 3,8 V (siehe Projektstatus).
Ohne Spannungsteiler in `main/main.c` `BATTERY_ADC_GPIO` auf `-1` setzen.
Die Anzeige rechnet mit `BATTERY_DIV_RATIO`. Bei Abweichung per Multimeter nachkalibrieren.

## Technische Hinweise
- Die G502 hat drei HID-Interfaces: Maus (0), Tastatur (1, G-Tasten) und HID++ (2, Konfiguration).
  Die Firmware nutzt die ersten beiden.
- **Kein Boot-Protokoll auf dem Tastatur-Interface setzen.** `set_protocol(BOOT)` schaltet bei der G502
  das ganze Gerät um: Die Maus sendet dann nur noch 4-Byte-Reports ohne Kippen und mit 8 Bit.
  Im Report-Protokoll liefert die Maus 8 Byte (16 Tasten, 16-Bit X/Y, Rad, Pan), die Tastatur 9 Byte
  (Report-ID 1, Modifier, reserviert, 6 Tasten). Die Firmware schneidet die ID der Tastatur ab.
- Das Kippen des Rads kommt nativ als Pan-Wert an und wird direkt weitergeleitet.
- Die Interfaces werden in einem eigenen Task geöffnet. Der HID-Host-Callback darf nicht blockieren,
  sonst laufen Control-Transfers in einen Timeout.
- Verbindungsintervall: Die Firmware bittet den PC um 7,5 bis 10 ms. Unter Windows stellt sich 7,5 ms ein.
  Sendeintervall 8 ms (`SEND_INTERVAL_MS`). Kürzere Werte lassen die Maus hüpfen, weil mehrere Reports
  gebündelt ankommen.

## Hinweise zum Betrieb
- Während der Maus-Betrieb läuft, darf am USB-C-Port des ESP kein PC-Kabel stecken.
  Debugging über den eingebauten USB-JTAG ist ebenfalls nicht möglich.
- Nach Änderungen am HID-Report-Aufbau das Gerät unter Windows entfernen und neu koppeln.
  Hilft das nicht: *ESP-IDF: Erase Flash* und neu flashen.
- Zum Debuggen `DEBUG_REPORTS` in `main/main.c` auf `1` setzen. Dann erscheinen USB-Reports im Log.

## Einschränkungen
- Aufbau in der Maus nicht abgeschlossen, siehe Projektstatus.
- BLE erreicht keine 1000 Hz. Real sind es etwa 110 bis 125 Reports pro Sekunde. Für Büro und Alltag gut,
  für kompetitives Gaming nicht gedacht.
- Die Laufzeit hängt vor allem von Maus-Sensor und Beleuchtung ab. RGB aus spart am meisten.

## Haftung
Der Umbau öffnet die Maus und führt einen LiPo-Akku ein. Garantie erlischt, Nachbau auf eigene
Gefahr. Nur geschützte Akkus verwenden und nichts quetschen oder kurzschließen.