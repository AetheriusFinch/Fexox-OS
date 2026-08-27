/*
 * USBHID.c — класс-драйвер USB HID, только Boot Protocol
 * (клавиатура/мышь, USB HID §4.2 — bInterfaceSubClass==1).
 *
 * Почему опрос через control-transfer (GET_REPORT), а не через
 * interrupt endpoint: настоящий interrupt-пайп требует Periodic Schedule
 * (Frame List записи у UHCI, Periodic List у EHCI) — этого пока нет ни в
 * UHCI.c, ни в EHCI.c (оба сейчас умеют только Async Schedule — control/
 * bulk). HID class spec явно разрешает получать отчёт через EP0 класс-
 * запросом GET_REPORT (аналог того, что обычно приходит по interrupt IN)
 * — этого достаточно для клавиатуры/мыши без добавления периодического
 * расписания в оба HCD. Плата за это — сам драйвер должен АКТИВНО
 * опрашивать устройство (usb_hid_poll_all, вызывается по таймеру, см.
 * хук в Scheduler.c), а не получать данные проактивно по прерыванию;
 * практическая задержка — интервал опроса (по умолчанию раз в
 * несколько тиков, см. Scheduler.c), для клавиатуры/мыши это незаметно.
 *
 * Сознательные границы этой версии:
 *   - Только Boot Protocol (фиксированный формат отчёта, USB HID §B.1),
 *     явно включается через SET_PROTOCOL при подключении. Report
 *     Protocol с разбором произвольного HID Report Descriptor не
 *     реализован — устройства, которые НЕ поддерживают Boot Protocol
 *     (редкость среди клавиатур/мышей, но встречается у экзотических
 *     устройств), этот драйвер не подхватит.
 *   - Клавиатура: без auto-repeat (символ шлётся один раз на нажатие,
 *     не на удержание) и без Num Lock/Caps Lock LED-обратной связи.
 *   - Раскладка — US QWERTY, без поддержки других раскладок/Alt Gr.
 */
#include "USBHID.h"
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

/* ui_extra.h (курсор) — те же функции, что использует PS2-мышь, поэтому
 * USB-мышь двигает ТОТ ЖЕ курсор без отдельного механизма отображения. */
extern void    ui_set_mouse_pos(int32_t x, int32_t y);
extern void    ui_get_mouse_pos(int32_t *out_x, int32_t *out_y);
extern uint32_t fb_width(void);
extern uint32_t fb_height(void);

/* =========================================================================
 * Устройства
 * ========================================================================= */
#define USB_HID_MAX_DEVICES 8
#define USB_HID_PROTO_KEYBOARD 1
#define USB_HID_PROTO_MOUSE    2

typedef struct {
    bool       used;
    usb_hcd_t *hcd;
    uint8_t    addr, speed, maxp0, iface_num, protocol;
    uint8_t    last_report[8];
    uint8_t    last_buttons;   /* только для мышей — см. usb_hid_mouse_buttons() */
    uint8_t    fail_count;     /* подряд неудачных GET_REPORT — см. usb_hid_poll_all */
} usb_hid_dev_t;

/* После стольких подряд неудачных опросов (таймаут/ошибка в
 * control_transfer — см. таймаут в UHCI.c/EHCI.c) считаем устройство
 * отключённым/зависшим и перестаём его опрашивать вообще. Без этого
 * "фантомное" неотвечающее устройство продолжало бы стоить до 50мс
 * блокировки всей системы КАЖДЫЙ опрос (раз в 3 тика, см. хук в
 * Scheduler.c) — уже само по себе не критично разово, но накапливается
 * в постоянный лаг, если устройство не отвечает НИКОГДА. */
#define USB_HID_MAX_CONSECUTIVE_FAILS 5

static usb_hid_dev_t g_hid[USB_HID_MAX_DEVICES];
static int           g_hid_count = 0;

/* =========================================================================
 * Кольцевой буфер клавиатурного ввода
 * ========================================================================= */
#define KBD_QUEUE_SIZE 32
static char g_kbd_queue[KBD_QUEUE_SIZE];
static int  g_kbd_head = 0, g_kbd_tail = 0;

static void kbd_queue_push(char c) {
    int next = (g_kbd_tail + 1) % KBD_QUEUE_SIZE;
    if (next == g_kbd_head) return;   /* буфер полон — теряем символ, не блокируемся */
    g_kbd_queue[g_kbd_tail] = c;
    g_kbd_tail = next;
}

bool usb_hid_kbd_read_char(char *out_ch) {
    if (g_kbd_head == g_kbd_tail) return false;
    if (out_ch) *out_ch = g_kbd_queue[g_kbd_head];
    g_kbd_head = (g_kbd_head + 1) % KBD_QUEUE_SIZE;
    return true;
}

/* =========================================================================
 * HID Usage ID (Usage Page 0x07, Keyboard) -> ASCII, Boot Protocol,
 * US QWERTY. См. USB HID Usage Tables, таблицу 12 "Keyboard/Keypad Page".
 * ========================================================================= */
