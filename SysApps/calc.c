/* Fexox OS Project, EULA GPLv2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include "font8x8.h"                                                                   

                                                                                
#define FX_SYS_GET_FRAMEBUFFER 500
#define FX_SYS_WIN_CREATE      600
#define FX_SYS_WIN_GET_RECT    603
#define FX_SYS_INPUT_POLL      604
#define FX_SYS_KBD_READ_CHAR   605

typedef struct {
    uint64_t phys_addr, virt_addr;
    uint32_t width, height, pitch, bpp;
    uint8_t  mode, r_shift, g_shift, b_shift;
} fx_fb_t;

typedef struct { int32_t x, y; uint32_t w, h, redraw_gen; } fx_rect_t;
typedef struct { int32_t x, y; uint8_t buttons; }          fx_input_t;

                                                                                
#define WIN_W      240
#define WIN_H      320
#define TITLEBAR_H 28                                                                             
#define DISP_H     76
#define MARGIN     10
#define GAP        8
#define COLS       4
#define ROWS       5

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define C_BG        RGB(32, 33, 36)
#define C_DISPLAY   RGB(24, 25, 28)
#define C_TEXT      RGB(235, 235, 235)
#define C_SUBTEXT   RGB(150, 155, 160)
#define C_DIGIT     RGB(60, 64, 67)
#define C_FUNC      RGB(84, 88, 92)
#define C_OP        RGB(66, 92, 140)
#define C_EQUALS    RGB(138, 180, 248)
#define C_EQUALS_T  RGB(20, 24, 32)
#define C_HOVER_ADD 18                                   
#define C_PRESS_ADD 40                                   

static fx_fb_t   g_fb;
static fx_rect_t g_rect;

                                                                                
static inline uint32_t native(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (r << g_fb.r_shift) | (g << g_fb.g_shift) | (b << g_fb.b_shift);
}

static uint32_t lighten(uint32_t rgb, int add) {
    int r = (int)((rgb >> 16) & 0xFF) + add, g = (int)((rgb >> 8) & 0xFF) + add, b = (int)(rgb & 0xFF) + add;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

                                                                                          
static void fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIN_W) w = WIN_W - x;
    if (y + h > WIN_H) h = WIN_H - y;
    if (w <= 0 || h <= 0) return;

    uint32_t px = native(rgb);
    for (int row = 0; row < h; row++) {
        int ay = y + row + g_rect.y;
        if (ay < 0 || (uint32_t)ay >= g_fb.height) continue;
        int ax = x + g_rect.x;
        int n  = w;
        if (ax < 0) { n += ax; ax = 0; }
        if (ax + n > (int)g_fb.width) n = (int)g_fb.width - ax;
        if (n <= 0) continue;
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(g_fb.virt_addr +
                               (uint64_t)ay * g_fb.pitch + (uint64_t)ax * 4u);
        while (n--) *p++ = px;
    }
}

static void draw_char(int x, int y, char c, int scale, uint32_t rgb) {
    if (c < 0x20 || c > 0x7E) return;
    const unsigned char *gl = g_font8x8[c - 0x20];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if ((gl[row] >> col) & 1)
                fill_rect(x + col * scale, y + row * scale, scale, scale, rgb);
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }

static void draw_text(int x, int y, const char *s, int scale, uint32_t rgb) {
    for (; *s; s++, x += 8 * scale) draw_char(x, y, *s, scale, rgb);
}

                                                                                
static double g_acc;                                                     
static char   g_op;                                                      
static char   g_entry[32];                                                
static bool   g_typing;
static bool   g_error;

static void format_number(double v, char *out, size_t n) {
    if (isnan(v) || isinf(v)) { snprintf(out, n, "Error"); return; }
    snprintf(out, n, "%.10g", v);
    if (strlen(out) > 12) snprintf(out, n, "%.5e", v);
}

static void calc_reset(void) {
    g_acc = 0; g_op = 0; g_entry[0] = '\0'; g_typing = false; g_error = false;
}

static double entry_value(void) { return strtod(g_entry, NULL); }

static void apply_pending(double rhs) {
    switch (g_op) {
    case '+': g_acc += rhs; break;
    case '-': g_acc -= rhs; break;
    case '*': g_acc *= rhs; break;
    case '/': if (rhs == 0.0) g_error = true; else g_acc /= rhs; break;
    default:  g_acc = rhs;  break;
    }
    if (isnan(g_acc) || isinf(g_acc)) g_error = true;
}

static void press(char key) {
    if (g_error && key != 'C') return;

    if ((key >= '0' && key <= '9') || key == '.') {
        if (!g_typing) { g_entry[0] = '\0'; g_typing = true; if (!g_op) g_acc = 0; }
        size_t len = strlen(g_entry);
        if (key == '.') {
            if (strchr(g_entry, '.')) return;
            if (len == 0) { strcpy(g_entry, "0"); len = 1; }
        } else if (len == 1 && g_entry[0] == '0') {
            len = 0;                                                            
        }
        if (len >= 12) return;
        g_entry[len] = key;
        g_entry[len + 1] = '\0';
        return;
    }

    switch (key) {
    case 'C':
        calc_reset();
        return;
    case '<':
        if (g_typing) {
            size_t len = strlen(g_entry);
            if (len > 0) g_entry[len - 1] = '\0';
            if (g_entry[0] == '\0') strcpy(g_entry, "0");
        }
        return;
    case '+': case '-': case '*': case '/':
        if (g_typing) {
            double v = entry_value();
            if (g_op) apply_pending(v); else g_acc = v;
            g_typing = false;
        }
        g_op = key;                                                                          
        return;
    case '=':
        if (g_typing) {
            double v = entry_value();
            if (g_op) apply_pending(v); else g_acc = v;
            g_typing = false;
        }
        g_op = 0;
        return;
    default:
        return;
    }
}

                                                                                
enum { K_DIGIT, K_FUNC, K_OP, K_EQUALS };

typedef struct { const char *label; char key; int col, row, cs, rs; int kind; } btn_t;

static const btn_t BTNS[] = {
    { "C", 'C', 0, 0, 1, 1, K_FUNC }, { "<", '<', 1, 0, 1, 1, K_FUNC },
    { "/", '/', 2, 0, 1, 1, K_OP   }, { "*", '*', 3, 0, 1, 1, K_OP   },
    { "7", '7', 0, 1, 1, 1, K_DIGIT}, { "8", '8', 1, 1, 1, 1, K_DIGIT},
    { "9", '9', 2, 1, 1, 1, K_DIGIT}, { "-", '-', 3, 1, 1, 1, K_OP   },
    { "4", '4', 0, 2, 1, 1, K_DIGIT}, { "5", '5', 1, 2, 1, 1, K_DIGIT},
    { "6", '6', 2, 2, 1, 1, K_DIGIT}, { "+", '+', 3, 2, 1, 1, K_OP   },
    { "1", '1', 0, 3, 1, 1, K_DIGIT}, { "2", '2', 1, 3, 1, 1, K_DIGIT},
    { "3", '3', 2, 3, 1, 1, K_DIGIT}, { "=", '=', 3, 3, 1, 2, K_EQUALS},
    { "0", '0', 0, 4, 2, 1, K_DIGIT}, { ".", '.', 2, 4, 1, 1, K_DIGIT},
};
#define NBTN ((int)(sizeof(BTNS) / sizeof(BTNS[0])))

static void btn_rect(const btn_t *b, int *x, int *y, int *w, int *h) {
    int cw = (WIN_W - 2 * MARGIN - (COLS - 1) * GAP) / COLS;
    int ch = (WIN_H - DISP_H - 2 * MARGIN - (ROWS - 1) * GAP) / ROWS;
    *x = MARGIN + b->col * (cw + GAP);
    *y = DISP_H + MARGIN + b->row * (ch + GAP);
    *w = b->cs * cw + (b->cs - 1) * GAP;
    *h = b->rs * ch + (b->rs - 1) * GAP;
}

static int hit_test(int lx, int ly) {
    for (int i = 0; i < NBTN; i++) {
        int x, y, w, h;
        btn_rect(&BTNS[i], &x, &y, &w, &h);
        if (lx >= x && ly >= y && lx < x + w && ly < y + h) return i;
    }
    return -1;
}

                                                                                
static void draw_display(void) {
    fill_rect(0, 0, WIN_W, DISP_H, C_DISPLAY);

    char main_txt[40], sub[48] = "";
    if (g_error)        snprintf(main_txt, sizeof main_txt, "Error");
    else if (g_typing)  snprintf(main_txt, sizeof main_txt, "%s", g_entry);
    else                format_number(g_acc, main_txt, sizeof main_txt);

    if (g_op && !g_error) {                                                 
        char a[32];
        format_number(g_acc, a, sizeof a);
        snprintf(sub, sizeof sub, "%s %c", a, g_op);
    }
    if (sub[0]) draw_text(WIN_W - MARGIN - text_w(sub, 1), 10, sub, 1, C_SUBTEXT);

    int scale = (text_w(main_txt, 3) <= WIN_W - 2 * MARGIN) ? 3 : 2;
    draw_text(WIN_W - MARGIN - text_w(main_txt, scale), DISP_H - 16 - 8 * scale, main_txt, scale, C_TEXT);
}

static void draw_button(int i, int hover, int pressed) {
    const btn_t *b = &BTNS[i];
    int x, y, w, h;
    btn_rect(b, &x, &y, &w, &h);

    uint32_t bg, fg = C_TEXT;
    switch (b->kind) {
    case K_FUNC:   bg = C_FUNC;  break;
    case K_OP:     bg = C_OP;    break;
    case K_EQUALS: bg = C_EQUALS; fg = C_EQUALS_T; break;
    default:       bg = C_DIGIT; break;
    }
    if (i == pressed)    bg = lighten(bg, C_PRESS_ADD);
    else if (i == hover) bg = lighten(bg, C_HOVER_ADD);

    fill_rect(x, y, w, h, bg);
    draw_text(x + (w - text_w(b->label, 2)) / 2, y + (h - 16) / 2, b->label, 2, fg);
}

static void draw_all(int hover, int pressed) {
    fill_rect(0, 0, WIN_W, WIN_H, C_BG);
    draw_display();
    for (int i = 0; i < NBTN; i++) draw_button(i, hover, pressed);
}

                                                                                
static char map_key(char c) {
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '*' || c == '/') return c;
    if (c == '=' || c == '\r' || c == '\n') return '=';
    if (c == 8 || c == 127) return '<';
    if (c == 27 || c == 'c' || c == 'C') return 'C';
    if (c == 'x' || c == 'X') return '*';
    return 0;
}

int main(void) {
    const char title[] = "Calculator";
    long win = syscall(FX_SYS_WIN_CREATE, title, (long)strlen(title), (long)WIN_W, (long)(WIN_H + TITLEBAR_H));
    if (win < 0) { printf("[calc] cannot create window\n"); return 1; }

    if (syscall(FX_SYS_WIN_GET_RECT, win, &g_rect) < 0) return 1;
    if (syscall(FX_SYS_GET_FRAMEBUFFER, &g_fb) < 0 || g_fb.mode != 1 || g_fb.bpp != 32) {
        printf("[calc] need a 32-bit linear framebuffer\n");
        return 1;
    }

    calc_reset();
    int hover = -1, pressed = -1;
    bool prev_lmb = false;
    int32_t last_x = g_rect.x, last_y = g_rect.y;
    uint32_t last_gen = g_rect.redraw_gen;
    draw_all(hover, pressed);

    for (;;) {
        if (syscall(FX_SYS_WIN_GET_RECT, win, &g_rect) < 0) break;                           
        bool dirty = false;
        if (g_rect.x != last_x || g_rect.y != last_y || g_rect.redraw_gen != last_gen) {
            last_x = g_rect.x; last_y = g_rect.y; last_gen = g_rect.redraw_gen;
            dirty = true;                                                                             
        }

                                                                            
        for (int n = 0; n < 8; n++) {
            char ch = 0;
            if (syscall(FX_SYS_KBD_READ_CHAR, &ch) != 1) break;
            char k = map_key(ch);
            if (k) { press(k); dirty = true; }
        }

                   
        fx_input_t in;
        if (syscall(FX_SYS_INPUT_POLL, &in) == 0) {
            int lx = in.x - g_rect.x, ly = in.y - g_rect.y;
            bool inside = lx >= 0 && ly >= 0 && lx < WIN_W && ly < WIN_H;
            bool lmb = (in.buttons & 1) != 0;

            int h = inside ? hit_test(lx, ly) : -1;
            if (h != hover) { hover = h; dirty = true; }

            if (lmb && !prev_lmb && h >= 0) {                                         
                pressed = h;
                press(BTNS[h].key);
                dirty = true;
            } else if (!lmb && pressed >= 0) {
                pressed = -1;
                dirty = true;
            }
            prev_lmb = lmb;
        }

        if (dirty) draw_all(hover, pressed);
        usleep(10000);                                                                             
    }
    return 0;
}
