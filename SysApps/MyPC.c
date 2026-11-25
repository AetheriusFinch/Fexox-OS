
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "font8x8.h"

/* ---- Fexox system calls ---- */
#define FX_SYS_GET_FRAMEBUFFER 500
#define FX_SYS_WIN_CREATE      600
#define FX_SYS_WIN_GET_RECT    603
#define FX_SYS_INPUT_POLL      604
#define FX_SYS_KBD_READ_CHAR   605
#define FX_SYS_DISPLAY_GET     530
#define FX_SYS_MOUNT_INFO      534
#define FX_SYS_IMAGE_LOAD      535

typedef struct {
    uint64_t phys_addr, virt_addr;
    uint32_t width, height, pitch, bpp;
    uint8_t  mode, r_shift, g_shift, b_shift;
} fx_fb_t;
typedef struct { int32_t x, y; uint32_t w, h, redraw_gen; } fx_rect_t;
typedef struct { int32_t x, y; uint8_t buttons; }          fx_input_t;
typedef struct { uint32_t version, generation, width, height, phys_w, phys_h, max_w, max_h, pitch,
                 rotation, filter, fit, flags, reserved[4]; } fx_display_t;
typedef struct {
    char path[128]; char dev[32]; char fs[16];
    uint64_t dev_bytes, total_bytes, free_bytes; uint32_t flags, pad;
} fx_mount_t;
typedef struct { uint64_t pixels; uint32_t cap_w, cap_h, out_w, out_h; } fx_imgreq_t;

/* ---- layout ---- */
#define WIN_W       640
#define WIN_H       420
#define TITLEBAR_H  28
#define BAR_H       34
#define CELL_W      104
#define CELL_H      96
#define ICON_SZ     48
#define SCROLL_W    22
#define MAX_ENTRIES 512
#define PATH_MAX_   1024

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define C_BG       RGB(247, 248, 250)
#define C_BAR      RGB(228, 230, 235)
#define C_TEXT     RGB(32, 33, 36)
#define C_SUB      RGB(110, 114, 122)
#define C_HOVER    RGB(222, 231, 247)
#define C_SELECT   RGB(190, 212, 247)
#define C_BTN      RGB(210, 213, 220)
#define C_BTN_HOT  RGB(190, 196, 208)
#define C_MENU     RGB(255, 255, 255)
#define C_MENU_BD  RGB(170, 174, 184)
#define C_DLG      RGB(240, 241, 244)
#define C_ACCENT   RGB(60, 120, 216)

static fx_fb_t   g_fb;
static fx_rect_t g_rect;

static inline uint32_t native(uint32_t rgb) {
    return ((rgb >> 16 & 255u) << g_fb.r_shift) | ((rgb >> 8 & 255u) << g_fb.g_shift) | ((rgb & 255u) << g_fb.b_shift);
}
static inline volatile uint32_t *pix(int x, int y) {          /* window-relative -> framebuffer */
    int ax = x + g_rect.x, ay = y + g_rect.y;
    if (x < 0 || y < 0 || x >= WIN_W || y >= WIN_H) return NULL;
    if (ax < 0 || ay < 0 || (uint32_t)ax >= g_fb.width || (uint32_t)ay >= g_fb.height) return NULL;
    return (volatile uint32_t *)(uintptr_t)(g_fb.virt_addr + (uint64_t)ay * g_fb.pitch + (uint64_t)ax * 4u);
}
static void fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIN_W) w = WIN_W - x;
    if (y + h > WIN_H) h = WIN_H - y;
    if (w <= 0 || h <= 0) return;
    uint32_t px = native(rgb);
    for (int r = 0; r < h; r++) {
        volatile uint32_t *p = pix(x, y + r);
        if (!p) continue;
        int n = w;
        int ax = x + g_rect.x;
        if (ax + n > (int)g_fb.width) n = (int)g_fb.width - ax;
        while (n-- > 0) *p++ = px;
    }
}
static void frame_rect(int x, int y, int w, int h, uint32_t rgb) {
    fill_rect(x, y, w, 1, rgb); fill_rect(x, y + h - 1, w, 1, rgb);
    fill_rect(x, y, 1, h, rgb); fill_rect(x + w - 1, y, 1, h, rgb);
}
static void draw_char(int x, int y, char c, int scale, uint32_t rgb) {
    if (c < 0x20 || c > 0x7E) return;
    const unsigned char *gl = g_font8x8[c - 0x20];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if ((gl[row] >> col) & 1) fill_rect(x + col * scale, y + row * scale, scale, scale, rgb);
}
static int text_w(const char *s, int scale) { return (int)strlen(s) * 8 * scale; }
static void draw_text(int x, int y, const char *s, int scale, uint32_t rgb) {
    for (; *s; s++, x += 8 * scale) draw_char(x, y, *s, scale, rgb);
}
static void draw_text_center(int cx, int y, const char *s, int scale, uint32_t rgb) {
    draw_text(cx - text_w(s, scale) / 2, y, s, scale, rgb);
}

