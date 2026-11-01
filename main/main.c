/*
 * USB-Maus (Logitech G502) -> BLE-HID-Maus Bridge auf ESP32-S3
 *
 * - ESP32-S3 arbeitet als USB-Host ueber den internen PHY
 *   (fest verdrahtet: GPIO19 = D-, GPIO20 = D+, nicht aenderbar)
 * - Maus-Interface der G502 liefert (laut Log) 4-Byte-Reports:
 *   Buttons(1 Byte), X, Y, Wheel (je int8). Kein Pan-Feld -> Tilt kommt anders an.
 * - Bewegungen werden gesammelt und alle 8 ms als ein BLE-Report gesendet
 *   (BLE schafft nicht 1000 Hz, daher Aufsummieren statt Verwerfen)
 *
 * esp_hid_gap.c/.h werden beim ersten CMake-Lauf automatisch aus
 * $IDF_PATH/examples/bluetooth/esp_hid_device/main/ nach main/ kopiert.
 * usb_host_hid wird ueber idf_component.yml automatisch geladen.
 */

#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "led_strip.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"

#include "esp_hidd.h"
#include "esp_hid_gap.h"
#include "esp_gatts_api.h"

static const char *TAG = "G502_BLE";

#define SEND_INTERVAL_MS 8
#define BLE_REPORT_ID    1
#define KBD_REPORT_ID    2

/* Diagnose: loggt USB-Reports bei Tastenwechsel oder Pan-Wert (nicht bei reiner Bewegung).
 * Nach der Fehlersuche auf 0 setzen. */
#define DEBUG_REPORTS    1

/* Mausrad-Kippen (Tilt) als Tasten -> horizontales Scrollen.
 * Bitmasken der Tasten aus dem Log eintragen (Button n = 1 << (n-1)).
 * 0 = aus. Beispiel: Button 14 = 0x2000, Button 15 = 0x4000 */
#define TILT_LEFT_MASK   0x0000
#define TILT_RIGHT_MASK  0x0000

/* Nur noetig, falls das Kippen NICHT nativ als Pan ankommt (bei der G502 kommt es an -> 0).
 * Sonst: Im G HUB Tilt links/rechts auf diese Tasten legen; die Firmware macht daraus
 * horizontales Scrollen. (Scroll Lock = 0x47, Pause = 0x48, F13 = 0x68, F14 = 0x69) */
#define TILT_LEFT_KEY    0
#define TILT_RIGHT_KEY   0
#define TILT_REPEAT_MS   100    // Wiederholrate, solange gekippt gehalten wird

/* Experiment: Maus-Interface ausdruecklich auf Report-Protokoll stellen (evtl. laengere Reports).
 * Bei Problemen mit der Mausbewegung wieder auf 0. */
#define MOUSE_FORCE_REPORT_PROTOCOL 1

/* ACHTUNG: set_protocol(BOOT) auf dem Tastatur-Interface schaltet bei der G502 offenbar das
 * ganze Geraet (auch die Maus) in den Boot-Modus -> Maus nur noch 4 Byte, kein Pan/Tilt.
 * Deshalb aus lassen (0). */
#define KBD_FORCE_BOOT_PROTOCOL 0

/* Akkustand ueber ADC (Spannungsteiler 100k/100k von Akku+ nach GND,
 * Mittelpunkt an diesen Pin + 100 nF nach GND). -1 = Akkuanzeige aus. */
/* Optional: GPIO, das den VBUS-Trenner der Maus (z.B. Ideal-Diode mit Enable-Pin) einschaltet,
 * sobald der ESP laeuft. Dann schaltet die Maus in den USB-Modus. Bei ausgeschaltetem ESP bleibt
 * der Trenner offen und die Maus laesst sich normal per USB betreiben. -1 = nicht benutzt. */
#define MOUSE_VBUS_EN_GPIO  -1       // z.B. 12

#define STATUS_LED_GPIO    48        // On-board WS2812 des Super Mini; -1 = nicht anfassen
#define BATTERY_ADC_GPIO   1
#define BATTERY_DIV_RATIO  2.0f      // (R1+R2)/R2, bei Abweichung per Multimeter nachkalibrieren

