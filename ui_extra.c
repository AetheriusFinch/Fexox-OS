/* Fexox OS Project, EULA GPLv2 */
 





#include <stdint.h>
#include <stddef.h>
#include "Framebuffer.h"
#include "png.h"
#include "ui_extra.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

extern int     vfs_open(const char *path, int flags);
extern int     vfs_close(int fd);
extern int64_t vfs_read(int fd, void *buf, uint64_t size);

 
extern uint8_t ps2_mouse_buttons(void);
 





extern uint8_t usb_hid_mouse_buttons(void);

#ifndef UI_O_RDONLY
#define UI_O_RDONLY 0
#endif
 
#define CURSOR_MAX_W        32
#define CURSOR_MAX_H        32

 

#define CURSOR_FILE_MAXSZ   (256 * 1024)

 


static png_image_t g_cursor;          
static int         g_cursor_loaded = 0;

static int32_t g_mouse_x = 100;
static int32_t g_mouse_y = 100;

 
static fb_color_t g_saved_bg[CURSOR_MAX_W * CURSOR_MAX_H];
static int32_t    g_saved_x = -1, g_saved_y = -1;
static uint32_t   g_saved_w = 0,  g_saved_h = 0;
static int        g_saved_valid = 0;

 


static int load_cursor_png(void) {
     
    DBG_MSG("UI", "cursor: trying UI/cursor.png");
    int fd = vfs_open("UI/cursor.png", UI_O_RDONLY);
    if (fd < 0) {
        DBG_MSG("UI", "cursor: trying /UI/cursor.png");
        fd = vfs_open("/UI/cursor.png", UI_O_RDONLY);
    }
    if (fd < 0) {
        DBG_MSG("UI", "cursor.png not found, using fallback arrow");
        return -1;
    }
    DBG_VAL("UI", "cursor: fd", (uint64_t)fd);

    uint8_t *buf = (uint8_t *)kmalloc(CURSOR_FILE_MAXSZ);
    if (!buf) {
        DBG_MSG("UI", "cursor: kmalloc failed");
        vfs_close(fd);
        return -1;
    }

    size_t total = 0;
    for (;;) {
        int64_t got = vfs_read(fd, buf + total, CURSOR_FILE_MAXSZ - total);
        if (got <= 0) break;
        total += (size_t)got;
        if (total >= CURSOR_FILE_MAXSZ) break;
    }
    vfs_close(fd);

    DBG_VAL("UI", "cursor: bytes read", (uint64_t)total);

    if (total == 0) {
        DBG_MSG("UI", "cursor: empty file");
        kfree(buf);
        return -1;
    }

     
    if (total < 8 || buf[0] != 0x89 || buf[1] != 'P' ||
        buf[2] != 'N' || buf[3] != 'G') {
        DBG_MSG("UI", "cursor: not a PNG (bad magic)");
        kfree(buf);
        return -1;
    }

    int rc = png_decode(buf, total, &g_cursor);
    kfree(buf);

    if (rc != PNG_OK) {
        DBG_VAL("UI", "cursor.png decode failed, rc", (uint64_t)(int64_t)rc);
        return -1;
    }
    if (g_cursor.width > CURSOR_MAX_W || g_cursor.height > CURSOR_MAX_H) {
        DBG_VAL("UI", "cursor.png too large, w", g_cursor.width);
        DBG_VAL("UI", "cursor.png too large, h", g_cursor.height);
        DBG_MSG("UI", "cursor.png max is 32x32, using fallback arrow");
        kfree(g_cursor.pixels);
        g_cursor.pixels = NULL;
        return -1;
    }

    DBG_VAL("UI", "cursor.png loaded OK, w", g_cursor.width);
    DBG_VAL("UI", "cursor.png loaded OK, h", g_cursor.height);
    return 0;
}

int ui_extra_init(void) {
    g_cursor.pixels = NULL;
    if (load_cursor_png() == 0) g_cursor_loaded = 1;
     


    return 0;
}

void ui_set_mouse_pos(int32_t x, int32_t y) {
    g_mouse_x = x;
    g_mouse_y = y;
}

 





void ui_get_mouse_pos(int32_t *out_x, int32_t *out_y) {
    if (out_x) *out_x = g_mouse_x;
    if (out_y) *out_y = g_mouse_y;
}

 




static const char g_fallback_arrow[19][12] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.....##### ",
    "#...#       ",
    "#..##       ",
    "#.#.#       ",
    "#. #.#      ",
    "#   #.#     ",
    "    #.#     ",
    "     #      ",
    "            ",
};
#define FALLBACK_W 12
#define FALLBACK_H 19

 


static void cursor_save_bg(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            g_saved_bg[row * w + col] = fb_get_pixel(x + (int32_t)col, y + (int32_t)row);
        }
    }
    g_saved_x = x; g_saved_y = y;
    g_saved_w = w; g_saved_h = h;
    g_saved_valid = 1;
}