static char hid_keycode_to_char(uint8_t code, bool shift) {
    if (code >= 0x04 && code <= 0x1D) {          /* a..z */
        char c = (char)('a' + (code - 0x04));
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    if (code >= 0x1E && code <= 0x27) {          /* 1..9,0 */
        static const char digits[]    = "1234567890";
        static const char digits_sh[] = "!@#$%^&*()";
        int idx = code - 0x1E;
        return shift ? digits_sh[idx] : digits[idx];
    }
    switch (code) {
        case 0x28: return '\n';               /* Enter        */
        case 0x2A: return '\b';               /* Backspace    */
        case 0x2B: return '\t';               /* Tab          */
        case 0x2C: return ' ';                /* Space        */
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default:   return 0;   /* Ctrl/Alt/F1-F12/стрелки/... — без ASCII-эквивалента */
    }
}

/* =========================================================================
 * attach
 * ========================================================================= */
void usb_hid_attach(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                    uint8_t iface_num, uint8_t protocol) {
    if (protocol != USB_HID_PROTO_KEYBOARD && protocol != USB_HID_PROTO_MOUSE) {
        DBG_VAL("HID", "неизвестный protocol, игнорируем, addr", addr);
        return;
    }
    if (g_hid_count >= USB_HID_MAX_DEVICES) {
        DBG_MSG("HID", "лимит устройств исчерпан (USB_HID_MAX_DEVICES)");
        return;
    }

    /* SET_PROTOCOL(Boot) — класс-запрос, best effort: некоторые простые
     * устройства не поддерживают Report Protocol вообще и всегда живут в
     * Boot Protocol — STALL на этот запрос от них ожидаем и не фатален. */
    usb_setup_pkt_t sp;
    sp.bmRequestType = USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE;
    sp.bRequest = 0x0B;   /* SET_PROTOCOL */
    sp.wValue   = 0;      /* 0 = Boot Protocol */
    sp.wIndex   = iface_num;
    sp.wLength  = 0;
    hcd->ops->control_transfer(hcd, addr, speed, maxp0, &sp, NULL, 0);

    usb_hid_dev_t *d = &g_hid[g_hid_count++];
    memset(d, 0, sizeof(*d));
    d->used      = true;
    d->hcd       = hcd;
    d->addr      = addr;
    d->speed     = speed;
    d->maxp0     = maxp0;
    d->iface_num = iface_num;
    d->protocol  = protocol;

    if (protocol == USB_HID_PROTO_KEYBOARD)
        DBG_VAL("HID", "клавиатура подключена, addr", addr);
    else
        DBG_VAL("HID", "мышь подключена, addr", addr);
}

/* =========================================================================
 * Разбор отчётов
 * ========================================================================= */
static void usb_hid_handle_mouse_report(usb_hid_dev_t *d, const uint8_t report[8]) {
    uint8_t buttons = report[0] & 0x07u;
    int8_t  dx = (int8_t)report[1];
    int8_t  dy = (int8_t)report[2];

    d->last_buttons = buttons;

    if (dx != 0 || dy != 0) {
        int32_t x, y;
        ui_get_mouse_pos(&x, &y);
        x += dx;
        y += dy;
        int32_t maxw = (int32_t)fb_width()  - 1;
        int32_t maxh = (int32_t)fb_height() - 1;
        if (x < 0) x = 0; if (y < 0) y = 0;
        if (x > maxw) x = maxw; if (y > maxh) y = maxh;
        ui_set_mouse_pos(x, y);
    }
}

static void usb_hid_handle_kbd_report(usb_hid_dev_t *d, const uint8_t report[8]) {
    uint8_t modifier = report[0];
    bool    shift    = (modifier & 0x22u) != 0;   /* bit1=LShift, bit5=RShift */

    /* Новые нажатия: код есть в report[2..7], но НЕ было в last_report[2..7].
     * 0 = нет клавиши, 1 = rollover error, 2 = POST fail, 3 = undefined —
     * все три служебных значения пропускаем. */
    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code < 4) continue;

        bool was_pressed = false;
        for (int j = 2; j < 8; j++) {
            if (d->last_report[j] == code) { was_pressed = true; break; }
        }
        if (was_pressed) continue;

        char ch = hid_keycode_to_char(code, shift);
        if (ch != 0) kbd_queue_push(ch);
    }

    memcpy(d->last_report, report, 8);
}

/* =========================================================================
 * usb_hid_poll_all
 * ========================================================================= */
void usb_hid_poll_all(void) {
    for (int i = 0; i < g_hid_count; i++) {
        usb_hid_dev_t *d = &g_hid[i];
        if (!d->used) continue;

        uint8_t  report[8];
        memset(report, 0, sizeof(report));
        uint16_t rlen = (d->protocol == USB_HID_PROTO_KEYBOARD) ? 8 : 4;

        usb_setup_pkt_t sp;
        sp.bmRequestType = USB_REQ_DIR_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE;
        sp.bRequest = 0x01;              /* GET_REPORT */
        sp.wValue   = (1u << 8) | 0u;    /* Input report, Report ID 0 */
        sp.wIndex   = d->iface_num;
        sp.wLength  = rlen;

        if (d->hcd->ops->control_transfer(d->hcd, d->addr, d->speed, d->maxp0,
                                          &sp, report, rlen) != 0) {
            d->fail_count++;
            if (d->fail_count >= USB_HID_MAX_CONSECUTIVE_FAILS) {
                DBG_VAL("HID", "устройство не отвечает, перестаём опрашивать, addr", d->addr);
                d->used = false;   /* больше не трогаем — см. проверку "if (!d->used) continue" выше */
            }
            continue;   /* временная ошибка / устройство отключено — пропускаем этот тик */
        }
        d->fail_count = 0;

        if (d->protocol == USB_HID_PROTO_MOUSE)
            usb_hid_handle_mouse_report(d, report);
        else
            usb_hid_handle_kbd_report(d, report);
    }
}

uint8_t usb_hid_mouse_buttons(void) {
    uint8_t b = 0;
    for (int i = 0; i < g_hid_count; i++) {
        if (g_hid[i].used && g_hid[i].protocol == USB_HID_PROTO_MOUSE)
            b |= g_hid[i].last_buttons;
    }
    return b;
}