/* ---------- BLE HID Report Map (identisch zum G502-Layout) ---------- */
static const uint8_t s_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, BLE_REPORT_ID,
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01, 0x29, 0x10,   // Button 1..16
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x10, 0x75, 0x01,
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x16, 0x01, 0x80,  //     Logical Min -32767
    0x26, 0xFF, 0x7F,  //     Logical Max  32767
    0x75, 0x10, 0x95, 0x02,
    0x09, 0x30, 0x09, 0x31,   // X, Y
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x01,
    0x09, 0x38,        //     Wheel
    0x81, 0x06,
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     AC Pan (horizontales Scrollen)
    0x95, 0x01,
    0x81, 0x06,
    0xC0,              //   End Collection
    0xC0,              // End Collection

    /* Tastatur (Boot-Layout: Modifier, reserviert, 6 Tasten) */
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KBD_REPORT_ID,
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0, 0x29, 0xE7,   // Modifier (Strg, Shift, Alt, GUI)
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01, 0x75, 0x08,   // reserviertes Byte
    0x81, 0x01,
    0x95, 0x06, 0x75, 0x08,   // 6 Tastencodes
    0x15, 0x00, 0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00, 0x29, 0x65,
    0x81, 0x00,
    0xC0               // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    { .data = s_report_map, .len = sizeof(s_report_map) }
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id         = 0x16C0,
    .product_id        = 0x05DF,
    .version           = 0x0100,
    .device_name       = "G502 BLE",
    .manufacturer_name = "DIY",
    .serial_number     = "0001",
    .report_maps       = s_report_maps,
    .report_maps_len   = 1
};

static esp_hidd_dev_t *s_hid_dev = NULL;
static volatile bool   s_ble_connected = false;
static SemaphoreHandle_t s_send_mutex;

static esp_err_t ble_send(uint8_t report_id, uint8_t *data, size_t len)
{
    if (!s_ble_connected || !s_hid_dev) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    esp_err_t r = esp_hidd_dev_input_set(s_hid_dev, 0, report_id, data, len);
    xSemaphoreGive(s_send_mutex);
    return r;
}

/* ---------- Maus-Zustand (Akkumulator) ---------- */
typedef struct {
    uint16_t buttons;     // aktueller Zustand
    uint16_t latched;     // Tasten, die seit dem letzten Senden gedrueckt waren
    uint16_t last_sent;
    int32_t  dx, dy, wheel, pan;
} mouse_state_t;

static mouse_state_t s_m;
static portMUX_TYPE  s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int  s_tilt_dir = 0;   // -1 links, +1 rechts, 0 aus
static volatile int  s_tilt_ms  = 0;

static void on_mouse_report(const uint8_t *d, size_t len)
{
    uint16_t b;
    int32_t x, y, w = 0, p = 0;

    if (len >= 8) {                         // G502: 16 Buttons, 16-Bit X/Y
        b = d[0] | (d[1] << 8);
        x = (int16_t)(d[2] | (d[3] << 8));
        y = (int16_t)(d[4] | (d[5] << 8));
        w = (int8_t)d[6];
        p = (int8_t)d[7];
    } else if (len >= 3) {                  // Boot-Protokoll Fallback
        b = d[0];
        x = (int8_t)d[1];
        y = (int8_t)d[2];
        if (len >= 4) w = (int8_t)d[3];
    } else {
        return;
    }

    uint16_t raw = b;

#if DEBUG_REPORTS
    {
        static uint16_t last_b;
        static int first = 10;                 // die ersten Reports immer zeigen (Format erkennen)
        if (first > 0 || raw != last_b || p != 0) {
            if (first > 0) first--;
            ESP_LOGI(TAG, "USB-Report len=%u buttons=0x%04X wheel=%d pan=%d",
                     (unsigned)len, raw, (int)w, (int)p);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, d, len, ESP_LOG_INFO);
            last_b = raw;
        }
    }
#endif

#if TILT_LEFT_MASK || TILT_RIGHT_MASK
    {
        static uint16_t prev_raw;
        uint16_t pressed = raw & ~prev_raw;          // nur steigende Flanken
        if (pressed & TILT_LEFT_MASK)  p -= 1;       // links
        if (pressed & TILT_RIGHT_MASK) p += 1;       // rechts
        prev_raw = raw;
        b &= ~(TILT_LEFT_MASK | TILT_RIGHT_MASK);    // Tilt nicht zusaetzlich als Taste senden
    }
#endif

    portENTER_CRITICAL(&s_lock);
    s_m.buttons  = b;
    s_m.latched |= b;
    s_m.dx += x; s_m.dy += y; s_m.wheel += w; s_m.pan += p;
    portEXIT_CRITICAL(&s_lock);
}

