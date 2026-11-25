/* Fexox OS Project, EULA GPLv2 */
#include "UsbDiag.h"
#include <stddef.h>

extern uint64_t usb_tsc_per_ms(void);

#define DIAG_N     512u
#define DIAG_W     96u

typedef struct { uint64_t tsc; uint32_t seq; uint8_t views; char text[DIAG_W]; } diag_ent_t;
static diag_ent_t g_diag[DIAG_N];
static volatile uint32_t g_diag_seq;
static uint64_t g_diag_t0;

static uint64_t dg_tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static uint32_t dg_views(const char *tag) {
    static const char *const usb[]  = { "XHCI", "USB", "EHCI", "UHCI", "HID", "USBHID", "NET", 0 };
    static const char *const stor[] = { "USBMSC", "VFS", "FAT", "KR", "BOOT", "AHCI", "NVME", "IDE", "MSC", 0 };
    uint32_t v = 0;
    for (int i = 0; usb[i]; i++)  { const char *a = tag, *b = usb[i];  while (*a && *a == *b) { a++; b++; } if (!*a && !*b) v |= USBDIAG_VIEW_USB; }
    for (int i = 0; stor[i]; i++) { const char *a = tag, *b = stor[i]; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) v |= USBDIAG_VIEW_STORAGE; }
    if (!v) v = USBDIAG_VIEW_USB | USBDIAG_VIEW_STORAGE;
    if (tag[0] == 'U' && tag[1] == 'S' && tag[2] == 'B' && tag[3] == 'M') v = USBDIAG_VIEW_USB | USBDIAG_VIEW_STORAGE;
    return v;
}

void usbdiag_log(const char *tag, const char *msg, uint64_t val, int has_val) {
    uint32_t seq = __atomic_fetch_add(&g_diag_seq, 1, __ATOMIC_RELAXED);
    diag_ent_t *e = &g_diag[seq % DIAG_N];
    if (seq == 0) g_diag_t0 = dg_tsc();
    e->tsc = dg_tsc(); e->views = (uint8_t)dg_views(tag);
    uint32_t n = 0;
    e->text[n++] = '[';
    for (const char *p = tag; *p && n < 16; p++) e->text[n++] = *p;
    e->text[n++] = ']'; e->text[n++] = ' ';
    for (const char *p = msg; *p && n < DIAG_W - 22; p++) e->text[n++] = (*p == '\n') ? ' ' : *p;
    if (has_val) {
        e->text[n++] = ':'; e->text[n++] = ' '; e->text[n++] = '0'; e->text[n++] = 'x';
        int started = 0;
        for (int sh = 60; sh >= 0; sh -= 4) {
            uint32_t d = (uint32_t)((val >> sh) & 15);
            if (d || started || sh == 0) { started = 1; e->text[n++] = "0123456789ABCDEF"[d]; }
        }
    }
    e->text[n] = '\0';
    e->seq = seq + 1;                      /* written last: marks the entry valid */
}

uint32_t usbdiag_total(void) { return g_diag_seq; }

static uint32_t dg_len(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

void usbdiag_dump(void (*out)(const char *), uint32_t views, uint32_t max_lines) {
    uint32_t total = g_diag_seq;
    if (!total) { out("  (log is empty)\n"); return; }
    uint32_t oldest = total > DIAG_N ? total - DIAG_N : 0;
    /* walk backwards, remember the first (oldest) sequence number to print */
    uint32_t found = 0, start = total;
    for (uint32_t s = total; s > oldest && found < max_lines; s--) {
        diag_ent_t *e = &g_diag[(s - 1) % DIAG_N];
        if (e->seq == s && (e->views & views)) { found++; start = s - 1; }
    }
    uint64_t per = usb_tsc_per_ms(); if (!per) per = 3000000ULL;
    for (uint32_t s = start; s < total; s++) {
        diag_ent_t *e = &g_diag[s % DIAG_N];
        if (e->seq != s + 1 || !(e->views & views)) continue;
        uint64_t ms = (e->tsc - g_diag_t0) / per;
        char line[96]; uint32_t n = 0;
        /* "+SSSSS " : milliseconds since the first log entry, in decimal */
        char t[12]; int tl = 0; uint64_t m = ms; if (!m) t[tl++] = '0'; while (m && tl < 10) { t[tl++] = (char)('0' + m % 10); m /= 10; }
        line[n++] = '+'; for (int pad = tl; pad < 6; pad++) line[n++] = ' '; while (tl) line[n++] = t[--tl];
        line[n++] = ' ';
        uint32_t room = 66 - n, len = dg_len(e->text);
        if (len <= room) { for (uint32_t i = 0; i < len; i++) line[n++] = e->text[i]; }
        else {                              /* keep head and TAIL (the value is at the end) */
            uint32_t head = room - 22 - 1;
            for (uint32_t i = 0; i < head; i++) line[n++] = e->text[i];
            line[n++] = '~';
            for (uint32_t i = len - 22; i < len; i++) line[n++] = e->text[i];
        }
        line[n++] = '\n'; line[n] = '\0';
        out(line);
    }
}