/* RGBA image alpha-blended onto the window */
typedef struct { uint8_t px[ICON_SZ * ICON_SZ * 4]; uint32_t w, h; bool ok; } icon_t;
static icon_t g_icon_disk, g_icon_folder, g_icon_file;

static void load_icon(icon_t *ic, const char *path) {
    fx_imgreq_t rq = { (uint64_t)(uintptr_t)ic->px, ICON_SZ, ICON_SZ, 0, 0 };
    if (syscall(FX_SYS_IMAGE_LOAD, path, &rq) == 0 && rq.out_w && rq.out_h) { ic->w = rq.out_w; ic->h = rq.out_h; ic->ok = true; }
}
static void blit_icon(const icon_t *ic, int x, int y) {
    for (uint32_t j = 0; j < ic->h; j++)
        for (uint32_t i = 0; i < ic->w; i++) {
            const uint8_t *p = ic->px + ((size_t)j * ic->w + i) * 4;
            if (!p[3]) continue;
            volatile uint32_t *d = pix(x + (int)i, y + (int)j);
            if (!d) continue;
            if (p[3] == 255) { *d = native(RGB(p[0], p[1], p[2])); continue; }
            uint32_t bg = *d;
            uint32_t br = (bg >> g_fb.r_shift) & 255u, bgc = (bg >> g_fb.g_shift) & 255u, bb = (bg >> g_fb.b_shift) & 255u;
            uint32_t a = p[3];
            *d = native(RGB((p[0] * a + br * (255 - a)) / 255, (p[1] * a + bgc * (255 - a)) / 255, (p[2] * a + bb * (255 - a)) / 255));
        }
}
/* fallback pictures when a PNG is missing */
static void fallback_icon(int kind, int x, int y) {
    if (kind == 0) { fill_rect(x + 2, y + 12, 44, 24, RGB(150, 154, 162)); fill_rect(x + 5, y + 15, 38, 3, RGB(90, 94, 102));
                     fill_rect(x + 5, y + 22, 38, 3, RGB(90, 94, 102)); fill_rect(x + 36, y + 29, 5, 4, RGB(80, 200, 80)); }
    else if (kind == 1) { fill_rect(x + 3, y + 10, 18, 6, RGB(236, 190, 70)); fill_rect(x + 3, y + 15, 42, 26, RGB(246, 205, 90)); }
    else { fill_rect(x + 10, y + 4, 28, 40, RGB(250, 250, 252)); frame_rect(x + 10, y + 4, 28, 40, RGB(150, 154, 162));
           fill_rect(x + 15, y + 14, 18, 2, RGB(170, 174, 182)); fill_rect(x + 15, y + 20, 18, 2, RGB(170, 174, 182)); }
}
static void draw_kind_icon(int kind, int x, int y) {
    icon_t *ic = kind == 0 ? &g_icon_disk : kind == 1 ? &g_icon_folder : &g_icon_file;
    if (ic->ok) blit_icon(ic, x + (ICON_SZ - (int)ic->w) / 2, y + (ICON_SZ - (int)ic->h) / 2);
    else        fallback_icon(kind, x, y);
}

