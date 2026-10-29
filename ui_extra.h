/* Fexox OS Project, EULA GPLv2 */
 


#ifndef UI_EXTRA_H
#define UI_EXTRA_H

#include <stdint.h>

 


int ui_extra_init(void);

 
void ui_set_mouse_pos(int32_t x, int32_t y);

 


void ui_get_mouse_pos(int32_t *out_x, int32_t *out_y);

 

void ui_tick(void);

#endif  