static void cursor_restore_bg(void) {
    if (!g_saved_valid) return;
    for (uint32_t row = 0; row < g_saved_h; row++) {
        for (uint32_t col = 0; col < g_saved_w; col++) {
            fb_put_pixel(g_saved_x + (int32_t)col, g_saved_y + (int32_t)row,
                         g_saved_bg[row * g_saved_w + col]);
        }
    }
    g_saved_valid = 0;
}

 
static inline uint8_t blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    return (uint8_t)(((uint32_t)src * a + (uint32_t)dst * (255 - a)) / 255);
}

 




static void cursor_draw(int32_t x, int32_t y) {
    if (g_cursor_loaded && g_cursor.pixels) {
        uint32_t w = g_cursor.width, h = g_cursor.height;
        cursor_save_bg(x, y, w, h);

        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++) {
                const uint8_t *px = g_cursor.pixels + (((size_t)row * w + col) * 4);
                uint8_t r = px[0], g = px[1], b = px[2], a = px[3];
                if (a == 0) continue;  

                int32_t sx = x + (int32_t)col;
                int32_t sy = y + (int32_t)row;

                if (a == 255) {
                    fb_put_pixel(sx, sy, fb_rgb(r, g, b));
                } else {
                    fb_color_t bg = fb_get_pixel(sx, sy);
                    uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
                    uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
                    uint8_t bb = (uint8_t)(bg & 0xFF);
                    fb_color_t mixed = fb_rgb(blend_chan(r, br, a),
                                               blend_chan(g, bgc, a),
                                               blend_chan(b, bb, a));
                    fb_put_pixel(sx, sy, mixed);
                }
            }
        }
    } else {
         
        cursor_save_bg(x, y, FALLBACK_W, FALLBACK_H);
        for (int row = 0; row < FALLBACK_H; row++) {
            for (int col = 0; col < FALLBACK_W; col++) {
                char c = g_fallback_arrow[row][col];
                if (c == '#') fb_put_pixel(x + col, y + row, FB_BLACK);
                else if (c == '.') fb_put_pixel(x + col, y + row, FB_WHITE);
                 
            }
        }
    }
}

 






void ui_tick(void) {
    if (fb_get_mode() != FB_MODE_LINEAR) return;

     












     


    int32_t old_x = g_saved_x, old_y = g_saved_y;
    uint32_t old_w = g_saved_w, old_h = g_saved_h;
    int had_old = g_saved_valid;

    cursor_restore_bg();

     


    uint8_t buttons = ps2_mouse_buttons() | usb_hid_mouse_buttons();
    bool lmb_down = (buttons & 1) != 0;

     











    static int s_release_grace = 0;
    static bool s_prev_lmb_down = false;
    if (s_prev_lmb_down && !lmb_down) s_release_grace = 3;
    s_prev_lmb_down = lmb_down;

     


    int big_change = lmb_down || (s_release_grace > 0);
    if (s_release_grace > 0) s_release_grace--;

    ui_desktop_handle_mouse(g_mouse_x, g_mouse_y, buttons);

    cursor_draw(g_mouse_x, g_mouse_y);

     

    int32_t new_x = g_saved_x, new_y = g_saved_y;
    uint32_t new_w = g_saved_w, new_h = g_saved_h;

    int clock_drawn = ui_clock_tick();

     









    if (big_change) {
        fb_flip();
         






    } else {
         
        int32_t ux = old_x, uy = old_y, uw2 = (int32_t)old_w, uh2 = (int32_t)old_h;
        if (had_old) {
            int32_t ox2 = old_x + (int32_t)old_w, oy2 = old_y + (int32_t)old_h;
            int32_t nx2 = new_x + (int32_t)new_w, ny2 = new_y + (int32_t)new_h;
            int32_t left   = old_x < new_x ? old_x : new_x;
            int32_t top    = old_y < new_y ? old_y : new_y;
            int32_t right  = ox2 > nx2 ? ox2 : nx2;
            int32_t bottom = oy2 > ny2 ? oy2 : ny2;
            ux = left; uy = top; uw2 = right - left; uh2 = bottom - top;
        } else {
            ux = new_x; uy = new_y; uw2 = (int32_t)new_w; uh2 = (int32_t)new_h;
        }
        if (uw2 > 0 && uh2 > 0)
            fb_flip_rect(ux, uy, (uint32_t)uw2, (uint32_t)uh2);

        if (clock_drawn)
            fb_flip_clock_area();

         
































        #define USERSPACE_WIN_FLIP_EVERY 4u
        static uint32_t s_win_flip_counter = 0;
         










        static uint64_t s_flip_next_tsc = 0;
        if ((s_win_flip_counter++ % USERSPACE_WIN_FLIP_EVERY) == 0) {
            uint32_t lo, hi;
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            uint64_t now = ((uint64_t)hi << 32) | lo;
            if (now >= s_flip_next_tsc) {
                ui_flip_userspace_windows();
                __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
                uint64_t end = ((uint64_t)hi << 32) | lo;
                uint64_t cost = end - now;
                s_flip_next_tsc = end + cost * 9ULL;
                static uint32_t s_dbg_slow = 0;
                if (cost > 30000000ULL && s_dbg_slow < 5) {    
                    s_dbg_slow++;
                    DBG_VAL("UI", "ui_flip_userspace_windows SLOW, tsc cycles", cost);
                }
            }
        }
    }
}
