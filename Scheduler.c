/* Fexox OS Project, EULA GPLv2 */
 

#include "Scheduler.h"
#include "debug_out.h"

 
#ifndef FEXOX_OS_SCHED_VERBOSE
#define FEXOX_OS_SCHED_VERBOSE 0
#endif

 




#define FEXOX_OS_KSTACK_SIZE (64u * 1024u)
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 
extern void dbg_puts(const char *s);
extern void dbg_hex64(uint64_t v);

 

extern void  *kmalloc(uint64_t size);
extern void   kfree(void *ptr);
extern void   pmm_free_frames(uint64_t pa, uint64_t count);
extern uint64_t vmm_kernel_pml4_phys(void);
extern void   interrupt_set_scheduler_tick(uint64_t (*fn)(uint64_t frame_rsp));

static uint64_t g_active_pml4_phys = 0;



typedef volatile int spinlock_t;

static inline void spinlock_acquire(spinlock_t *l) {
     












    uint64_t flags;
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r"(flags)
        :
        : "memory"
    );
    *l = (int)flags;
}

static inline void spinlock_release(spinlock_t *l) {
     



    if (*l & 0x200) {
        __asm__ volatile ("sti" ::: "memory");
    }
}

 
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

 
static uint64_t g_tsc_per_ns = 3;  

static inline uint64_t tsc_to_ns(uint64_t tsc) {
    return tsc / g_tsc_per_ns;
}

 




static const uint32_t prio_to_weight[40] = {
       88761, 71755, 56483, 46273, 36291,
       29154, 23254, 18705, 14949, 11916,
        9548,  7620,  6100,  4904,  3906,
        3121,  2501,  1991,  1586,  1277,
        1024,   820,   655,   526,   423,
         335,   272,   215,   172,   137,
         110,    87,    70,    56,    45,
          36,    29,    23,    18,    15,
};

static inline uint32_t prio_to_w(int prio) {
    if (prio < SCHED_PRIO_MIN) prio = SCHED_PRIO_MIN;
    if (prio > SCHED_PRIO_MAX) prio = SCHED_PRIO_MAX;
    return prio_to_weight[prio + 20];
}

 



typedef struct {
    task_t  *data[SCHED_MAX_TASKS];
    int      size;
} run_queue_t;

static inline void heap_swap(run_queue_t *h, int a, int b) {
    task_t *tmp = h->data[a];
    h->data[a] = h->data[b];
    h->data[b] = tmp;
    h->data[a]->heap_idx = a;
    h->data[b]->heap_idx = b;
}

static void heap_up(run_queue_t *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[parent]->vruntime <= h->data[i]->vruntime)
            break;
        heap_swap(h, parent, i);
        i = parent;
    }
}

static void heap_down(run_queue_t *h, int i) {
    while (1) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < h->size && h->data[l]->vruntime < h->data[smallest]->vruntime)
            smallest = l;
        if (r < h->size && h->data[r]->vruntime < h->data[smallest]->vruntime)
            smallest = r;
        if (smallest == i) break;
        heap_swap(h, i, smallest);
        i = smallest;
    }
}

static void rq_push(run_queue_t *h, task_t *t) {
    if (h->size >= SCHED_MAX_TASKS) return;
    int i = h->size++;
    h->data[i] = t;
    t->heap_idx = i;
    heap_up(h, i);
}

static task_t *rq_pop(run_queue_t *h) {
    if (h->size == 0) return NULL;
    task_t *top = h->data[0];
    top->heap_idx = -1;
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];
        h->data[0]->heap_idx = 0;
        heap_down(h, 0);
    }
    return top;
}

__attribute__((unused)) static void rq_remove(run_queue_t *h, task_t *t) {
    int i = t->heap_idx;
    if (i < 0 || i >= h->size) return;
    t->heap_idx = -1;
    h->size--;
    if (i < h->size) {
        h->data[i] = h->data[h->size];
        h->data[i]->heap_idx = i;
        heap_up(h, i);
        heap_down(h, i);
    }
}

static inline uint64_t rq_min_vruntime(run_queue_t *h) {
    return h->size > 0 ? h->data[0]->vruntime : 0;
}

 


typedef struct {
    run_queue_t  rq;                 
    task_t      *current;            
    task_t      *idle;               

     
    task_t      *all_head;
    uint32_t     task_count;

     
    uint32_t     next_tid;

     

    uint64_t     min_vruntime;

     
    spinlock_t lock;

     
    bool         ready;

     
    uint64_t     slice_start_tsc;

     






    volatile bool yield_req;
} scheduler_t;

static scheduler_t g_sched;

 


