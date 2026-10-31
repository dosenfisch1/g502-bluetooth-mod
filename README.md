# G502 USB → BLE (ESP32-S3 Super Mini)

Macht aus einer kabelgebundenen Logitech G502 eine Bluetooth-LE-Maus. Der ESP32-S3
sitzt als USB-Host in der Maus und gibt Maus und Tastatur (G-Tasten) per BLE an den PC weiter.

## Funktionen
- Maus: 16 Tasten, 16-Bit-Auflösung, Rad und horizontales Scrollen
- G-Tasten als Tastatur (z. B. Strg+Bild hoch/runter für Tabwechsel)
- Bewegungen werden gesammelt und alle 8 ms gesendet, kurze Klicks gehen nicht verloren
- Akkustand über den BLE-Battery-Service (optional, ADC-Messung)
- On-board-RGB-LED (GPIO48) wird beim Start ausgeschaltet

## Verkabelung
| Maus-Ader   | ESP32-S3 |
|-------------|----------|
| D+ (grün)   | D+ am USB-C-Anschluss (GPIO20) |
| D− (weiß)   | D− am USB-C-Anschluss (GPIO19) |
| 5 V (rot)   | 5 V aus Boost-Wandler (und 5V-Pin des Boards) |
| GND         | GND |

GPIO19/20 sind beim Super Mini nicht herausgeführt, D+/D− werden direkt an den
USB-C-Anschluss gelötet.

Stromversorgung: LiPo → Ladeplatine → Schalter → 5-V-Boost → Maus + ESP.
Akkumessung: Mittelpunkt eines 100k/100k-Teilers (hinter dem Schalter) an GPIO1,
dazu 100 nF nach GND. Ohne Teiler in `main/main.c` `BATTERY_ADC_GPIO` auf `-1` setzen.

## Fertige Firmware flashen
Unter *Actions* bzw. *Releases* liegt `g502-ble-merged.bin`. Sie enthält Bootloader,
Partitionstabelle und App und gehört an Adresse `0x0`.

Flashen geht nur über **UART** (GPIO43 = TX, GPIO44 = RX), weil der USB-C-Port der Maus gehört:

    esptool.py --chip esp32s3 -p COMx -b 460800 write_flash 0x0 g502-ble-merged.bin

Download-Modus: BOOT gedrückt halten, RESET tippen, BOOT loslassen.
Alternativ im Browser mit dem Esptool-JS-Webflasher (Chrome/Edge).

## Selbst bauen (VS Code + ESP-IDF 5.5)
1. Ordner öffnen, `F1` → *ESP-IDF: Set Espressif Device Target* → `esp32s3`
2. `F1` → *ESP-IDF: Select Port to Use* → COM-Port des UART-Adapters
3. Build `Strg+E B`, Flash `Strg+E F`, Monitor `Strg+E M`

Zusammengeführtes Image selbst erzeugen (im ESP-IDF-Terminal):

    cd build
    esptool.py --chip esp32s3 merge_bin -o g502-ble-merged.bin @flash_args

## Hinweise
- Nach Änderungen am HID-Report-Aufbau das Gerät unter Windows entfernen und neu koppeln.
  Hilft das nicht: *ESP-IDF: Erase Flash* und neu flashen.
- Einstellungen aus `menuconfig` mit `idf.py save-defconfig` nach `sdkconfig.defaults`
  übernehmen, sonst fehlen sie im GitHub-Build (`sdkconfig` liegt nicht im Repo).