/* Tastatur-Reports (z.B. G7/G8 -> Strg+Hoch/Runter) durchreichen.
 * Tilt-Tasten (F13/F14) werden abgefangen und in horizontales Scrollen umgewandelt. */
static void on_kbd_report(const uint8_t *d, size_t len)
{
#if DEBUG_REPORTS
    ESP_LOGI("KBD", "Tastatur-Report len=%u", (unsigned)len);
    ESP_LOG_BUFFER_HEX_LEVEL("KBD", d, len, ESP_LOG_INFO);
#endif
    // Report-Protokoll: 9 Byte = Report-ID (01) + Modifier, reserviert, 6 Tasten.
    // Boot-Format hat 8 Byte ohne ID. Andere Laengen/IDs ignorieren.
    const uint8_t *k;
    if (len == 9 && d[0] == 0x01)  k = d + 1;
    else if (len == 8)             k = d;
    else return;
    uint8_t rep[8];
    memcpy(rep, k, 8);

#if TILT_LEFT_KEY || TILT_RIGHT_KEY
    int dir = 0;
    for (int i = 2; i < 8; i++) {
        if (TILT_LEFT_KEY  && rep[i] == TILT_LEFT_KEY)  { dir = -1; rep[i] = 0; }
        if (TILT_RIGHT_KEY && rep[i] == TILT_RIGHT_KEY) { dir =  1; rep[i] = 0; }
    }
    if (dir != 0 && dir != s_tilt_dir) {         // neuer Tastendruck -> sofort ein Impuls
        portENTER_CRITICAL(&s_lock);
        s_m.pan += dir;
        portEXIT_CRITICAL(&s_lock);
        s_tilt_ms = 0;
    }
    s_tilt_dir = dir;
#endif
    ble_send(KBD_REPORT_ID, rep, sizeof(rep));
}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void ble_sender_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SEND_INTERVAL_MS));
        if (!s_ble_connected || !s_hid_dev) {
            portENTER_CRITICAL(&s_lock);
            s_m.dx = s_m.dy = s_m.wheel = s_m.pan = 0;   // nichts aufstauen
            s_m.latched = s_m.buttons;
            portEXIT_CRITICAL(&s_lock);
            continue;
        }

#if TILT_LEFT_KEY || TILT_RIGHT_KEY
        if (s_tilt_dir != 0) {
            s_tilt_ms += SEND_INTERVAL_MS;
            if (s_tilt_ms >= TILT_REPEAT_MS) {
                s_tilt_ms = 0;
                portENTER_CRITICAL(&s_lock);
                s_m.pan += s_tilt_dir;
                portEXIT_CRITICAL(&s_lock);
            }
        }
#endif

        int32_t dx, dy, w, p;
        uint16_t btn, prev_sent = 0;
        bool dirty;

        portENTER_CRITICAL(&s_lock);
        btn = s_m.buttons | s_m.latched;   // kurze Klicks nicht verlieren
        dx  = clamp(s_m.dx,    -32767, 32767);
        dy  = clamp(s_m.dy,    -32767, 32767);
        w   = clamp(s_m.wheel, -127, 127);
        p   = clamp(s_m.pan,   -127, 127);
        dirty = dx || dy || w || p || (btn != s_m.last_sent);
        if (dirty) {
            s_m.dx -= dx; s_m.dy -= dy; s_m.wheel -= w; s_m.pan -= p;
            s_m.latched   = 0;
            prev_sent = s_m.last_sent;
            s_m.last_sent = btn;
        }
        portEXIT_CRITICAL(&s_lock);

        if (!dirty) continue;

        uint8_t rep[8] = {
            btn & 0xFF, btn >> 8,
            (uint8_t)(dx & 0xFF), (uint8_t)((dx >> 8) & 0xFF),
            (uint8_t)(dy & 0xFF), (uint8_t)((dy >> 8) & 0xFF),
            (uint8_t)(int8_t)w, (uint8_t)(int8_t)p
        };
        if (ble_send(BLE_REPORT_ID, rep, sizeof(rep)) != ESP_OK) {
            // BLE-Stack gerade ausgelastet: Bewegung nicht verwerfen, naechstes Mal mitsenden
            portENTER_CRITICAL(&s_lock);
            s_m.dx += dx; s_m.dy += dy; s_m.wheel += w; s_m.pan += p;
            s_m.latched |= btn;
            s_m.last_sent = prev_sent;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

/* ---------- Akkustand (Battery Service) ---------- */
static volatile bool s_batt_refresh = true;

#if BATTERY_ADC_GPIO >= 0
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static adc_channel_t             s_chan;

static void batt_init(void)
{
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &unit, &s_chan));
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc));
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &ccfg));

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = unit, .chan = s_chan,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "ADC-Kalibrierung nicht verfuegbar, nutze Naeherung");
    }
}

