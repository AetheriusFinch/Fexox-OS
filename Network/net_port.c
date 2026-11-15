/* Fexox OS Project, EULA GPLv2 */
 



#include "net.h"
#include "Scheduler.h"

extern uint64_t usb_tsc_per_ms(void);         
extern void     usb_hotplug_poll(void);       
#include "debug_out.h"

static inline uint64_t np_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t np_now_ms(void) {
    static uint64_t tsc0 = 0;
    if (tsc0 == 0) tsc0 = np_rdtsc();
    uint64_t per = usb_tsc_per_ms();
    if (per == 0) per = 3000000ULL;
    return (np_rdtsc() - tsc0) / per;
}

uint64_t np_irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}

void np_irq_restore(uint64_t f) {
    __asm__ volatile ("pushq %0; popfq" :: "r"(f) : "memory", "cc");
}

void np_yield(void) { sched_yield(); }

void np_idle(void) {
     

    __asm__ volatile ("sti\n\thlt" ::: "memory");
}

int np_thread_create(void (*fn)(void *), void *arg, const char *name) {
    return sched_create_kthread(fn, arg, name, 0);
}

void np_log(const char *msg) { dbg_puts(msg); dbg_puts("\n"); }

uint64_t np_entropy(void) {
    uint64_t v = np_rdtsc();
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if ((c >> 30) & 1) {                           
        uint64_t r; uint8_t ok;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(r), "=qm"(ok) :: "cc");
        if (ok) v ^= r;
    }
    return v;
}

void np_usb_hotplug(void) { usb_hotplug_poll(); }
