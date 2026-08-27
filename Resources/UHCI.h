#ifndef UHCI_H
#define UHCI_H

/*
 * UHCI.h — драйвер UHCI (Universal Host Controller Interface), Intel,
 * используется для USB 1.1 портов (low/full speed) и как "companion
 * controller" рядом с EHCI на USB 2.0 чипсетах (см. EHCI.c: неHS-
 * устройства EHCI после порт-рестарта отдаёт именно на UHCI/OHCI).
 *
 * uhci_pci_scan() — единственная точка входа, вызывается из usb_init()
 * (USB.c). Сканирует PCI (class 0x0C, subclass 0x03, prog-if 0x00),
 * поднимает каждый найденный контроллер и регистрирует его в USB-ядре
 * через usb_register_hcd() — дальше с ним работает только USB.c через
 * usb_hcd_ops_t (control_transfer/port_reset), сам UHCI.c больше никуда
 * не вызывается напрямую.
 */

void uhci_pci_scan(void);

#endif /* UHCI_H */
