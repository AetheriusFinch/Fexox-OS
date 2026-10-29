/* Fexox OS Project, EULA GPLv2 */
 


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../Syscall.h"

#define SLG_SYS_INPUT_POLL 604
#define SLG_SYS_KLOG_READ  880
#define SLG_SYS_EXIT       60
#define SLG_SYS_YIELD      24
#define SLG_SYS_NANOSLEEP  35
#define SLG_SYS_FB         500
#define SLG_SYS_WIN_CREATE 600
#define SLG_SYS_WIN_RECT   603

typedef struct { int32_t x, y; uint8_t buttons; } slg_input_t;

static inline long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

static void sys_exit(int code) {
    raw_syscall(SLG_SYS_EXIT, code, 0, 0, 0, 0);
    for (;;) { }
}
static void sys_yield(void) { raw_syscall(SLG_SYS_YIELD, 0, 0, 0, 0, 0); }
 

typedef struct { int64_t tv_sec; int64_t tv_nsec; } slg_timespec_t;
static void sys_sleep_ms(unsigned ms) {
    slg_timespec_t ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (int64_t)(ms % 1000) * 1000000;
    raw_syscall(SLG_SYS_NANOSLEEP, (long)(uintptr_t)&ts, 0, 0, 0, 0);
}

 





#if defined(__GNUC__) && !defined(__clang__)
#define NO_LOOP_PATTERNS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define NO_LOOP_PATTERNS
#endif
NO_LOOP_PATTERNS void *memset(void *d, int c, size_t n) {
    unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d;
}
NO_LOOP_PATTERNS void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    while (n--) *dd++ = *ss++; return d;
}
NO_LOOP_PATTERNS void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

 
static unsigned long str_len(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }

 
static const unsigned char g_font8x8[95][8] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  
    { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00 },  
    { 0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00 },  
    { 0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00 },  
    { 0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00 },  
    { 0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00 },  
    { 0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00 },  
    { 0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00 },  
    { 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00 },  
    { 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00 },  
    { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00 },  
    { 0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00 },  
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06 },  
    { 0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00 },  
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00 },  
    { 0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00 },  
    { 0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00 },  
    { 0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00 },  
    { 0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00 },  
    { 0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00 },  
    { 0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00 },  
    { 0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00 },  
    { 0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00 },  
    { 0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00 },  
    { 0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00 },  
    { 0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00 },  
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00 },  
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06 },  
    { 0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00 },  
    { 0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00 },  
    { 0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00 },  
    { 0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00 },  
    { 0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00 },  
    { 0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00 },  
    { 0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00 },  
    { 0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00 },  
    { 0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00 },  
    { 0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00 },  
    { 0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00 },  
    { 0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00 },  
    { 0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00 },  
    { 0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },  
    { 0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00 },  
    { 0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00 },  
    { 0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00 },  
    { 0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00 },  
    { 0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00 },  
    { 0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00 },  
    { 0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00 },  
    { 0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00 },  
    { 0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00 },  
    { 0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00 },  
    { 0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },  
    { 0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00 },  
    { 0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00 },  
    { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00 },  
    { 0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00 },  
    { 0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00 },  
    { 0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00 },  
    { 0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00 },  
    { 0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00 },  
    { 0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00 },  
    { 0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00 },  
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF },  
    { 0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00 },  
    { 0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00 },  
    { 0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00 },  
    { 0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00 },  
    { 0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00 },  
    { 0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00 },  
    { 0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00 },  
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F },  
    { 0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00 },  
    { 0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00 },  
    { 0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E },  
    { 0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00 },  
    { 0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },  
    { 0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00 },  
    { 0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00 },  
    { 0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00 },  
    { 0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F },  
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78 },  
    { 0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00 },  
    { 0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00 },  
    { 0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00 },  
    { 0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00 },  
    { 0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00 },  
    { 0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00 },  
    { 0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00 },  
    { 0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F },  
    { 0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00 },  
    { 0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00 },  
    { 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00 },  
    { 0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00 },  
    { 0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00 },  
};

 
static sc_framebuffer_t g_fb;
static uint8_t *g_fb_base;
static uint32_t g_bpp_bytes;
static int32_t g_clip_x0, g_clip_y0, g_clip_x1, g_clip_y1;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << g_fb.r_shift) | ((uint32_t)g << g_fb.g_shift) | ((uint32_t)b << g_fb.b_shift);
}

