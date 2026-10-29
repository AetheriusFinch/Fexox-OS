/* Fexox OS Project, EULA GPLv2 */
 
#ifndef DEBUG_OUT_H
#define DEBUG_OUT_H

#include <stdint.h>
#include <stddef.h>

 


#define FEXOX_OS_KLOG_SIZE (256u * 1024u)    
typedef void (*fexox_os_klog_hook_t)(char);
__attribute__((weak)) fexox_os_klog_hook_t fexox_os_klog_hook = 0;

static inline void dbg_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t dbg_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void dbg_putc(char c) {
     




    dbg_outb(0x3F8, (uint8_t)c);
    if (fexox_os_klog_hook) fexox_os_klog_hook(c);
}

 

static inline void dbg_put_ascii(const char *a) { while (*a) dbg_putc(*a++); }

static inline void dbg_puts(const char *s) {
    static const char *const lo[32] = {       
        "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p",
        "r","s","t","u","f","kh","ts","ch","sh","shch","","y","","e","yu","ya" };
    static const char *const up[32] = {       
        "A","B","V","G","D","E","Zh","Z","I","Y","K","L","M","N","O","P",
        "R","S","T","U","F","Kh","Ts","Ch","Sh","Shch","","Y","","E","Yu","Ya" };
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\n') { dbg_putc('\r'); dbg_putc('\n'); s++; continue; }
        if (c < 0x80) { dbg_putc((char)c); s++; continue; }

         
        unsigned char c1 = (unsigned char)s[1];
        if ((c == 0xD0 || c == 0xD1) && (c1 & 0xC0) == 0x80) {
            unsigned cp = ((unsigned)(c & 0x1Fu) << 6) | (unsigned)(c1 & 0x3Fu);
            if (cp >= 0x430 && cp <= 0x44F)      dbg_put_ascii(lo[cp - 0x430]);
            else if (cp >= 0x410 && cp <= 0x42F) dbg_put_ascii(up[cp - 0x410]);
            else if (cp == 0x451)                dbg_put_ascii("e");
            else if (cp == 0x401)                dbg_put_ascii("E");
            else                                 dbg_putc('?');
            s += 2; continue;
        }
        if (c == 0xE2 && (unsigned char)s[1] == 0x80 && s[2]) {    
            unsigned char c2 = (unsigned char)s[2];
            if (c2 == 0x94 || c2 == 0x93) dbg_putc('-');           
            else if (c2 == 0xA6)          dbg_put_ascii("...");    
            else if (c2 == 0x98 || c2 == 0x99) dbg_putc('\'');     
            else if (c2 == 0x9C || c2 == 0x9D) dbg_putc('"');      
            else                          dbg_putc('?');
            s += 3; continue;
        }
        if (c == 0xE2 && (unsigned char)s[1] == 0x86 && s[2]) {    
            unsigned char c2 = (unsigned char)s[2];
            dbg_put_ascii(c2 == 0x92 ? "->" : (c2 == 0x90 ? "<-" : "?"));
            s += 3; continue;
        }
         
        int skip = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        dbg_putc('?');
        while (skip-- > 0 && *s) s++;
    }
}

static inline void dbg_hex64(uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    dbg_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        dbg_putc(hex[(v >> (unsigned)i) & 0xF]);
}

static inline void dbg_dec64(uint64_t v) {
    char buf[24];
    int n = 0;
    if (v == 0) {
        dbg_putc('0');
        return;
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        dbg_putc(buf[--n]);
}

 
#define DBG_MSG(tag, msg) do { dbg_puts("[" tag "] " msg "\n"); } while(0)

 
#define DBG_VAL(tag, name, val) do { \
    dbg_puts("[" tag "] " name "="); \
    dbg_hex64((uint64_t)(val)); \
    dbg_puts("\n"); \
} while(0)

 
#define DBG_RC(tag, name, val) do { \
    int64_t _rc = (int64_t)(val); \
    dbg_puts("[" tag "] " name "="); \
    if (_rc < 0) { dbg_putc('-'); dbg_dec64((uint64_t)(-_rc)); } \
    else         { dbg_dec64((uint64_t)_rc); } \
    dbg_puts("\n"); \
} while(0)

 
#define DBG_PANIC(tag, msg) do { \
    dbg_puts("\n*** PANIC [" tag "] "); \
    dbg_puts(msg); \
    dbg_puts(" ***\n"); \
    for (;;) __asm__ volatile ("hlt"); \
} while(0)

#endif  