static void kmemset_s(void *dst, uint8_t val, uint64_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void kstrncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

 
static void update_min_vruntime(void) {
    uint64_t mv = g_sched.min_vruntime;
    if (g_sched.current && g_sched.current != g_sched.idle)
        mv = g_sched.current->vruntime;
    if (g_sched.rq.size > 0) {
        uint64_t rq_min = rq_min_vruntime(&g_sched.rq);
        if (mv == 0 || rq_min < mv) mv = rq_min;
    }
    if (mv > g_sched.min_vruntime)
        g_sched.min_vruntime = mv;
}

 
__attribute__((unused)) static task_t *pick_next(void) {
    if (g_sched.rq.size > 0)
        return g_sched.rq.data[0];
    return g_sched.idle;
}

 


static void idle_fn(void *arg) {
    (void)arg;
    while (1)
        __asm__ volatile ("hlt");
}

 


static task_t *alloc_task(void) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return NULL;
    kmemset_s(t, 0, sizeof(task_t));
    t->heap_idx = -1;
    return t;
}

 
















 
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} sched_frame_t;

 
static void task_wrapper(void (*fn)(void *), void *arg) {
     







    dbg_putc('W');    

     




    DBG_MSG("SC", "task_wrapper: entered");
    fn(arg);
    sched_exit();
}

static int setup_kstack(task_t *t, void (*fn)(void *), void *arg) {
    DBG_VAL("SC", "setup_kstack: request size", t->kstack_size);
    uint8_t *stack = (uint8_t *)kmalloc(t->kstack_size);
    DBG_VAL("SC", "setup_kstack: kmalloc result", (uint64_t)(uintptr_t)stack);
    if (!stack) {
        DBG_MSG("SC", "setup_kstack: FAILED - kmalloc returned NULL");
        return -1;
    }
    DBG_VAL("SC", "setup_kstack: zeroing stack", (uint64_t)(uintptr_t)stack);
    kmemset_s(stack, 0, t->kstack_size);
    t->kstack_base = stack;

     





    uint8_t *stack_top = stack + t->kstack_size;
    stack_top = (uint8_t *)((uint64_t)stack_top & ~0xFULL);

     










    uint8_t *top = stack_top - sizeof(sched_frame_t);
    sched_frame_t *frame = (sched_frame_t *)top;
    kmemset_s(frame, 0, sizeof(sched_frame_t));

     
    frame->rip    = (uint64_t)(uintptr_t)task_wrapper;
    frame->cs     = 0x08;                   
    frame->rflags = 0x202;                  
    frame->rsp    = (uint64_t)(uintptr_t)stack_top;
    frame->ss     = 0x10;                   

     

    frame->rdi = (uint64_t)(uintptr_t)fn;
    frame->rsi = (uint64_t)(uintptr_t)arg;

    t->ctx.rsp = (uint64_t)(uintptr_t)top;          
    t->ctx.cr3 = 0;  
    t->kstack_top = (uint64_t)(uintptr_t)stack_top;  
    return 0;
}

 


int sched_init(void) {
    dbg_puts("[SC] sched_init: START\n");
    
    kmemset_s(&g_sched, 0, sizeof(g_sched));
    g_sched.next_tid = 1;
    g_sched.lock     = 0;
    g_sched.ready    = false;

    dbg_puts("[SC] sched_init: calling kmalloc for idle\n");
     






    task_t *idle = alloc_task();
    dbg_puts("[SC] sched_init: kmalloc result=");
    dbg_hex64((uint64_t)(uintptr_t)idle);
    dbg_puts("\n");
    
    if (!idle) return -1;

    idle->tid      = 0;
    idle->state    = TASK_RUNNABLE;
    idle->priority = SCHED_PRIO_MAX;
    idle->weight   = prio_to_w(SCHED_PRIO_MAX);
    idle->vruntime = 0;
    idle->kstack_size = FEXOX_OS_KSTACK_SIZE;
    kstrncpy(idle->name, "idle", sizeof(idle->name));

    if (setup_kstack(idle, idle_fn, NULL) != 0) {
        kfree(idle);
        return -1;
    }

    g_sched.idle     = idle;
    g_sched.current  = idle;
    g_sched.all_head = idle;
    g_sched.task_count = 1;

    interrupt_set_scheduler_tick(sched_tick);

    g_sched.ready = true;
    return 0;
}

 