static void put_px(int32_t x, int32_t y, uint32_t c) {
    if (x < g_clip_x0 || y < g_clip_y0 || x >= g_clip_x1 || y >= g_clip_y1) return;
    uint8_t *p = g_fb_base + (size_t)y * g_fb.pitch + (size_t)x * g_bpp_bytes;
    if (g_bpp_bytes == 4) *(volatile uint32_t *)p = c;
    else { p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)(c >> 16); }
}

static void set_clip(const sc_win_rect_t *r) {
    g_clip_x0 = r->x; g_clip_y0 = r->y;
    g_clip_x1 = r->x + (int32_t)r->w; g_clip_y1 = r->y + (int32_t)r->h;
    if (g_clip_x0 < 0) g_clip_x0 = 0;
    if (g_clip_y0 < 0) g_clip_y0 = 0;
    if (g_clip_x1 > (int32_t)g_fb.width)  g_clip_x1 = (int32_t)g_fb.width;
    if (g_clip_y1 > (int32_t)g_fb.height) g_clip_y1 = (int32_t)g_fb.height;
}

static void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    for (int32_t yy = y; yy < y + h; yy++)
        for (int32_t xx = x; xx < x + w; xx++) put_px(xx, yy, c);
}

static void draw_char(int32_t x, int32_t y, char ch, uint32_t fg) {
    if (ch < 0x20 || ch > 0x7E) return;
    const unsigned char *g = g_font8x8[(unsigned char)ch - 0x20];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (1u << col)) put_px(x + col, y + row, fg);
}

static void draw_text(int32_t x, int32_t y, const char *s, uint32_t fg) {
    while (*s) { draw_char(x, y, *s++, fg); x += 8; }
}

 
#define WIN_W      700u
#define WIN_H      440u
#define BAR_H      30
#define PAD        6
#define LINE_H     10
#define MAX_COLS   120
#define MAX_LINES  600             

static char     g_lines[MAX_LINES][MAX_COLS + 1];
static uint32_t g_line_col[MAX_LINES];
static uint32_t g_total;           
static int      g_cur_len;         
static char     g_cur[MAX_COLS + 1];
static int      g_cols = 80;
static int      g_scroll_back;     
static bool     g_dirty = true;

static uint32_t COL_BG, COL_BAR, COL_TXT, COL_ERR, COL_OK, COL_NET, COL_DIM, COL_BTN, COL_BTN_ON, COL_WHITE, COL_WARN;