/* ---- model ---- */
enum { K_DRIVE = 0, K_DIR = 1, K_FILE = 2, K_UP = 3 };
typedef struct { char name[256]; char sub[48]; char sub2[32]; char target[PATH_MAX_]; int kind; } entry_t;

static entry_t *g_ent;
static int      g_n;
static char     g_path[PATH_MAX_] = "";          /* "" = My Computer */
static int      g_scroll;                        /* first visible row */
static int      g_hover = -1, g_sel = -1;

static void fmt_size(uint64_t b, char *o, size_t n) {
    if (b >= (1ull << 30)) snprintf(o, n, "%llu.%llu GB", (unsigned long long)(b >> 30), (unsigned long long)((b >> 30 << 0) ? ((b * 10 >> 30) % 10) : 0));
    else if (b >= (1ull << 20)) snprintf(o, n, "%llu MB", (unsigned long long)(b >> 20));
    else if (b >= 1024) snprintf(o, n, "%llu KB", (unsigned long long)(b >> 10));
    else snprintf(o, n, "%llu B", (unsigned long long)b);
}

static int cmp_entries(const void *a, const void *b) {
    const entry_t *x = a, *y = b;
    if (x->kind != y->kind) return x->kind == K_UP ? -1 : y->kind == K_UP ? 1 : x->kind - y->kind;
    return strcasecmp(x->name, y->name);
}

static void parent_path(const char *p, char *out, size_t n) {
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/') l--;
    while (l > 1 && p[l - 1] != '/') l--;
    if (l > 1) l--;
    if (l >= n) l = n - 1;
    memcpy(out, p, l); out[l] = 0;
    if (!out[0]) strcpy(out, "/");
}

static void reload(void) {
    g_n = 0; g_scroll = 0; g_hover = g_sel = -1;
    if (!g_path[0]) {                                   /* My Computer: the mounted volumes */
        fx_mount_t m;
        for (int i = 0; i < 64 && g_n < MAX_ENTRIES; i++) {
            if (syscall(FX_SYS_MOUNT_INFO, (long)i, &m) != 0) break;
            entry_t *e = &g_ent[g_n++];
            memset(e, 0, sizeof *e);
            e->kind = K_DRIVE;
            snprintf(e->name, sizeof e->name, "%s", m.path);
            char sz[24];
            fmt_size(m.total_bytes ? m.total_bytes : m.dev_bytes, sz, sizeof sz);
            snprintf(e->sub, sizeof e->sub, "%s %s", m.fs, sz);
            snprintf(e->sub2, sizeof e->sub2, "%s", m.dev);
            snprintf(e->target, sizeof e->target, "%s", m.path);
        }
        return;
    }
    entry_t *up = &g_ent[g_n++];
    memset(up, 0, sizeof *up);
    up->kind = K_UP; strcpy(up->name, "..");
    if (strcmp(g_path, "/") == 0) up->target[0] = 0; else parent_path(g_path, up->target, sizeof up->target);
    DIR *d = opendir(g_path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_n < MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        entry_t *e = &g_ent[g_n];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        snprintf(e->target, sizeof e->target, "%s%s%s", g_path, strcmp(g_path, "/") ? "/" : "", de->d_name);
        int isdir = (de->d_type == DT_DIR);
        if (de->d_type == DT_UNKNOWN || de->d_type == DT_LNK) { struct stat st; isdir = (stat(e->target, &st) == 0) && S_ISDIR(st.st_mode); }
        e->kind = isdir ? K_DIR : K_FILE;
        if (!isdir) { struct stat st; if (stat(e->target, &st) == 0) fmt_size((uint64_t)st.st_size, e->sub, sizeof e->sub); }
        g_n++;
    }
    closedir(d);
    if (g_n > 2) qsort(g_ent, (size_t)g_n, sizeof(entry_t), cmp_entries);
}