int sched_create_kthread(void (*fn)(void *), void *arg,
                         const char *name, int prio) {
    if (!fn) return -1;

    task_t *t = alloc_task();
    if (!t) return -1;

    spinlock_acquire(&g_sched.lock);

    t->tid       = g_sched.next_tid++;
    t->state     = TASK_RUNNABLE;
    t->priority  = prio;
    t->weight    = prio_to_w(prio);
    t->kstack_size = FEXOX_OS_KSTACK_SIZE;
    kstrncpy(t->name, name ? name : "kthread", sizeof(t->name));

     
    update_min_vruntime();
    t->vruntime = g_sched.min_vruntime;

    if (setup_kstack(t, fn, arg) != 0) {
        spinlock_release(&g_sched.lock);
        kfree(t);
        return -1;
    }

     
    t->next_all = g_sched.all_head;
    if (g_sched.all_head) g_sched.all_head->prev_all = t;
    g_sched.all_head = t;
    g_sched.task_count++;

     
    rq_push(&g_sched.rq, t);

    spinlock_release(&g_sched.lock);

    DBG_VAL("SC", "created tid", t->tid);
    return (int)t->tid;
}

 

























 







extern void tss_set_rsp0(uint64_t rsp0);
extern void syscall_set_kstack(uint64_t rsp0);   

static uint64_t context_switch(uint64_t frame_rsp) {
    task_t *prev = g_sched.current;
    uint64_t now_tsc = rdtsc();

     
    uint64_t delta_tsc = now_tsc - g_sched.slice_start_tsc;
    uint64_t delta_ns  = tsc_to_ns(delta_tsc);

     
    if (prev && prev != g_sched.idle) {
        uint64_t weighted = (delta_ns * NICE_0_WEIGHT) / prev->weight;
        prev->vruntime     += weighted;
        prev->total_runtime += delta_ns;
    }

    update_min_vruntime();

     
    if (prev) {
        prev->ctx.rsp = frame_rsp;
        if (prev->state == TASK_RUNNING) {
            prev->state = TASK_RUNNABLE;
            if (prev != g_sched.idle)
                rq_push(&g_sched.rq, prev);
        }
         
    }

     
    task_t *next = rq_pop(&g_sched.rq);
    if (!next) next = g_sched.idle;

    next->state   = TASK_RUNNING;
    next->switches++;
    g_sched.current = next;
    g_sched.slice_start_tsc = now_tsc;
    g_sched.yield_req = false;

     



















    {
        uint64_t want = next->pml4_phys ? next->pml4_phys : vmm_kernel_pml4_phys();
        if (want != g_active_pml4_phys) {
            __asm__ volatile ("mov %0, %%cr3" :: "r"(want) : "memory");
            g_active_pml4_phys = want;
        }
    }

     






















    if (prev && prev->state == TASK_ZOMBIE) {
        if (prev->kstack_base) {
            kfree(prev->kstack_base);
            prev->kstack_base = NULL;
        }
        if (prev->pml4_phys) {
            pmm_free_frames(prev->pml4_phys, 1);
            prev->pml4_phys = 0;
        }
        kfree(prev);
        prev = NULL;
    }

     


#if FEXOX_OS_SCHED_VERBOSE
    DBG_VAL("SC", "context_switch: next tid", (uint64_t)next->tid);
#endif

     










    tss_set_rsp0(next->kstack_top);
    syscall_set_kstack(next->kstack_top);

     


    return next->ctx.rsp;
}

 










int sched_kill(uint32_t tid) {
    spinlock_acquire(&g_sched.lock);
    task_t *t = sched_find(tid);
    if (!t || t->state == TASK_ZOMBIE) { spinlock_release(&g_sched.lock); return 0; }
    if (t == g_sched.idle) { spinlock_release(&g_sched.lock); return -1; }

    DBG_VAL("SC", "sched_kill: tid", (uint64_t)tid);
    DBG_VAL("SC", "sched_kill: state", (uint64_t)t->state);

     
    if (t->heap_idx >= 0) rq_remove(&g_sched.rq, t);

     
    if (t->prev_all) t->prev_all->next_all = t->next_all;
    if (t->next_all) t->next_all->prev_all = t->prev_all;
    if (g_sched.all_head == t) g_sched.all_head = t->next_all;
    g_sched.task_count--;

    if (t == g_sched.current) {
         

        t->state = TASK_ZOMBIE;
        g_sched.yield_req = true;
        spinlock_release(&g_sched.lock);
        return 1;
    }

    t->state = TASK_ZOMBIE;
    if (t->kstack_base) { kfree(t->kstack_base); t->kstack_base = NULL; }
    if (t->pml4_phys && t->pml4_phys != g_active_pml4_phys) {
        pmm_free_frames(t->pml4_phys, 1);
        t->pml4_phys = 0;
    }
    kfree(t);
    spinlock_release(&g_sched.lock);
    return 1;
}

 