static int batt_read_mv(void)
{
    int sum = 0, n = 0, raw;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(s_adc, s_chan, &raw) == ESP_OK) { sum += raw; n++; }
    }
    if (!n) return -1;
    raw = sum / n;
    int mv;
    if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv);
    else        mv = raw * 3100 / 4095;
    return (int)(mv * BATTERY_DIV_RATIO);
}

static int mv_to_percent(int mv)
{
    // Grobe LiPo-Entladekurve (unter Last etwas pessimistisch)
    static const struct { int mv; int pct; } t[] = {
        {4200,100},{4100,90},{4000,80},{3900,65},{3850,55},{3800,40},
        {3750,30},{3700,18},{3650,10},{3600,6},{3500,2},{3300,0}
    };
    const size_t n = sizeof(t) / sizeof(t[0]);
    if (mv >= t[0].mv) return 100;
    for (size_t i = 1; i < n; i++) {
        if (mv >= t[i].mv) {
            return t[i].pct + (mv - t[i].mv) * (t[i-1].pct - t[i].pct) / (t[i-1].mv - t[i].mv);
        }
    }
    return 0;
}

static void battery_task(void *arg)
{
    float filt = 0;
    int tick = 0;
    while (1) {
        int mv = batt_read_mv();
        if (mv > 2500 && mv < 4500) {                 // nur plausible Werte
            filt = (filt == 0) ? (float)mv : filt * 0.8f + mv * 0.2f;
        }
        // alle 30 s oder direkt nach dem Verbinden an den PC melden
        if (filt > 0 && (tick % 6 == 0 || s_batt_refresh)) {
            int pct = mv_to_percent((int)filt);
            if (s_hid_dev) esp_hidd_dev_battery_set(s_hid_dev, (uint8_t)pct);
            ESP_LOGI(TAG, "Akku: %d mV -> %d %%", (int)filt, pct);
            s_batt_refresh = false;
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif

/* ---------- BLE Events ---------- */
static void ble_hidd_event_cb(void *args, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "BLE HID gestartet, Advertising...");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "BLE verbunden");
        s_ble_connected = true;
        s_batt_refresh = true;
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "BLE getrennt");
        s_ble_connected = false;
        esp_hid_ble_gap_adv_start();
        break;
    default:
        break;
    }
}

/* ---------- USB Host / HID ---------- */
static void hid_interface_cb(hid_host_device_handle_t dev,
                             const hid_host_interface_event_t event, void *arg)
{
    uint8_t data[64];
    size_t  len = 0;
    uintptr_t proto = (uintptr_t)arg;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &len) == ESP_OK) {
            if (proto == HID_PROTOCOL_KEYBOARD) on_kbd_report(data, len);
            else                                on_mouse_report(data, len);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "USB-Maus getrennt");
        hid_host_device_close(dev);
        if (proto == HID_PROTOCOL_KEYBOARD) {
            s_tilt_dir = 0;
            uint8_t rel[8] = {0};
            ble_send(KBD_REPORT_ID, rel, sizeof(rel));   // alle Tasten loslassen
        } else {
            portENTER_CRITICAL(&s_lock);
            memset(&s_m, 0, sizeof(s_m));
            portEXIT_CRITICAL(&s_lock);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "USB Transfer-Fehler");
        break;
    default:
        break;
    }
}

static QueueHandle_t s_hid_connect_q;

/* Dieser Callback laeuft im HID-Host-Task. Control-Transfers (set_protocol usw.)
 * brauchen genau diesen Task zum Abschluss -> hier NICHT blockieren, nur weiterreichen. */
static void hid_device_cb(hid_host_device_handle_t dev,
                          const hid_host_driver_event_t event, void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        xQueueSend(s_hid_connect_q, &dev, 0);
    }
}