static void open_path(const char *p) { snprintf(g_path, sizeof g_path, "%s", p); reload(); }

/* ---- geometry ---- */
static int cols(void) { int c = (WIN_W - SCROLL_W - 12) / CELL_W; return c < 1 ? 1 : c; }
static int rows_visible(void) { return (WIN_H - BAR_H - 8) / CELL_H; }
static int total_rows(void) { return (g_n + cols() - 1) / cols(); }
static void cell_pos(int idx, int *x, int *y) {
    int c = cols();
    *x = 8 + (idx % c) * CELL_W;
    *y = BAR_H + 6 + (idx / c - g_scroll) * CELL_H;
}
static int hit_entry(int lx, int ly) {
    if (ly < BAR_H + 6 || lx < 8 || lx >= 8 + cols() * CELL_W) return -1;
    int c = (lx - 8) / CELL_W, r = (ly - BAR_H - 6) / CELL_H + g_scroll;
    if ((ly - BAR_H - 6) / CELL_H >= rows_visible()) return -1;
    int idx = r * cols() + c;
    return idx < g_n ? idx : -1;
}

/* ---- popups ---- */
static bool g_menu_open; static int g_menu_x, g_menu_y, g_menu_hot = -1;
static const char *const k_menu[3] = { "New folder", "New file", "Refresh" };
#define MENU_W 150
#define MENU_ITEM_H 26
static bool g_dlg_open; static int g_dlg_kind;            /* 0 folder, 1 file */
static char g_dlg_text[64]; static int g_dlg_len; static char g_dlg_err[64]; static bool g_dlg_fresh;   /* first key replaces the proposed name */

static void trunc_label(const char *s, int maxc, char *out) {
    size_t l = strlen(s);
    if ((int)l <= maxc) { strcpy(out, s); return; }
    memcpy(out, s, (size_t)maxc - 2); out[maxc - 2] = '.'; out[maxc - 1] = '.'; out[maxc] = 0;
}