uint64_t sched_tick(uint64_t frame_rsp) {
    if (!g_sched.ready) return frame_rsp;

     

    extern void ui_tick(void);
    ui_tick();
    extern void ui_exit_watchdog(void);    
    ui_exit_watchdog();
    extern void ps2_kbd_poll(void);        
    ps2_kbd_poll();

     


    {
        static uint32_t s_diag_tick = 0, s_diag_n = 0;
        if ((++s_diag_tick % 100) == 0 && s_diag_n < 12) {
            s_diag_n++;
            for (task_t *dt = g_sched.all_head; dt; dt = dt->next_all) {
                if (dt->tid == 0) continue;
                DBG_VAL("SCD", "task tid", dt->tid);
                DBG_VAL("SCD", "  state (0=run 1=rdy 2=sleep)", (uint64_t)dt->state);
                DBG_VAL("SCD", "  total_runtime_ns", dt->total_runtime);
                DBG_VAL("SCD", "  switches", dt->switches);
            }
        }
    }

     
    extern void fexox_os_isr_heartbeat(void);
    fexox_os_isr_heartbeat();

     









    extern void usb_hid_poll_all(void);
    static uint32_t s_usb_hid_tick = 0;
    if ((s_usb_hid_tick++ % 3) == 0) {
         


        uint64_t t0 = rdtsc();
        usb_hid_poll_all();
        uint64_t ns = tsc_to_ns(rdtsc() - t0);
        if (ns >= 20000000ULL)    
            DBG_VAL("SC", "usb_hid_poll_all SLOW, ns", ns);
    }

    spinlock_acquire(&g_sched.lock);

    task_t *cur = g_sched.current;

     
    uint64_t delta_tsc = rdtsc() - g_sched.slice_start_tsc;
    uint64_t delta_ns  = tsc_to_ns(delta_tsc);

    bool need_switch = g_sched.yield_req || (delta_ns >= SCHED_TIMESLICE_NS);

     
    if (!need_switch && g_sched.rq.size > 0 && cur != g_sched.idle) {
        task_t *best = g_sched.rq.data[0];
         
        uint64_t lag = (cur->vruntime > best->vruntime)
                     ? (cur->vruntime - best->vruntime) : 0;
        if (lag > SCHED_TIMESLICE_NS)
            need_switch = true;
    }

     
    if (cur == g_sched.idle && g_sched.rq.size > 0)
        need_switch = true;

    uint64_t resume_rsp = frame_rsp;
    if (need_switch)
        resume_rsp = context_switch(frame_rsp);

    spinlock_release(&g_sched.lock);
    return resume_rsp;
}

 


void sched_yield(void) {
     
    spinlock_acquire(&g_sched.lock);
    g_sched.yield_req = true;
    spinlock_release(&g_sched.lock);
     











    __asm__ volatile ("sti\n\thlt" ::: "memory");
}

 


void sched_exit(void) {
    spinlock_acquire(&g_sched.lock);

    task_t *t = g_sched.current;
    t->state = TASK_ZOMBIE;

     
    if (t->prev_all) t->prev_all->next_all = t->next_all;
    if (t->next_all) t->next_all->prev_all = t->prev_all;
    if (g_sched.all_head == t) g_sched.all_head = t->next_all;
    g_sched.task_count--;

    DBG_VAL("SC", "task exit tid", t->tid);

     
    g_sched.yield_req = true;
    spinlock_release(&g_sched.lock);

     



    while (1) __asm__ volatile ("sti\n\thlt" ::: "memory");
}

 


void sched_sleep_ns(uint64_t ns) {
    spinlock_acquire(&g_sched.lock);
    task_t *t = g_sched.current;
    t->state = TASK_SLEEPING;
     
     
    (void)ns;
    g_sched.yield_req = true;
    spinlock_release(&g_sched.lock);
     

    while (t->state == TASK_SLEEPING)
        __asm__ volatile ("sti\n\thlt" ::: "memory");
}

 


void sched_wake(uint32_t tid) {
    spinlock_acquire(&g_sched.lock);
    task_t *t = g_sched.all_head;
    while (t) {
        if (t->tid == tid && t->state == TASK_SLEEPING) {
            t->state = TASK_RUNNABLE;
             
            update_min_vruntime();
            if (t->vruntime < g_sched.min_vruntime)
                t->vruntime = g_sched.min_vruntime;
            rq_push(&g_sched.rq, t);
            break;
        }
        t = t->next_all;
    }
    spinlock_release(&g_sched.lock);
}

 


task_t *sched_current(void) { return g_sched.current; }

 





uint64_t sched_current_pml4_phys(void) {
    return g_sched.current ? g_sched.current->pml4_phys : 0;
}

task_t *sched_find(uint32_t tid) {
    task_t *t = g_sched.all_head;
    while (t) {
        if (t->tid == tid) return t;
        t = t->next_all;
    }
    return NULL;
}

 







bool sched_task_alive(uint32_t tid) {
    task_t *t = sched_find(tid);
    return t && t->state != TASK_ZOMBIE;
}

uint32_t sched_task_count(void) { return g_sched.task_count; }