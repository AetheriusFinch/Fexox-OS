/* Fexox OS Project, EULA GPLv2 */
/*
 * BootSplash - black boot screen: UI/Load.png in the centre, a thin rounded white bar below it
 * with a segment that runs to the right in a loop.  All boot text is collected silently.
 * On an error (no root filesystem, FATAL / ERR / FAIL lines, a kernel panic) the splash is
 * replaced by a full text log in the top-left corner (errors red, warnings yellow, OK green).
 *
 * The splash draws straight into the physical framebuffer, so it keeps animating while the
 * desktop is being rendered into the back surface.
 */
#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include <stdint.h>
#include <stdbool.h>

int  splash_init(void);                     /* after fb_init(); 0 = splash is up, <0 = use the old console */
bool splash_enabled(void);                  /* splash module is in charge of boot text */
bool splash_active(void);                   /* the animation screen is showing */
bool splash_error_mode(void);               /* the log screen is showing */

void splash_log(const char *text);          /* ALL boot text goes here (kept; shown only in error mode) */
void splash_tick(void);                     /* advance the animation (cheap, rate-limited, call anywhere) */
void splash_attach_timer(void);             /* drive it from the 100 Hz timer interrupt (call after interrupts are on) */
bool splash_set_image(const char *path);    /* load a PNG and show it above the bar; false = not found / bad */
void splash_error(const char *reason);      /* force the log screen */
void splash_hold_errors(uint32_t ms);       /* if the log screen is up: keep it for `ms`, then return */
void splash_finish(void);                   /* boot is done, the desktop takes over */

#endif