static void draw_all(int lx, int ly) {
    fill_rect(0, 0, WIN_W, WIN_H, C_BG);
    /* top bar */
    fill_rect(0, 0, WIN_W, BAR_H, C_BAR);
    bool can_up = g_path[0] != 0;
    bool up_hot = can_up && lx >= 6 && lx < 34 && ly >= 5 && ly < 29;
    fill_rect(6, 5, 28, 24, up_hot ? C_BTN_HOT : C_BTN);
    draw_text_center(20, 13, "<", 1, can_up ? C_TEXT : C_SUB);
    fill_rect(42, 5, WIN_W - 50, 24, RGB(255, 255, 255));
    frame_rect(42, 5, WIN_W - 50, 24, RGB(190, 194, 204));
    char pbuf[96];
    trunc_label(g_path[0] ? g_path : "My Computer", 70, pbuf);
    draw_text(50, 13, pbuf, 1, C_TEXT);
    /* items */
    int rv = rows_visible();
    for (int i = 0; i < g_n; i++) {
        int x, y; cell_pos(i, &x, &y);
        if (y < BAR_H || y + CELL_H > WIN_H) continue;
        if (i == g_sel)        fill_rect(x, y, CELL_W - 4, CELL_H - 4, C_SELECT);
        else if (i == g_hover) fill_rect(x, y, CELL_W - 4, CELL_H - 4, C_HOVER);
        const entry_t *e = &g_ent[i];
        int kind = e->kind == K_DRIVE ? 0 : (e->kind == K_FILE ? 2 : 1);
        draw_kind_icon(kind, x + (CELL_W - 4 - ICON_SZ) / 2, y + 6);
        char lab[16]; trunc_label(e->name, 12, lab);
        draw_text_center(x + (CELL_W - 4) / 2, y + 6 + ICON_SZ + 6, lab, 1, C_TEXT);
        if (e->sub[0]) { char s2[16]; trunc_label(e->sub, 12, s2); draw_text_center(x + (CELL_W - 4) / 2, y + 6 + ICON_SZ + 18, s2, 1, C_SUB); }
        if (e->sub2[0]) { char s3[16]; trunc_label(e->sub2, 12, s3); draw_text_center(x + (CELL_W - 4) / 2, y + 6 + ICON_SZ + 30, s3, 1, C_SUB); }
    }
    if (g_n == 0) draw_text_center(WIN_W / 2, WIN_H / 2, g_path[0] ? "Empty" : "No volumes", 2, C_SUB);
    /* scroll buttons */
    if (total_rows() > rv) {
        int sx = WIN_W - SCROLL_W - 2;
        fill_rect(sx, BAR_H + 4, SCROLL_W, 22, C_BTN);        draw_text_center(sx + SCROLL_W / 2, BAR_H + 11, "^", 1, C_TEXT);
        fill_rect(sx, WIN_H - 26, SCROLL_W, 22, C_BTN);       draw_text_center(sx + SCROLL_W / 2, WIN_H - 19, "v", 1, C_TEXT);
    }
    /* context menu */
    if (g_menu_open) {
        fill_rect(g_menu_x, g_menu_y, MENU_W, MENU_ITEM_H * 3 + 4, C_MENU);
        frame_rect(g_menu_x, g_menu_y, MENU_W, MENU_ITEM_H * 3 + 4, C_MENU_BD);
        for (int i = 0; i < 3; i++) {
            if (i == g_menu_hot) fill_rect(g_menu_x + 2, g_menu_y + 2 + i * MENU_ITEM_H, MENU_W - 4, MENU_ITEM_H, C_HOVER);
            draw_text(g_menu_x + 12, g_menu_y + 2 + i * MENU_ITEM_H + 9, k_menu[i], 1, C_TEXT);
        }
    }
    /* name dialog */
    if (g_dlg_open) {
        int dw = 330, dh = 118, dx = (WIN_W - dw) / 2, dy = (WIN_H - dh) / 2;
        fill_rect(dx, dy, dw, dh, C_DLG); frame_rect(dx, dy, dw, dh, C_MENU_BD);
        draw_text(dx + 14, dy + 12, g_dlg_kind == 0 ? "New folder name:" : "New file name:", 1, C_TEXT);
        fill_rect(dx + 14, dy + 32, dw - 28, 24, RGB(255, 255, 255)); frame_rect(dx + 14, dy + 32, dw - 28, 24, C_ACCENT);
        draw_text(dx + 20, dy + 40, g_dlg_text, 1, C_TEXT);
        fill_rect(dx + 20 + g_dlg_len * 8, dy + 38, 2, 12, C_TEXT);
        if (g_dlg_err[0]) draw_text(dx + 14, dy + 64, g_dlg_err, 1, RGB(200, 40, 40));
        fill_rect(dx + dw - 160, dh + dy - 36, 70, 24, C_BTN);  draw_text_center(dx + dw - 125, dh + dy - 28, "Create", 1, C_TEXT);
        fill_rect(dx + dw - 82, dh + dy - 36, 70, 24, C_BTN);   draw_text_center(dx + dw - 47, dh + dy - 28, "Cancel", 1, C_TEXT);
    }
}

static void do_create(void) {
    if (!g_dlg_len) { snprintf(g_dlg_err, sizeof g_dlg_err, "Enter a name"); return; }
    if (strchr(g_dlg_text, '/')) { snprintf(g_dlg_err, sizeof g_dlg_err, "Name cannot contain /"); return; }
    char full[PATH_MAX_];
    snprintf(full, sizeof full, "%s%s%s", g_path, strcmp(g_path, "/") ? "/" : "", g_dlg_text);
    int rc;
    if (g_dlg_kind == 0) rc = mkdir(full, 0755);
    else { int fd = open(full, O_CREAT | O_EXCL | O_WRONLY, 0644); rc = fd < 0 ? -1 : close(fd); }
    if (rc != 0) { snprintf(g_dlg_err, sizeof g_dlg_err, "Failed: %s", strerror(errno)); return; }
    g_dlg_open = false; g_dlg_err[0] = 0;
    reload();
}