static void hid_setup_task(void *arg)
{
    hid_host_device_handle_t dev;
    while (xQueueReceive(s_hid_connect_q, &dev, portMAX_DELAY)) {
        hid_host_dev_params_t p;
        if (hid_host_device_get_params(dev, &p) != ESP_OK) continue;
        ESP_LOGI(TAG, "HID-Interface %d: subclass=%d proto=%d", p.iface_num, p.sub_class, p.proto);

        // Maus (Interface 0) und Tastatur (Interface 1, G-Tasten) nutzen,
        // das HID++-Interface (Konfiguration) ignorieren.
        if (p.proto != HID_PROTOCOL_MOUSE && p.proto != HID_PROTOCOL_KEYBOARD) continue;

        const hid_host_device_config_t cfg = {
            .callback = hid_interface_cb,
            .callback_arg = (void *)(uintptr_t)p.proto
        };
        if (hid_host_device_open(dev, &cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Interface %d: open fehlgeschlagen", p.iface_num);
            continue;
        }

#if KBD_FORCE_BOOT_PROTOCOL
        if (p.proto == HID_PROTOCOL_KEYBOARD) {
            if (hid_class_request_set_protocol(dev, HID_REPORT_PROTOCOL_BOOT) != ESP_OK)
                ESP_LOGW(TAG, "set_protocol(BOOT) fehlgeschlagen");
            if (hid_class_request_set_idle(dev, 0, 0) != ESP_OK)
                ESP_LOGW(TAG, "set_idle fehlgeschlagen (unkritisch)");
        }
#endif
#if MOUSE_FORCE_REPORT_PROTOCOL
        if (p.proto == HID_PROTOCOL_MOUSE) {
            if (hid_class_request_set_protocol(dev, HID_REPORT_PROTOCOL_REPORT) != ESP_OK)
                ESP_LOGW(TAG, "set_protocol(REPORT) fehlgeschlagen");
        }
#endif

        if (hid_host_device_start(dev) != ESP_OK) {
            ESP_LOGW(TAG, "Interface %d: start fehlgeschlagen", p.iface_num);
            continue;
        }
        ESP_LOGI(TAG, "%s-Interface gestartet",
                 p.proto == HID_PROTOCOL_KEYBOARD ? "Tastatur" : "Maus");
    }
}

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_cfg));
    xTaskNotifyGive((TaskHandle_t)arg);

    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

#if STATUS_LED_GPIO >= 0
static led_strip_handle_t s_led;   // Handle behalten, damit die Datenleitung nicht floatet

static void led_off(void)
{
    led_strip_config_t strip_cfg = { .strip_gpio_num = STATUS_LED_GPIO, .max_leds = 1 };
    led_strip_rmt_config_t rmt_cfg = { .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led) == ESP_OK) {
        led_strip_clear(s_led);
    }
}
#endif

void app_main(void)
{
#if STATUS_LED_GPIO >= 0
    led_off();
#endif
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* BLE-HID */
    ESP_ERROR_CHECK(esp_hid_gap_init(HIDD_BLE_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE, s_hid_config.device_name));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE,
                                      ble_hidd_event_cb, &s_hid_dev));

    s_send_mutex = xSemaphoreCreateMutex();
    xTaskCreate(ble_sender_task, "ble_send", 4096, NULL, 6, NULL);

#if BATTERY_ADC_GPIO >= 0
    batt_init();
    xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL);
#endif

    /* USB-Host */
    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096,
                            xTaskGetCurrentTaskHandle(), 2, NULL, 1);   // Core 1
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    s_hid_connect_q = xQueueCreate(8, sizeof(hid_host_device_handle_t));
    xTaskCreate(hid_setup_task, "hid_setup", 4096, NULL, 4, NULL);

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 1,
        .callback = hid_device_cb,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_cfg));

#if MOUSE_VBUS_EN_GPIO >= 0
    // Erst jetzt VBUS freigeben: der USB-Host-Stack ist bereit, die Maus kann sich melden
    gpio_config_t vbus_io = {
        .pin_bit_mask = 1ULL << MOUSE_VBUS_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&vbus_io));
    ESP_ERROR_CHECK(gpio_set_level(MOUSE_VBUS_EN_GPIO, 1));
    ESP_LOGI(TAG, "VBUS fuer Maus eingeschaltet (GPIO %d)", MOUSE_VBUS_EN_GPIO);
#endif

    ESP_LOGI(TAG, "Bereit: Maus an USB anstecken, am PC nach \"%s\" koppeln", s_hid_config.device_name);
}