/* Fexox OS Project, EULA GPLv2 */
/* On-screen diagnostics: every DBG_MSG / DBG_VAL of the including file is ALSO kept in a
   ring buffer, so the boot screen can show what really happened (no guessing). */
#ifndef USBDIAG_H
#define USBDIAG_H
#include <stdint.h>

#define USBDIAG_VIEW_USB     1u    /* XHCI / USB / EHCI / UHCI / HID */
#define USBDIAG_VIEW_STORAGE 2u    /* USBMSC / VFS / FAT / BOOT / AHCI / NVMe */

void     usbdiag_log(const char *tag, const char *msg, uint64_t val, int has_val);
uint32_t usbdiag_total(void);
/* prints the last `max_lines` entries of the given view(s), oldest first */
void     usbdiag_dump(void (*out)(const char *), uint32_t views, uint32_t max_lines);

/* Re-define the debug macros: keep the serial output, add the ring-buffer copy. */
#ifdef DBG_MSG
#undef DBG_MSG
#endif
#ifdef DBG_VAL
#undef DBG_VAL
#endif
#define DBG_MSG(tag, msg) do { \
        usbdiag_log((tag), (msg), 0, 0); \
        dbg_puts("["); dbg_puts(tag); dbg_puts("] "); dbg_puts(msg); dbg_puts("\n"); \
    } while (0)
#define DBG_VAL(tag, msg, val) do { \
        uint64_t _dv = (uint64_t)(val); \
        usbdiag_log((tag), (msg), _dv, 1); \
        dbg_puts("["); dbg_puts(tag); dbg_puts("] "); dbg_puts(msg); dbg_puts(": "); dbg_hex64(_dv); dbg_puts("\n"); \
    } while (0)

#endif
