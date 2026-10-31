# G502 USB -> BLE (ESP32-S3), ESP-IDF 5.5 + VS Code

## Verkabelung (fest vorgegeben durch den ESP32-S3)
| Maus-Ader | ESP32-S3 |
|-----------|----------|
| D+ (gruen)| GPIO20   |
| D- (weiss)| GPIO19   |
| 5 V (rot) | 5 V-Versorgung (Akku -> Boost), NICHT vom ESP-3V3 |
| GND       | GND      |

## Build in VS Code (Espressif IDF Extension)
1. Ordner `g502_ble` in VS Code oeffnen
2. F1 -> "ESP-IDF: Set Espressif Device Target" -> esp32s3
3. F1 -> "ESP-IDF: Select Port to Use" -> Port des UART-Anschlusses waehlen
4. Build: Strg+E B  (esp_hid_gap.* werden automatisch kopiert, usb_host_hid geladen)
5. Flash: Strg+E F   (UART-Port des DevKitC-1, nicht der "USB"-Port)
6. Monitor: Strg+E M

Hinweis: GPIO19/20 gehoeren dem USB-Host. Waehrend der Maus-Test laeuft, darf am
nativen USB-Port des DevKits kein Kabel zum PC stecken. Debugging ueber den
eingebauten USB-JTAG ist ebenfalls nicht moeglich.