static bool has(const char *s, const char *needle) {
    for (; *s; s++) {
        const char *a = s, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

static uint32_t classify(const char *s) {
    if (has(s, "PANIC") || has(s, "FAIL") || has(s, "failed") || has(s, "error") ||
        has(s, "ERR") || has(s, "not found") || has(s, "FAULT") || has(s, "timeout"))
        return COL_ERR;
    if (has(s, "[USB") || has(s, "[NET") || has(s, "[EHCI") || has(s, "[UHCI") || has(s, "[MSC"))
        return COL_NET;
    if (has(s, "[ELF") || has(s, "[FXA") || has(s, "[WIN") || has(s, "[UI"))
        return COL_OK;
    if (has(s, "[KM") || has(s, "ignored") || has(s, "skipped") || has(s, "WARN"))
        return COL_WARN;
    return COL_TXT;
}

static void push_line(void) {
    g_cur[g_cur_len] = '\0';
    uint32_t idx = g_total % MAX_LINES;
    for (int i = 0; i <= g_cur_len; i++) g_lines[idx][i] = g_cur[i];
    g_line_col[idx] = classify(g_cur);
    g_total++;
    g_cur_len = 0;
    g_dirty = true;
}

static void feed(const char *buf, long n) {
    for (long i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') { push_line(); continue; }
        if (c == '\t') c = ' ';
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) c = '.';    
        if (g_cur_len >= g_cols) push_line();                                
        g_cur[g_cur_len++] = c;
    }
}

static void add_msg(const char *s, uint32_t col) {
    int saved = g_cur_len;
    (void)saved;
    int i = 0;
    while (s[i] && i < MAX_COLS) { g_cur[i] = s[i]; i++; }
    g_cur_len = i;
    push_line();
    g_line_col[(g_total - 1) % MAX_LINES] = col;
}

 
typedef struct { int32_t x, y, w, h; } btn_t;
static btn_t B_LOG, B_CLEAR, B_UP, B_DN;
static bool g_logging = true;    

static bool in_btn(const btn_t *b, int32_t lx, int32_t ly) {
    return lx >= b->x && ly >= b->y && lx < b->x + b->w && ly < b->y + b->h;
}

static void draw_btn(const sc_win_rect_t *r, const btn_t *b, const char *label, bool on) {
    uint32_t bg = on ? COL_BTN_ON : COL_BTN;
    fill_rect(r->x + b->x, r->y + b->y, b->w, b->h, bg);
    int32_t tw = (int32_t)str_len(label) * 8;
    draw_text(r->x + b->x + (b->w - tw) / 2, r->y + b->y + (b->h - 8) / 2, label, COL_WHITE);
}

static void layout(const sc_win_rect_t *r) {
    B_LOG   = (btn_t){ PAD, 4, 104, 22 };
    B_CLEAR = (btn_t){ PAD + 112, 4, 64, 22 };
    B_UP    = (btn_t){ (int32_t)r->w - PAD - 2 * 44 - 4, 4, 44, 22 };
    B_DN    = (btn_t){ (int32_t)r->w - PAD - 44, 4, 44, 22 };
    g_cols = ((int32_t)r->w - 2 * PAD) / 8;
    if (g_cols > MAX_COLS) g_cols = MAX_COLS;
    if (g_cols < 20) g_cols = 20;
}

static void number(unsigned long v, char *out) {
    char t[24]; int n = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
}

static void redraw(const sc_win_rect_t *r) {
    set_clip(r);
    fill_rect(r->x, r->y, (int32_t)r->w, (int32_t)r->h, COL_BG);
    fill_rect(r->x, r->y, (int32_t)r->w, BAR_H, COL_BAR);
    draw_btn(r, &B_LOG, g_logging ? "Stop Log" : "Log System", g_logging);
    draw_btn(r, &B_CLEAR, "Clear", false);
    draw_btn(r, &B_UP, "Up", false);
    draw_btn(r, &B_DN, "Dn", false);

     
    char st[40];
    const char *lbl = "lines: ";
    int p = 0; while (lbl[p]) { st[p] = lbl[p]; p++; }
    char nb[24]; number(g_total, nb);
    for (int i = 0; nb[i] && p < 38; i++) st[p++] = nb[i];
    st[p] = 0;
    draw_text(r->x + PAD + 112 + 64 + 12, r->y + 11, st, COL_DIM);
    draw_text(r->x + (int32_t)r->w / 2 - 20, r->y + 11, "SysLogs v2", COL_WHITE);

    int32_t area_y = BAR_H + 4;
    int visible = ((int32_t)r->h - area_y - PAD) / LINE_H;
    if (visible < 1) visible = 1;

    uint32_t avail = g_total < MAX_LINES ? g_total : MAX_LINES;
    if (g_scroll_back > (int)avail - visible) g_scroll_back = (int)avail - visible;
    if (g_scroll_back < 0) g_scroll_back = 0;
    int32_t last = (int32_t)g_total - 1 - g_scroll_back;      
    int32_t first = last - visible + 1;
    int32_t oldest = (int32_t)g_total - (int32_t)avail;
    if (first < oldest) first = oldest;

    int32_t y = r->y + area_y;
    if (g_total == 0) {
        draw_text(r->x + PAD, y, g_logging ? "Waiting for log..." :
                  "Press [Log System] to stream the kernel log (same as QEMU serial).", COL_DIM);
    }
    for (int32_t i = first; i <= last; i++, y += LINE_H) {
        uint32_t idx = (uint32_t)i % MAX_LINES;
        draw_text(r->x + PAD, y, g_lines[idx], g_line_col[idx]);
    }
    g_dirty = false;
}

static char g_rd[8192];
static uint64_t g_pos;

void syslogs_main(void) {
    if (raw_syscall(SLG_SYS_FB, (long)(uintptr_t)&g_fb, 0, 0, 0, 0) != 0 || g_fb.mode != 1) sys_exit(1);
    g_fb_base = (uint8_t *)(uintptr_t)g_fb.virt_addr;
    g_bpp_bytes = g_fb.bpp / 8; if (!g_bpp_bytes) g_bpp_bytes = 4;

    COL_BG = rgb(0x0C, 0x0C, 0x0C);  COL_BAR = rgb(0x26, 0x26, 0x2C);
    COL_TXT = rgb(0xCC, 0xCC, 0xCC); COL_ERR = rgb(0xFF, 0x60, 0x60);
    COL_OK = rgb(0x70, 0xE0, 0x70);  COL_NET = rgb(0x60, 0xD0, 0xF0);
    COL_DIM = rgb(0x80, 0x80, 0x80); COL_BTN = rgb(0x3A, 0x5F, 0xA8);
    COL_BTN_ON = rgb(0xB0, 0x40, 0x40); COL_WHITE = rgb(0xFF, 0xFF, 0xFF);
    COL_WARN = rgb(0xE8, 0xC0, 0x50);

    static const char title[] = "System Logs";
    long win_id = raw_syscall(SLG_SYS_WIN_CREATE, (long)(uintptr_t)title, (long)str_len(title),
                              (long)WIN_W, (long)(WIN_H + 28u), 0);
    if (win_id < 0) sys_exit(1);

    sc_win_rect_t rect;
    if (raw_syscall(SLG_SYS_WIN_RECT, win_id, (long)(uintptr_t)&rect, 0, 0, 0) != 0) sys_exit(0);
    add_msg("[SYSLOGS] v2 started, streaming kernel log (same as QEMU serial)...", COL_OK);
    layout(&rect);
    int32_t last_x = rect.x, last_y = rect.y;
    redraw(&rect);

    slg_input_t in;
    bool prev_lmb = false;
    unsigned idle_ticks = 0;

    for (;;) {
        if (raw_syscall(SLG_SYS_WIN_RECT, win_id, (long)(uintptr_t)&rect, 0, 0, 0) != 0) sys_exit(0);
        if (rect.x != last_x || rect.y != last_y) { last_x = rect.x; last_y = rect.y; g_dirty = true; }

         
        bool got_data = false;
        if (g_logging) {
            for (int chunk = 0; chunk < 8; chunk++) {
                long n = raw_syscall(SLG_SYS_KLOG_READ, (long)(uintptr_t)&g_pos, (long)(uintptr_t)g_rd,
                                     (long)sizeof(g_rd), 0, 0);
                if (n < 0) {
                    add_msg("[SYSLOGS] SYS_KLOG_READ (880) not available: add the case to syscall_dispatch", COL_ERR);
                    g_logging = false;
                    break;
                }
                if (n == 0) break;
                feed(g_rd, n);
                got_data = true;
                if ((unsigned long)n < sizeof(g_rd)) break;
            }
        }

         
        if (raw_syscall(SLG_SYS_INPUT_POLL, (long)(uintptr_t)&in, 0, 0, 0, 0) >= 0) {
            int32_t lx = in.x - rect.x, ly = in.y - rect.y;
            bool lmb = (in.buttons & 1) != 0;
            if (lmb && !prev_lmb && lx >= 0 && ly >= 0 && lx < (int32_t)rect.w && ly < (int32_t)rect.h) {
                if (in_btn(&B_LOG, lx, ly)) {
                    g_logging = !g_logging;
                    if (g_logging) {
                        g_pos = 0;                  
                        g_total = 0; g_cur_len = 0; g_scroll_back = 0;
                    }
                    g_dirty = true;
                } else if (in_btn(&B_CLEAR, lx, ly)) {
                    g_total = 0; g_cur_len = 0; g_scroll_back = 0; g_dirty = true;
                } else if (in_btn(&B_UP, lx, ly)) {
                    g_scroll_back += 10; g_dirty = true;
                } else if (in_btn(&B_DN, lx, ly)) {
                    g_scroll_back -= 10; if (g_scroll_back < 0) g_scroll_back = 0; g_dirty = true;
                }
            }
            prev_lmb = lmb;
        }

         
        if (g_dirty || ++idle_ticks > 100) { idle_ticks = 0; layout(&rect); redraw(&rect); }

        if (got_data) sys_yield();        
        else          sys_sleep_ms(30);   
    }
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "    and $-16, %rsp\n"
    "    call syslogs_main\n"
    "    mov $60, %rax\n"
    "    xor %rdi, %rdi\n"
    "    syscall\n"
    "1:  jmp 1b\n"
);
