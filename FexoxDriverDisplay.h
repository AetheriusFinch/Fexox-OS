/* Fexox OS Project, EULA GPLv2 */















#ifndef FEXOX_DRIVER_DISPLAY_H
#define FEXOX_DRIVER_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "Framebuffer.h"

#define FDD_ROT_0    0
#define FDD_ROT_90   1       
#define FDD_ROT_180  2
#define FDD_ROT_270  3

#define FDD_FILTER_NEAREST   0
#define FDD_FILTER_BILINEAR  1

#define FDD_FIT_LETTERBOX    0       
#define FDD_FIT_STRETCH      1       

#define FDD_MIN_W   640u
#define FDD_MIN_H   480u

 
int  fdd_init(const fb_info_t *info, uint8_t *front_virt);    
bool fdd_ready(void);                  



uint8_t *fdd_alloc_surface(void);
bool     fdd_active(void);             

uint8_t *fdd_surface(void);
uint32_t fdd_surface_pitch(void);      
uint64_t fdd_surface_bytes(void);      
void     fdd_surface_max(uint32_t *w, uint32_t *h);

 
int  fdd_set_mode(uint32_t logical_w, uint32_t logical_h, int rotation, int filter, int fit);
int  fdd_set_rotation(int rotation);
int  fdd_set_filter(int filter, int fit);

uint32_t fdd_logical_w(void);
uint32_t fdd_logical_h(void);
int      fdd_rotation(void);
int      fdd_filter(void);
int      fdd_fit(void);
void     fdd_phys_size(uint32_t *w, uint32_t *h);



void fdd_rotate_delta(int32_t *dx, int32_t *dy);

 
void fdd_present(void);
void fdd_present_rect(int32_t x, int32_t y, uint32_t w, uint32_t h);

 
int  fdd_standard_modes(uint32_t (*out)[2], int max);

#endif