static void open_dialog(int kind) {
    g_dlg_open = true; g_dlg_kind = kind; g_dlg_err[0] = 0;
    snprintf(g_dlg_text, sizeof g_dlg_text, "%s", kind == 0 ? "New folder" : "New file");
    g_dlg_len = (int)strlen(g_dlg_text);
    g_dlg_fresh = true;
}

static void click(int lx, int ly, bool right) {
    if (g_dlg_open) {
        int dw = 330, dh = 118, dx = (WIN_W - dw) / 2, dy = (WIN_H - dh) / 2;
        if (lx >= dx + dw - 160 && lx < dx + dw - 90 && ly >= dy + dh - 36 && ly < dy + dh - 12) do_create();
        else if (lx >= dx + dw - 82 && lx < dx + dw - 12 && ly >= dy + dh - 36 && ly < dy + dh - 12) g_dlg_open = false;
        return;
    }
    if (g_menu_open) {
        g_menu_open = false;
        if (lx >= g_menu_x && lx < g_menu_x + MENU_W && ly >= g_menu_y + 2 && ly < g_menu_y + 2 + MENU_ITEM_H * 3) {
            int it = (ly - g_menu_y - 2) / MENU_ITEM_H;
            if (it == 0 && g_path[0]) open_dialog(0);
            else if (it == 1 && g_path[0]) open_dialog(1);
            else if (it == 2) reload();
        }
        return;
    }
    if (right) {
        g_menu_open = true; g_menu_hot = -1;
        g_menu_x = lx + MENU_W > WIN_W ? WIN_W - MENU_W - 2 : lx;
        g_menu_y = ly + MENU_ITEM_H * 3 + 4 > WIN_H ? WIN_H - MENU_ITEM_H * 3 - 6 : ly;
        return;
    }
    if (ly < BAR_H) {
        if (g_path[0] && lx >= 6 && lx < 34 && ly >= 5 && ly < 29) {
            char par[PATH_MAX_];
            if (strcmp(g_path, "/") == 0) par[0] = 0; else parent_path(g_path, par, sizeof par);
            open_path(par);
        }
        return;
    }
    int sx = WIN_W - SCROLL_W - 2;
    if (total_rows() > rows_visible() && lx >= sx) {
        if (ly < BAR_H + 26 && g_scroll > 0) g_scroll--;
        else if (ly >= WIN_H - 28 && g_scroll < total_rows() - rows_visible()) g_scroll++;
        return;
    }
    int i = hit_entry(lx, ly);
    if (i < 0) { g_sel = -1; return; }
    g_sel = i;
    const entry_t *e = &g_ent[i];
    if (e->kind == K_DIR || e->kind == K_DRIVE) open_path(e->target);
    else if (e->kind == K_UP) open_path(e->target);
}

#ifdef MYPC_TEST
extern int mypc_test_hook(void);
#endif

