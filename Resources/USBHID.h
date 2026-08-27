#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>
#include <stdbool.h>

/*
 * USBHID.h — класс-драйвер USB HID (клавиатура/мышь), только Boot
 * Protocol. Подробности реализации и сознательные границы (почему опрос
 * идёт через control-transfer GET_REPORT, а не через interrupt endpoint)
 * — см. шапку USBHID.c.
 */

struct usb_hcd;   /* opaque — полное определение в USB.h */

/* Вызывается из USB.c (usb_enumerate_port) при обнаружении HID-
 * интерфейса с bInterfaceSubClass==1 (Boot Protocol поддержан
 * устройством). protocol — bInterfaceProtocol: 1 = клавиатура,
 * 2 = мышь; любое другое значение игнорируется (unsupported). */
void usb_hid_attach(struct usb_hcd *hcd, uint8_t dev_addr, uint8_t speed,
                    uint8_t maxp0, uint8_t iface_num, uint8_t protocol);

/* Опросить все подключённые HID-устройства (GET_REPORT по каждому).
 * Вызывать периодически — см. хук в Scheduler.c (throttled, не каждый
 * тик, тот же приём, что у ui_flip_userspace_windows в ui_extra.c). */
void usb_hid_poll_all(void);

/* Состояние кнопок мыши, объединённое (OR) по ВСЕМ подключённым USB HID
 * мышам, закэшированное последним usb_hid_poll_all() — те же биты, что у
 * ps2_mouse_buttons() (bit0=LMB, bit1=RMB, bit2=MMB). Дешёвый геттер без
 * I/O, можно звать каждый тик (см. ui_extra.c: ui_tick). */
uint8_t usb_hid_mouse_buttons(void);

/* Неблокирующее чтение одного символа из кольцевого буфера USB-клавиатур
 * (все подключённые клавиатуры пишут в один общий буфер). Возвращает
 * false, если буфер пуст. Символы — уже переведённые через boot-раскладку
 * US QWERTY (см. USBHID.c) с учётом Shift; функциональные/служебные
 * клавиши без ASCII-эквивалента не попадают в буфер. */
bool usb_hid_kbd_read_char(char *out_ch);

#endif /* USBHID_H */
