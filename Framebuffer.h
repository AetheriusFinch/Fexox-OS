/* Fexox OS Project, EULA GPLv2 */
 

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


#define FB_MODE_NONE    0    
#define FB_MODE_VGA     1    
#define FB_MODE_LINEAR  2    

 





typedef struct {
    uint8_t  mode;           
    uint32_t width;          
    uint32_t height;         
    uint32_t pitch;          
    uint8_t  bpp;            
    uint64_t phys_addr;      
     
    uint32_t red_mask;       
    uint32_t green_mask;     
    uint32_t blue_mask;      
} fb_info_t;


typedef uint32_t fb_color_t;    

#define FB_BLACK        0x000000U
#define FB_WHITE        0xFFFFFFU
#define FB_RED          0xFF0000U
#define FB_GREEN        0x00FF00U
#define FB_BLUE         0x0000FFU
#define FB_CYAN         0x00FFFFU
#define FB_MAGENTA      0xFF00FFU
#define FB_YELLOW       0xFFFF00U
#define FB_GRAY         0x808080U
#define FB_DARK_GRAY    0x404040U
#define FB_LIGHT_GRAY   0xC0C0C0U
#define FB_ORANGE       0xFF8000U
#define FB_DARK_BLUE    0x000080U
#define FB_DARK_GREEN   0x008000U

 
static inline fb_color_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((fb_color_t)r << 16) | ((fb_color_t)g << 8) | b;
}

 


typedef struct {
    int32_t x, y;
    uint32_t w, h;
} fb_rect_t;



int fb_init(const fb_info_t *info);

 
int fb_get_mode(void);

 
uint32_t fb_width(void);
uint32_t fb_height(void);

 





uint64_t fb_phys_addr(void);
uint32_t fb_pitch(void);
uint8_t  fb_bpp(void);

 




void fb_get_channel_shifts(uint8_t *r_shift, uint8_t *g_shift, uint8_t *b_shift);

 



 
void fb_put_pixel(int32_t x, int32_t y, fb_color_t color);

 
fb_color_t fb_get_pixel(int32_t x, int32_t y);


 
void fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                  fb_color_t color);

 
void fb_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                  fb_color_t color, uint32_t thickness);

 
void fb_hline(int32_t x, int32_t y, uint32_t len, fb_color_t color);

 
void fb_vline(int32_t x, int32_t y, uint32_t len, fb_color_t color);

 
void fb_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
             fb_color_t color);

 
void fb_clear(fb_color_t color);

 
void fb_blit(int32_t dst_x, int32_t dst_y,
             int32_t src_x, int32_t src_y,
             uint32_t w, uint32_t h);

#define FB_TRANSPARENT_BG 0xFF000000U    

void fb_draw_char(int32_t x, int32_t y, char c,
                  fb_color_t fg, fb_color_t bg);

 




int32_t fb_draw_string(int32_t x, int32_t y, const char *str,
                       fb_color_t fg, fb_color_t bg);

 
#define FB_FONT_W   8
#define FB_FONT_H   16

static inline uint32_t fb_text_width(const char *str) {
    uint32_t n = 0;
    while (*str++) n++;
    return n * FB_FONT_W;
}


 
void fbcon_init(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                fb_color_t fg, fb_color_t bg);

 
void fbcon_puts(const char *str);

 
void fbcon_putc(char c);

 
void fbcon_clear(void);

 
void fbcon_set_colors(fb_color_t fg, fb_color_t bg);


int fb_enable_double_buffer(void);

 
void fb_disable_double_buffer(void);

 
void fb_flip(void);

void fb_flip_rect(int32_t x, int32_t y, uint32_t w, uint32_t h);

 

void fb_flip_clock_area(void);

 
void *fb_back_ptr(void);

 


void ui_flip_userspace_windows(void);

 



 
#define FB_IN_BOUNDS(x, y) \
    ((uint32_t)(x) < fb_width() && (uint32_t)(y) < fb_height())



void ui_draw_desktop(void);

 

void ui_redraw_clock(void);

 


int ui_clock_tick(void);



void ui_desktop_handle_mouse(int32_t x, int32_t y, uint8_t buttons);



#define UI_WIN_TITLE_MAX   64
#define UI_WIN_INVALID     (-1)


int ui_win_create(const char *title, uint32_t w, uint32_t h, int32_t owner_pid);

 
int ui_win_close(int win_id);

 
int ui_win_move(int win_id, int32_t x, int32_t y);



bool ui_win_get_rect(int win_id, int32_t *out_x, int32_t *out_y,
                     uint32_t *out_w, uint32_t *out_h, uint32_t *out_redraw_gen);

 


void ui_win_close_all_owned_by(int32_t owner_pid);

#endif  