int main(void) {
    const char title[] = "My Computer";
    long win = syscall(FX_SYS_WIN_CREATE, title, (long)strlen(title), (long)WIN_W, (long)(WIN_H + TITLEBAR_H));
    if (win < 0) { printf("[mypc] cannot create window\n"); return 1; }
    if (syscall(FX_SYS_WIN_GET_RECT, win, &g_rect) < 0) return 1;
    if (syscall(FX_SYS_GET_FRAMEBUFFER, &g_fb) < 0 || g_fb.mode != 1 || g_fb.bpp != 32) {
        printf("[mypc] need a 32-bit linear framebuffer\n");
        return 1;
    }
    g_ent = (entry_t *)malloc(sizeof(entry_t) * MAX_ENTRIES);
    if (!g_ent) return 1;
    load_icon(&g_icon_disk, "/UI/disk.png");
    load_icon(&g_icon_folder, "/UI/folder.png");
    load_icon(&g_icon_file, "/UI/file.png");
    reload();

    fx_display_t di; uint32_t last_disp = 0;
    if (syscall(FX_SYS_DISPLAY_GET, &di) == 0) last_disp = di.generation;
    int32_t last_x = g_rect.x, last_y = g_rect.y; uint32_t last_gen = g_rect.redraw_gen;
    bool prev_l = false, prev_r = false;
    int lx = -1, ly = -1;
    draw_all(lx, ly);

    for (;;) {
        if (syscall(FX_SYS_WIN_GET_RECT, win, &g_rect) < 0) break;            /* window closed */
        bool dirty = false;
        if (g_rect.x != last_x || g_rect.y != last_y || g_rect.redraw_gen != last_gen) {
            last_x = g_rect.x; last_y = g_rect.y; last_gen = g_rect.redraw_gen; dirty = true;
        }
        if (syscall(FX_SYS_DISPLAY_GET, &di) == 0 && di.generation != last_disp) {
            last_disp = di.generation; g_fb.width = di.width; g_fb.height = di.height; dirty = true;
        }
        for (int n = 0; n < 8; n++) {                                            /* keyboard */
            char ch = 0;
            if (syscall(FX_SYS_KBD_READ_CHAR, &ch) != 1) break;
            if (g_dlg_open) {
                if (ch == '\r' || ch == '\n') do_create();
                else if (ch == 27) g_dlg_open = false;
                else if (ch == 8 || ch == 127) { if (g_dlg_fresh) { g_dlg_len = 0; g_dlg_text[0] = 0; g_dlg_fresh = false; } else if (g_dlg_len > 0) g_dlg_text[--g_dlg_len] = 0; }
                else if (ch >= 0x20 && ch < 0x7F) {
                    if (g_dlg_fresh) { g_dlg_len = 0; g_dlg_text[0] = 0; g_dlg_fresh = false; }
                    if (g_dlg_len < (int)sizeof(g_dlg_text) - 1) { g_dlg_text[g_dlg_len++] = ch; g_dlg_text[g_dlg_len] = 0; }
                }
                dirty = true;
            } else if (ch == 8 || ch == 127) { if (g_path[0]) { char par[PATH_MAX_]; if (!strcmp(g_path, "/")) par[0] = 0; else parent_path(g_path, par, sizeof par); open_path(par); dirty = true; } }
            else if (ch == 27) { g_menu_open = false; dirty = true; }
        }
        fx_input_t in;
        if (syscall(FX_SYS_INPUT_POLL, &in) == 0) {
            lx = in.x - g_rect.x; ly = in.y - g_rect.y;
            bool inside = lx >= 0 && ly >= 0 && lx < WIN_W && ly < WIN_H;
            bool l = (in.buttons & 1) != 0, r = (in.buttons & 2) != 0;
            int h = (inside && !g_menu_open && !g_dlg_open) ? hit_entry(lx, ly) : -1;
            if (h != g_hover) { g_hover = h; dirty = true; }
            if (g_menu_open && inside) {
                int hot = (lx >= g_menu_x && lx < g_menu_x + MENU_W && ly >= g_menu_y + 2 && ly < g_menu_y + 2 + MENU_ITEM_H * 3) ? (ly - g_menu_y - 2) / MENU_ITEM_H : -1;
                if (hot != g_menu_hot) { g_menu_hot = hot; dirty = true; }
            }
            if (inside && ((l && !prev_l) || (r && !prev_r))) { click(lx, ly, r && !prev_r); dirty = true; }
            prev_l = l; prev_r = r;
        }
        if (dirty) draw_all(lx, ly);
#ifdef MYPC_TEST
        if (mypc_test_hook()) break;
#endif
        usleep(10000);
    }
    return 0;
}
