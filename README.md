# G502 Bluetooth Mod

[![Build](https://github.com/dosenfisch1/g502-bluetooth-mod/actions/workflows/build.yml/badge.svg)](https://github.com/dosenfisch1/g502-bluetooth-mod/actions/workflows/build.yml)

Macht aus einer kabelgebundenen **Logitech G502** eine Bluetooth-LE-Maus, ohne Dongle.
Ein ESP32-S3 Super Mini sitzt als USB-Host in der Maus und gibt Maus und Tastatur (G-Tasten)
per BLE an den PC weiter. Ein Schalter schaltet das Ganze ein und aus.

## Funktionen
- Maus mit 16 Tasten, 16-Bit-Auflösung, Rad und horizontalem Scrollen
- G-Tasten als Tastatur, z. B. Strg+Bild hoch/runter für Tabwechsel
  (Belegung muss im internen Speicher der Maus liegen, nicht nur im G-HUB-Profil)
- Bewegungen werden gesammelt und alle 8 ms gesendet, kurze Klicks gehen nicht verloren
- Akkustand über den BLE-Battery-Service (optional, ADC-Messung)
- On-board-RGB-LED (GPIO48) wird beim Start ausgeschaltet

## Fertige Firmware
Unter [Releases](https://github.com/dosenfisch1/g502-bluetooth-mod/releases) liegt `g502-ble-merged.bin`.
Sie enthält Bootloader, Partitionstabelle und App und wird an Adresse `0x0` geflasht.

Geflasht wird über **UART** (GPIO43 = TX, GPIO44 = RX), weil der USB-C-Port der Maus gehört:

    python -m esptool --chip esp32s3 -p COMx -b 460800 write_flash 0x0 g502-ble-merged.bin

Download-Modus: BOOT gedrückt halten, RESET tippen, BOOT loslassen.

## Hardware
| Teil | Hinweis |
|------|---------|
| ESP32-S3 Super Mini | D+/D− direkt am USB-C-Anschluss anlöten (GPIO19/20 sind nicht herausgeführt) |
| LiPo 1S mit Schutzschaltung | Platz in der Maus beachten |
| Ladeplatine (MCP73831 oder TP4056 mit Schutz) | hängt vor dem Schalter am Akku |
| Schiebeschalter (≥ 1 A) | zwischen Akku+ und Boost-Wandler |
| 5-V-Boost-Wandler (≥ 1 A) | versorgt Maus und ESP |
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
Ohne Spannungsteiler in `main/main.c` `BATTERY_ADC_GPIO` auf `-1` setzen.

## Selbst bauen (VS Code + ESP-IDF 5.5)
1. Ordner öffnen, `F1` → *ESP-IDF: Set Espressif Device Target* → `esp32s3`
2. `F1` → *ESP-IDF: Select Port to Use* → COM-Port des UART-Adapters
3. Build `Strg+E B`, Flash `Strg+E F`, Monitor `Strg+E M`

`esp_hid_gap.c/.h` werden beim ersten Build aus dem ESP-IDF-Beispiel kopiert,
`usb_host_hid` und `led_strip` lädt der Komponentenmanager.

Zusammengeführtes Image selbst erzeugen (im ESP-IDF-Terminal):

    idf.py merge-bin

## Hinweise
- Während der Maus-Betrieb läuft, darf am USB-C-Port des ESP kein PC-Kabel stecken.
  Debugging über den eingebauten USB-JTAG ist ebenfalls nicht möglich.
- Nach Änderungen am HID-Report-Aufbau das Gerät unter Windows entfernen und neu koppeln.
  Hilft das nicht: *ESP-IDF: Erase Flash* und neu flashen.
- Einstellungen aus `menuconfig` mit `idf.py save-defconfig` nach `sdkconfig.defaults`
  übernehmen, sonst fehlen sie im GitHub-Build (`sdkconfig` liegt nicht im Repo).

## Einschränkungen
- BLE erreicht keine 1000 Hz, der PC bestimmt das Verbindungsintervall (meist 7,5 bis 15 ms).
  Für Büro und Alltag gut, für kompetitives Gaming nicht gedacht.
- Die Laufzeit hängt vor allem von Maus-Sensor und Beleuchtung ab. RGB aus spart am meisten.

## Haftung
Der Umbau öffnet die Maus und führt einen LiPo-Akku ein. Garantie erlischt, Nachbau auf eigene
Gefahr. Nur geschützte Akkus verwenden und nichts quetschen oder kurzschließen.