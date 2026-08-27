#ifndef EHCI_H
#define EHCI_H

/*
 * EHCI.h — драйвер EHCI (Enhanced Host Controller Interface, USB 2.0
 * High-Speed, 480 Мбит/с).
 *
 * ehci_pci_scan() — единственная точка входа, вызывается из usb_init()
 * (USB.c) ПЕРЕД uhci_pci_scan() — важен именно такой порядок: во время
 * ehci_pci_scan() порты, на которых обнаружено НЕ high-speed устройство,
 * сразу передаются во владение companion-контроллеру (PORTSC bit
 * Port Owner, см. EHCI.c: ehci_port_reset) — чтобы к моменту, когда
 * uhci_pci_scan() начнёт опрашивать свои порты, эти устройства уже были
 * видны через UHCI. Low/full-speed устройства сам EHCI не обслуживает
 * (split-транзакции не реализованы, см. шапку EHCI.c) — этим
 * непосредственно занимается UHCI.c.
 */

void ehci_pci_scan(void);

#endif /* EHCI_H */
