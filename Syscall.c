/* Fexox OS Project, EULA GPLv2 */
 


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "Syscall.h"
#include "VFS.h"
#include "Scheduler.h"
#include "ELF64.h"
#include "debug_out.h"

 


extern void    *kmalloc(size_t size);
extern void     kfree(void *ptr);

 


extern int      fb_get_mode(void);
extern uint32_t fb_width(void);
extern uint32_t fb_height(void);
extern uint64_t fb_phys_addr(void);
extern uint32_t fb_pitch(void);
extern uint8_t  fb_bpp(void);
extern void    *fb_back_ptr(void);    



extern void     fb_get_channel_shifts(uint8_t *r_shift, uint8_t *g_shift, uint8_t *b_shift);
#define FB_MODE_LINEAR_ 2    

extern int  ui_win_create(const char *title, uint32_t w, uint32_t h, int32_t owner_pid);
extern int  ui_win_close(int win_id);
extern int  ui_win_move(int win_id, int32_t x, int32_t y);
extern void ui_win_close_all_owned_by(int32_t owner_pid);
extern int32_t ui_win_owner_pid(int win_id);
extern bool    ui_exit_requested(int32_t tid);
extern void    ui_exit_request_clear(int32_t tid);
extern bool ui_win_get_rect(int win_id, int32_t *out_x, int32_t *out_y,
                            uint32_t *out_w, uint32_t *out_h, uint32_t *out_redraw_gen);

 
#include "Network/net.h"

extern uint8_t ps2_mouse_buttons(void);
extern uint8_t usb_hid_mouse_buttons(void);           
extern bool    usb_hid_kbd_read_char(char *out_ch);   
extern bool    ui_kbd_focus_owned_by(int32_t pid);    
extern void    ui_get_mouse_pos(int32_t *out_x, int32_t *out_y);
extern uint64_t pmm_alloc_frames(size_t count, size_t align);
extern void     pmm_free_frames(uint64_t pa, size_t count);
extern int      vmm_map_user(uint64_t va, uint64_t pa, bool rw, bool exec);
extern int      vmm_map(uint64_t va, uint64_t pa, uint64_t flags);
extern int      vmm_unmap(uint64_t va);
extern uint64_t vmm_virt_to_phys(uint64_t va);   

extern uint64_t usb_tsc_per_ms(void);            
extern void     pmm_get_stats(uint64_t *total_frames, uint64_t *free_frames);    
extern uint64_t sched_current_pml4_phys(void);   
extern uint64_t vmm_kernel_pml4_phys(void);      
                           
extern int      vmm_map_user_ex(uint64_t va, uint64_t pa, bool rw, bool exec, int kind);
extern uint64_t vmm_fork_user_space(uint64_t src_pml4_phys);
extern void     vmm_destroy_user_space(uint64_t pml4_phys);
extern int      vmm_cow_ready(void);
extern int      vmm_cow_break(uint64_t va);
extern int      vmm_user_release_page(uint64_t va);
#define VMM_PAGE_OWNED    0
#define VMM_PAGE_SHARED   1
#define VMM_PAGE_FOREIGN  2
extern void     kmalloc_get_diag(uint32_t *fails, uint64_t *last_fail_size, uint64_t *heap_used);

 
extern void  *memset(void *dst, int c, size_t n);
extern void  *memcpy(void *dst, const void *src, size_t n);
extern char  *strncpy(char *dst, const char *src, size_t n);
extern size_t strlen(const char *s);
extern int    strcmp(const char *a, const char *b);
extern int    strncmp(const char *a, const char *b, size_t n);

 


#define PAGE_SIZE           4096ULL
#define PAGE_SHIFT          12
#define PAGE_MASK           (~(PAGE_SIZE - 1ULL))
#define ROUND_UP(n, a)      (((uint64_t)(n) + (uint64_t)(a) - 1ULL) & ~((uint64_t)(a) - 1ULL))
#define ROUND_DOWN(n, a)    ((uint64_t)(n) & ~((uint64_t)(a) - 1ULL))

 
#define USER_VIRT_LIMIT     0x00007FFFFFFFFFFFULL

 




 
#define USER_HEAP_BASE      0x0000000010000000ULL
#define USER_HEAP_MAX       0x0000000080000000ULL

 
#define USER_MMAP_BASE      0x0000000100000000ULL
#define USER_MMAP_LIMIT     0x0000300000000000ULL

 
#define VGA_TEXT_PHYS       0x000B8000ULL
#define VGA_TEXT_COLS       80
#define VGA_TEXT_ROWS       25

 
#define PTE_P   (1ULL << 0)    
#define PTE_W   (1ULL << 1)    
#define PTE_U   (1ULL << 3)    
#define PTE_NX  (1ULL << 63)   

 
#define DIRECT_MAP_OFFSET   0xFFFF880000000000ULL

 






#define SC_FD_MAX   256

typedef struct {
    bool    open;
    int     vfs_fd;     
    int     flags;      
    int     fd_flags;   
    bool    is_pipe_read;
    bool    is_pipe_write;
    int     pipe_idx;   
    uint32_t dir_pos;   
} sc_file_t;

                                                                         
                                                                           
                                                                         
                                                                         
                                                                            
#define SC_VMA_MAX        64
#define SC_VMA_ANON       1
#define SC_VMA_FILECOPY   2

typedef struct {
    bool     used;
    uint8_t  kind;
    bool     rw, exec;
    uint64_t va;
    uint64_t pa;                                                                                  
    uint64_t npages;
} sc_vma_t;

#define SC_PROC_MAX     256
#define SC_PROC_FREE    0
#define SC_PROC_RUN     1
#define SC_PROC_ZOMBIE  2
#define SC_NO_TID       0xFFFFFFFFu

typedef struct sc_proc {
    int      state;
    uint32_t pid, ppid, tid;                                                                
    int      exit_code;
    uint64_t cpu_ns;                                                
    uint64_t child_cpu_ns;                                                           
    uint64_t brk_current, brk_mapped, mmap_next;
    sc_vma_t vma[SC_VMA_MAX];
    char     cwd[VFS_MAX_PATH];
    sc_file_t files[SC_FD_MAX];

                                          
    int             term_sig;                                                          
    uint32_t        umask_val;
    uint64_t        sigmask, sigpending;                                               
    sc_ksigaction_t sigact[SC_NSIG];
    sc_stack_t      altstack;
    uint64_t        clear_child_tid;                                                       

                                                                                         
                                                                                        
                                                                                          
    struct sc_proc *leader;
    bool            exit_set;                                                                        
    uint32_t        vm_owner;                                                                          
} sc_proc_t;

static sc_proc_t *g_procs[SC_PROC_MAX];
static sc_proc_t  g_proc0;                                                  
static sc_proc_t *g_cur_cache     = NULL;
static uint32_t   g_cur_cache_tid = SC_NO_TID;

static sc_proc_t *sc_cur(void);
static sc_proc_t *sc_thr(void);
#define g_files       (sc_cur()->files)
#define g_vma         (sc_cur()->vma)
#define g_brk_current (sc_cur()->brk_current)
#define g_brk_mapped  (sc_cur()->brk_mapped)
#define g_mmap_next   (sc_cur()->mmap_next)
#define g_cwd         (sc_cur()->cwd)

static inline uint64_t sc_irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void sc_irq_restore(uint64_t f) {
    if (f & 0x200) __asm__ volatile ("sti" ::: "memory");
}

static bool sc_task_alive(uint32_t tid) {
    task_t *t = sched_find(tid);
    return t && t->state != TASK_ZOMBIE;
}

                                                                 
static bool sc_group_alive(const sc_proc_t *L) {
    if (sc_task_alive(L->tid)) return true;
    for (int i = 0; i < SC_PROC_MAX; i++) {
        const sc_proc_t *r = g_procs[i];
        if (r && r->leader == L && sc_task_alive(r->tid)) return true;
    }
    return false;
}
static bool sc_group_others_alive(const sc_proc_t *L, uint32_t my_tid) {
    if (L->tid != my_tid && sc_task_alive(L->tid)) return true;
    for (int i = 0; i < SC_PROC_MAX; i++) {
        const sc_proc_t *r = g_procs[i];
        if (r && r->leader == L && r->tid != my_tid && sc_task_alive(r->tid)) return true;
    }
    return false;
}

static void sc_proc_defaults(sc_proc_t *p) {
    memset(p, 0, sizeof(*p));
    p->state       = SC_PROC_RUN;
    p->brk_current = p->brk_mapped = USER_HEAP_BASE;
    p->mmap_next   = USER_MMAP_BASE;
    p->cwd[0]      = '/';
    p->umask_val   = 022;
    for (int i = 0; i < 3; i++) {
        p->files[i].open     = true;
        p->files[i].vfs_fd   = -1;
        p->files[i].pipe_idx = -1;
        p->files[i].flags    = (i == 0) ? O_RDONLY : O_WRONLY;
    }
}

static sc_proc_t *sc_proc_find_tid(uint32_t tid) {
    for (int i = 0; i < SC_PROC_MAX; i++)
        if (g_procs[i] && g_procs[i]->state != SC_PROC_FREE && g_procs[i]->tid == tid)
            return g_procs[i];
    return NULL;
}
static sc_proc_t *sc_proc_find_pid(uint32_t pid) {
    for (int i = 0; i < SC_PROC_MAX; i++)
        if (g_procs[i] && g_procs[i]->state != SC_PROC_FREE && !g_procs[i]->leader && g_procs[i]->pid == pid)
            return g_procs[i];
    return NULL;
}

static void sc_proc_free(sc_proc_t *p) {
    uint64_t fl = sc_irq_save();
    if (g_cur_cache == p) { g_cur_cache = NULL; g_cur_cache_tid = SC_NO_TID; }
    for (int i = 0; i < SC_PROC_MAX; i++) {
        if (g_procs[i] == p) { g_procs[i] = NULL; break; }
    }
    sc_irq_restore(fl);
    if (p != &g_proc0) kfree(p);
}

                                                                                 
static void sc_proc_gc(void) {
    for (int i = 0; i < SC_PROC_MAX; i++) {
        sc_proc_t *p = g_procs[i];
        if (!p || p->tid == SC_NO_TID || sc_task_alive(p->tid)) continue;
        if (p->leader) { sc_proc_free(p); continue; }                               
        if (sc_group_alive(p)) continue;                                                           
        sc_proc_t *par = p->ppid ? sc_proc_find_pid(p->ppid) : NULL;
        if (par && sc_task_alive(par->tid)) continue;                                      
        sc_proc_free(p);
    }
}

                                                                                       
static sc_proc_t *sc_proc_alloc(void) {
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < SC_PROC_MAX; i++) {
            if (g_procs[i]) continue;
            sc_proc_t *p = (sc_proc_t *)kmalloc(sizeof(sc_proc_t));
            if (!p) return NULL;
            memset(p, 0, sizeof(*p));
            p->state = SC_PROC_RUN;
            p->tid = SC_NO_TID;
            g_procs[i] = p;
            return p;
        }
        if (pass == 0) sc_proc_gc();
    }
    return NULL;
}

static sc_proc_t *sc_thr(void) {
    task_t  *t   = sched_current();
    uint32_t tid = t ? t->tid : 0;
    if (g_cur_cache && g_cur_cache_tid == tid) return g_cur_cache;
    sc_proc_t *p = sc_proc_find_tid(tid);
    if (!p) {                                                                                
        p = sc_proc_alloc();
        if (p) {
            sc_proc_defaults(p);
            p->pid = p->tid = tid;
        } else {
            p = &g_proc0;
            if (g_proc0.state == SC_PROC_FREE) { sc_proc_defaults(&g_proc0); g_proc0.pid = g_proc0.tid = 0; }
        }
    }
    uint64_t fl = sc_irq_save();
    g_cur_cache = p; g_cur_cache_tid = tid;
    sc_irq_restore(fl);
    return p;
}

                                                                                          
static sc_proc_t *sc_cur(void) {
    sc_proc_t *t = sc_thr();
    return t->leader ? t->leader : t;
}

static sc_file_t *sc_get_file(int fd);

 
static int sc_alloc_fd(void) {
     
    for (int i = 3; i < SC_FD_MAX; i++) {
        if (!g_files[i].open) return i;
    }
    return -1;
}

 
static sc_file_t *sc_get_file(int fd) {
    if (fd < 0 || fd >= SC_FD_MAX) return NULL;
    if (!g_files[fd].open)         return NULL;
    return &g_files[fd];
}

 


static pipe_t g_pipes[PIPE_MAX];

static int pipe_alloc(void) {
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!g_pipes[i].valid) {
            memset(&g_pipes[i], 0, sizeof(pipe_t));
            g_pipes[i].valid = true;
            return i;
        }
    }
    return -1;
}

 







static uint8_t g_vfs_refs[VFS_MAX_FD];

static void sc_release_file(sc_file_t *f) {
    if (f->is_pipe_read || f->is_pipe_write) {
        if (f->pipe_idx < 0 || f->pipe_idx >= PIPE_MAX) return;
        pipe_t *p = &g_pipes[f->pipe_idx];
        if (f->is_pipe_write) { if (p->wr_refs > 1) p->wr_refs--; else { p->wr_refs = 0; p->write_closed = true; } }
        if (f->is_pipe_read)  { if (p->rd_refs > 1) p->rd_refs--; else { p->rd_refs = 0; p->read_closed  = true; } }
        if (p->write_closed && p->read_closed) p->valid = false;
    } else if (f->vfs_fd >= 0) {
        if (f->vfs_fd < VFS_MAX_FD && g_vfs_refs[f->vfs_fd] > 1) {
            g_vfs_refs[f->vfs_fd]--;
        } else {
            if (f->vfs_fd < VFS_MAX_FD) g_vfs_refs[f->vfs_fd] = 0;
            vfs_close(f->vfs_fd);
        }
    }
}

 

static int sc_alloc_fd_from(int min_fd) {
    if (min_fd < 0) min_fd = 0;
    for (int i = min_fd; i < SC_FD_MAX; i++)
        if (!g_files[i].open) return i;
    return -1;
}

 
static void sc_dup_into(int new_fd, const sc_file_t *src) {
    g_files[new_fd] = *src;
    g_files[new_fd].fd_flags = 0;               
    if (src->is_pipe_read || src->is_pipe_write) {
        pipe_t *p = &g_pipes[src->pipe_idx];
        if (src->is_pipe_write && p->wr_refs < 255) p->wr_refs++;
        if (src->is_pipe_read  && p->rd_refs < 255) p->rd_refs++;
    } else if (src->vfs_fd >= 0 && src->vfs_fd < VFS_MAX_FD) {
        if (g_vfs_refs[src->vfs_fd] < 255) g_vfs_refs[src->vfs_fd]++;
    }
}

 


static shm_region_t g_shm[SHM_MAX_REGIONS];

static int shm_find_by_name(const char *name) {
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (g_shm[i].valid && strcmp(g_shm[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int shm_alloc_slot(void) {
    for (int i = 0; i < SHM_MAX_REGIONS; i++) {
        if (!g_shm[i].valid) return i;
    }
    return -1;
}

 




static uint64_t mmap_bump_alloc(uint64_t size) {
    uint64_t addr = g_mmap_next;
    if (addr + size > USER_MMAP_LIMIT) return (uint64_t)-1;
    g_mmap_next = ROUND_UP(addr + size, PAGE_SIZE);
    return addr;
}

 












static void sc_vma_record(uint8_t kind, uint64_t va, uint64_t pa, uint64_t npages, bool rw, bool exec) {
    for (int i = 0; i < SC_VMA_MAX; i++) {
        if (!g_vma[i].used) {
            g_vma[i].used = true; g_vma[i].kind = kind;
            g_vma[i].va = va; g_vma[i].pa = pa; g_vma[i].npages = npages;
            g_vma[i].rw = rw; g_vma[i].exec = exec;
            return;
        }
    }
     
}

static sc_vma_t *sc_vma_containing(uint64_t va) {
    for (int i = 0; i < SC_VMA_MAX; i++) {
        if (g_vma[i].used && va >= g_vma[i].va &&
            va < g_vma[i].va + (g_vma[i].npages << PAGE_SHIFT))
            return &g_vma[i];
    }
    return NULL;
}

#define SC_PTE_ADDR   0x000FFFFFFFFFF000ULL
#define SC_PTE_PS     (1ULL << 7)

static uint64_t *sc_pt_virt(uint64_t phys) {
    return (uint64_t *)(uintptr_t)(phys + DIRECT_MAP_OFFSET);
}

static uint64_t sc_cur_pml4(void) {
    uint64_t c = sched_current_pml4_phys();
    return c ? c : vmm_kernel_pml4_phys();
}

 

static uint64_t *sc_find_pte(uint64_t va) {
    uint64_t *t = sc_pt_virt(sc_cur_pml4());
    uint64_t e = t[(va >> 39) & 511];
    if (!(e & PTE_P)) return NULL;
    t = sc_pt_virt(e & SC_PTE_ADDR);
    e = t[(va >> 30) & 511];
    if (!(e & PTE_P) || (e & SC_PTE_PS)) return NULL;
    t = sc_pt_virt(e & SC_PTE_ADDR);
    e = t[(va >> 21) & 511];
    if (!(e & PTE_P) || (e & SC_PTE_PS)) return NULL;
    t = sc_pt_virt(e & SC_PTE_ADDR);
    return &t[(va >> 12) & 511];
}

 
static bool sc_user_page_present(uint64_t va, uint64_t *pa_out) {
    uint64_t *pte = sc_find_pte(va & PAGE_MASK);
    if (!pte || !(*pte & PTE_P)) return false;
    if (pa_out) *pa_out = *pte & SC_PTE_ADDR;
    return true;
}

 
static bool sc_user_unmap_page(uint64_t va) {
    uint64_t *pte = sc_find_pte(va & PAGE_MASK);
    if (!pte || !(*pte & PTE_P)) return false;
    *pte = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(va & PAGE_MASK) : "memory");
    return true;
}

 



 


bool syscall_validate_ptr(const void *ptr, size_t size) {
    uint64_t va = (uint64_t)(uintptr_t)ptr;
    if (va == 0) return false;
    if (va > USER_VIRT_LIMIT) return false;
    if (size > 0 && (va + size - 1) > USER_VIRT_LIMIT) return false;
    return true;
}

 

static int64_t sc_copy_string(const char *user_ptr, char *kbuf, size_t kbuf_size) {
    if (!syscall_validate_ptr(user_ptr, 1)) return -1;
    size_t i = 0;
    const char *p = user_ptr;
    while (i < kbuf_size - 1) {
         

        kbuf[i] = p[i];
        if (p[i] == '\0') return (int64_t)i;
        i++;
    }
    kbuf[i] = '\0';
    return (int64_t)i;
}

 



 



                                                                             
                                                                           
                                                                             
static int sc_copy_path(const char *user_ptr, char *kbuf, size_t kbuf_size) {
    if (!syscall_validate_ptr(user_ptr, 1)) return -1;
    char *tmp = (char *)kmalloc(2 * VFS_MAX_PATH);
    if (!tmp) return -1;
    char *raw = tmp + VFS_MAX_PATH;
    size_t i = 0;
    for (;;) {                                                                          
        char ch = user_ptr[i];
        if (i >= VFS_MAX_PATH - 1) { kfree(tmp); return -1; }
        raw[i] = ch;
        if (ch == '\0') break;
        i++;
    }
    size_t n = 0;
    if (raw[0] != '/') {
        const char *cwd = g_cwd;
        size_t cl = strlen(cwd);
        if (cl + 1 + i + 1 > VFS_MAX_PATH) { kfree(tmp); return -1; }
        memcpy(tmp, cwd, cl); n = cl;
        if (n == 0 || tmp[n - 1] != '/') tmp[n++] = '/';
    }
    memcpy(tmp + n, raw, i + 1);

    size_t o = 0;
    if (kbuf_size < 2) { kfree(tmp); return -1; }
    kbuf[o++] = '/';
    const char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *c = p;
        while (*p && *p != '/') p++;
        size_t l = (size_t)(p - c);
        if (l == 1 && c[0] == '.') continue;
        if (l == 2 && c[0] == '.' && c[1] == '.') {
            while (o > 1 && kbuf[o - 1] != '/') o--;
            if (o > 1) o--;
            continue;
        }
        if (o + l + 2 > kbuf_size) { kfree(tmp); return -1; }
        if (o > 1) kbuf[o++] = '/';
        memcpy(kbuf + o, c, l); o += l;
    }
    kbuf[o] = '\0';
    kfree(tmp);
    return (int)o;
}

static void    sc_raise_self(int sig);
static void    sc_futex_cleanup(uint32_t pid);
static void    sc_exit_clear_tid(sc_proc_t *p);
static int64_t sc_vfs_errno(int r);

                                                                         
static void sc_files_addref(sc_proc_t *p) {
    for (int fd = 0; fd < SC_FD_MAX; fd++) {
        sc_file_t *f = &p->files[fd];
        if (!f->open) continue;
        if (f->is_pipe_read || f->is_pipe_write) {
            if (f->pipe_idx < 0 || f->pipe_idx >= PIPE_MAX) continue;
            pipe_t *pp = &g_pipes[f->pipe_idx];
            if (f->is_pipe_write && pp->wr_refs < 255) pp->wr_refs++;
            if (f->is_pipe_read  && pp->rd_refs < 255) pp->rd_refs++;
        } else if (f->vfs_fd >= 0 && f->vfs_fd < VFS_MAX_FD) {
            if (g_vfs_refs[f->vfs_fd] < 255) g_vfs_refs[f->vfs_fd]++;
        }
    }
}
static void sc_files_close_all(sc_proc_t *p) {
    for (int fd = 0; fd < SC_FD_MAX; fd++) {
        if (!p->files[fd].open) continue;
        sc_release_file(&p->files[fd]);
        p->files[fd].open = false;
    }
}
                                                                                       
static void sc_group_kill_others(sc_proc_t *L, uint32_t my_tid) {
    for (int i = 0; i < SC_PROC_MAX; i++) {
        sc_proc_t *r = g_procs[i];
        if (!r || (r != L && r->leader != L)) continue;
        if (r->tid == my_tid || r->tid == SC_NO_TID || !sc_task_alive(r->tid)) continue;
        ui_win_close_all_owned_by((int32_t)r->tid);
        sc_futex_cleanup(r->tid);
        sched_kill(r->tid);
    }
}

                                                                               
                                                                  
static void sc_proc_on_exit(uint64_t code) {
    sc_proc_t *th = sc_thr();
    sc_proc_t *p  = sc_cur();
    task_t *t = sched_current();
    uint32_t my_tid = t ? t->tid : th->tid;
    p->cpu_ns += t ? t->total_runtime : 0;
    sc_exit_clear_tid(th);                                           
    sc_futex_cleanup(my_tid);
    if (th != p) th->state = SC_PROC_ZOMBIE;
    if (!sc_group_others_alive(p, my_tid)) {
        if (!p->exit_set) p->exit_code = (int)(code & 0xFF);
        sc_files_close_all(p);
        for (int i = 0; i < SC_PROC_MAX; i++)
            if (g_procs[i] && g_procs[i] != p && !g_procs[i]->leader && g_procs[i]->ppid == p->pid) g_procs[i]->ppid = 0;
        p->state = SC_PROC_ZOMBIE;
    }
}

static int64_t sc_exit(uint64_t code);

 

static inline void sc_check_exit_request(void) {
    task_t *t = sched_current();
    if (t && ui_exit_requested((int32_t)t->tid)) {
        ui_exit_request_clear((int32_t)t->tid);
        DBG_MSG("SC", "window closed by user -> terminating process");
        sc_exit(0);
    }
}

static int64_t sc_exit(uint64_t code) {
    DBG_VAL("SC", "sys_exit code", code);

    task_t *t = sched_current();
    if (t) {
        ui_exit_request_clear((int32_t)t->tid);    
        ui_win_close_all_owned_by((int32_t)t->tid);
    }

    sc_proc_on_exit(code);
    sched_exit();             
    __builtin_unreachable();
}

                                                         
static int64_t sc_exit_group(uint64_t code) {
    sc_proc_t *p = sc_cur();
    task_t *t = sched_current();
    p->exit_code = (int)(code & 0xFF);
    p->exit_set  = true;
    sc_group_kill_others(p, t ? t->tid : SC_NO_TID);
    return sc_exit(code);
}

 


static int64_t sc_getpid(void) {
    task_t *t = sched_current();
    if (!t) return 1;
    return (int64_t)sc_cur()->pid;
}
static int64_t sc_getppid(void) { return (int64_t)sc_cur()->ppid; }
static int64_t sc_gettid(void) {
    task_t *t = sched_current();
    return t ? (int64_t)t->tid : 1;
}

 


static int64_t sc_sched_yield(void) {
    sched_yield();
    return 0;
}

 












                                                                       
                                                                            
                                                                           
                                                               
                                                                          
                                                         
                                                                                      
typedef struct {
    uint64_t rip, rsp, rflags, rbx, rbp, r12, r13, r14, r15, fs_base;
    uint64_t r8, r9, r10, rdx, rsi, rdi;                                                                       
    uint64_t set_tid_ptr;                                            
} fork_ctx_t;

#define SC_MSR_FS_BASE 0xC0000100U

static void fork_child_trampoline(void *arg) {
    fork_ctx_t c = *(fork_ctx_t *)arg;
    kfree(arg);
    if (c.fs_base)
        __asm__ volatile ("wrmsr" :: "c"(SC_MSR_FS_BASE), "a"((uint32_t)c.fs_base),
                                     "d"((uint32_t)(c.fs_base >> 32)) : "memory");
    if (c.set_tid_ptr && sc_user_page_present(c.set_tid_ptr, NULL)) {
        task_t *me = sched_current();
        if (me) *(uint32_t *)(uintptr_t)c.set_tid_ptr = me->tid;
    }
    __asm__ volatile (
        "pushq $0x23\n\t"                                  
        "pushq 8(%%rax)\n\t"                               
        "pushq 16(%%rax)\n\t"                              
        "pushq $0x2B\n\t"                                   
        "pushq 0(%%rax)\n\t"                               
        "movq 24(%%rax), %%rbx\n\t"
        "movq 32(%%rax), %%rbp\n\t"
        "movq 40(%%rax), %%r12\n\t"
        "movq 48(%%rax), %%r13\n\t"
        "movq 56(%%rax), %%r14\n\t"
        "movq 64(%%rax), %%r15\n\t"
        "movq 80(%%rax), %%r8\n\t"
        "movq 88(%%rax), %%r9\n\t"
        "movq 96(%%rax), %%r10\n\t"
        "movq 104(%%rax), %%rdx\n\t"
        "movq 112(%%rax), %%rsi\n\t"
        "movq 120(%%rax), %%rdi\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "xorq %%r11, %%r11\n\t"
        "xorl %%eax, %%eax\n\t"                                                
        "iretq\n\t"
        :
        : "a"(&c)
        : "memory"
    );
    __builtin_unreachable();
}

static int64_t sc_fork_ex(uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls) {
    task_t *cur = sched_current();
    if (!cur || !cur->pml4_phys) {
        DBG_MSG("SC", "sys_fork: caller has no user address space");
        return -(int64_t)ENOSYS;
    }
    if (!vmm_cow_ready()) {
        DBG_MSG("SC", "sys_fork: COW refcount table missing");
        return -(int64_t)ENOSYS;
    }
    sc_proc_t *pp = sc_cur();
    sc_proc_t *cp = sc_proc_alloc();
    if (!cp) return -(int64_t)EAGAIN;

    fork_ctx_t *fc = (fork_ctx_t *)kmalloc(sizeof(fork_ctx_t));
    if (!fc) { sc_proc_free(cp); return -(int64_t)ENOMEM; }
    const uint64_t *fr = (const uint64_t *)(uintptr_t)cur->kstack_top;
    fc->rflags = (fr[-1] | 0x200ULL) & ~0x100ULL;                           
    fc->rip = fr[-2]; fc->rsp = child_stack ? child_stack : fr[-3];
    fc->r10 = fr[-10]; fc->r9 = fr[-11]; fc->r8 = fr[-12];
    fc->rdx = fr[-13]; fc->rsi = fr[-14]; fc->rdi = fr[-15];
    fc->set_tid_ptr = (flags & CLONE_CHILD_SETTID) ? ctid : 0;
    fc->rbx = fr[-4]; fc->rbp = fr[-5];
    fc->r12 = fr[-6]; fc->r13 = fr[-7]; fc->r14 = fr[-8]; fc->r15 = fr[-9];
    {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(SC_MSR_FS_BASE));
        fc->fs_base = ((uint64_t)hi << 32) | lo;
    }
    if (flags & CLONE_SETTLS) fc->fs_base = tls;
    uint64_t child_fs = fc->fs_base;

    uint64_t child_pml4 = vmm_fork_user_space(cur->pml4_phys);
    if (child_pml4 == (uint64_t)-1) {
        DBG_MSG("SC", "sys_fork: vmm_fork_user_space failed (OOM)");
        kfree(fc); sc_proc_free(cp);
        return -(int64_t)ENOMEM;
    }

                                                                        
    memcpy(cp, pp, sizeof(*cp));
    cp->state = SC_PROC_RUN; cp->ppid = pp->pid; cp->tid = SC_NO_TID;
    cp->exit_code = 0; cp->cpu_ns = 0; cp->child_cpu_ns = 0;
    cp->term_sig = 0; cp->sigpending = 0;
    cp->leader = NULL; cp->exit_set = false; cp->vm_owner = 0;
    cp->clear_child_tid = (flags & CLONE_CHILD_CLEARTID) ? ctid : 0;
    sc_files_addref(cp);

                                                                               
    uint64_t fl = sc_irq_save();
    int tid = sched_create_kthread(fork_child_trampoline, fc, cur->name, cur->priority);
    task_t *ct = (tid >= 0) ? sched_find((uint32_t)tid) : NULL;
    if (!ct) {
        sc_irq_restore(fl);
        if (tid >= 0) sched_kill((uint32_t)tid);
        vmm_destroy_user_space(child_pml4);
        sc_files_close_all(cp);
        if (tid < 0) kfree(fc);
        sc_proc_free(cp);
        return -(int64_t)ENOMEM;
    }
    ct->pml4_phys = child_pml4;
    sched_arch_fork_state(ct);                                                        
    ct->fs_base = child_fs;                                              
    cp->pid = cp->tid = (uint32_t)tid;
    sc_irq_restore(fl);

    DBG_VAL("SC", "sys_fork child tid", (uint64_t)(uint32_t)tid);
    if ((flags & CLONE_PARENT_SETTID) && ptid && syscall_validate_ptr((void *)(uintptr_t)ptid, 4))
        *(uint32_t *)(uintptr_t)ptid = (uint32_t)tid;
    if (flags & CLONE_VFORK) {                                                                
        while (sc_task_alive((uint32_t)tid)) { sc_check_exit_request(); sched_yield(); }
    }
    return (int64_t)(uint32_t)tid;
}

static int64_t sc_fork(void) { return sc_fork_ex(0, 0, 0, 0, 0); }

                                                                                      
                                                                                
                                                                                      
static int64_t sc_clone_thread(uint64_t flags, uint64_t child_stack, uint64_t ptid,
                               uint64_t ctid, uint64_t tls) {
    task_t *cur = sched_current();
    if (!cur || !cur->pml4_phys) return -(int64_t)ENOSYS;
    if (!child_stack) return -(int64_t)EINVAL;
    if ((flags & (CLONE_VM | CLONE_SIGHAND | CLONE_FILES)) != (CLONE_VM | CLONE_SIGHAND | CLONE_FILES))
        return -(int64_t)EINVAL;
    sc_proc_t *L  = sc_cur();
    sc_proc_t *th = sc_proc_alloc();
    if (!th) return -(int64_t)EAGAIN;
    fork_ctx_t *fc = (fork_ctx_t *)kmalloc(sizeof(fork_ctx_t));
    if (!fc) { sc_proc_free(th); return -(int64_t)ENOMEM; }

    const uint64_t *fr = (const uint64_t *)(uintptr_t)cur->kstack_top;
    fc->rflags = (fr[-1] | 0x200ULL) & ~0x100ULL;
    fc->rip = fr[-2]; fc->rsp = child_stack;
    fc->r10 = fr[-10]; fc->r9 = fr[-11]; fc->r8 = fr[-12];
    fc->rdx = fr[-13]; fc->rsi = fr[-14]; fc->rdi = fr[-15];
    fc->rbx = fr[-4]; fc->rbp = fr[-5];
    fc->r12 = fr[-6]; fc->r13 = fr[-7]; fc->r14 = fr[-8]; fc->r15 = fr[-9];
    fc->set_tid_ptr = (flags & CLONE_CHILD_SETTID) ? ctid : 0;
    {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(SC_MSR_FS_BASE));
        fc->fs_base = ((uint64_t)hi << 32) | lo;
    }
    if (flags & CLONE_SETTLS) fc->fs_base = tls;
    uint64_t child_fs = fc->fs_base;

    th->leader = L; th->pid = L->pid; th->ppid = 0; th->state = SC_PROC_RUN;
    th->clear_child_tid = (flags & CLONE_CHILD_CLEARTID) ? ctid : 0;

    uint64_t fl = sc_irq_save();                                                                        
    int tid = sched_create_kthread(fork_child_trampoline, fc, cur->name, cur->priority);
    task_t *ct = (tid >= 0) ? sched_find((uint32_t)tid) : NULL;
    if (!ct || sched_pml4_ref(cur->pml4_phys) != 0) {
        sc_irq_restore(fl);
        if (tid >= 0) sched_kill((uint32_t)tid);
        if (tid < 0) kfree(fc);
        sc_proc_free(th);
        return -(int64_t)(ct ? EAGAIN : ENOMEM);
    }
    ct->pml4_phys = cur->pml4_phys;                                       
    sched_arch_fork_state(ct);
    ct->fs_base = child_fs;
    th->tid = (uint32_t)tid;
    if ((flags & CLONE_PARENT_SETTID) && ptid && syscall_validate_ptr((void *)(uintptr_t)ptid, 4))
        *(uint32_t *)(uintptr_t)ptid = (uint32_t)tid;                                                         
    sc_irq_restore(fl);
    DBG_VAL("SC", "sys_clone thread tid", (uint64_t)(uint32_t)tid);
    return (int64_t)(uint32_t)tid;
}

                                                                                      
                                                                                         
static int64_t sc_clone_lx(uint64_t flags, uint64_t child_stack, uint64_t ptid,
                           uint64_t ctid, uint64_t tls) {
    if (child_stack && !syscall_validate_ptr((void *)(uintptr_t)child_stack, 8))
        return -(int64_t)EINVAL;
    if (flags & CLONE_THREAD) return sc_clone_thread(flags, child_stack, ptid, ctid, tls);
    return sc_fork_ex(flags, child_stack, ptid, ctid, tls);
}

 















 










#define SC_EXEC_E2BIG   (-1000)
#define SC_EXEC_EFAULT  (-1001)

                                                              
static int sc_exec_spawn(const char *path, uint64_t argv_ptr, uint64_t envp_ptr) {
    char        *pool = (char *)kmalloc(ELF_ARG_MAX_BYTES);
    const char **vec  = (const char **)kmalloc(2 * (ELF_ARG_MAX_COUNT + 1) * sizeof(char *));
    if (!pool || !vec) { if (pool) kfree(pool); if (vec) kfree(vec); return ELF_ERR_NOMEM; }
    const char **argv = vec, **envp = vec + (ELF_ARG_MAX_COUNT + 1);
    size_t used = 0;
    int rc = 0;

    for (int pass = 0; pass < 2 && rc == 0; pass++) {
        uint64_t uvec = pass ? envp_ptr : argv_ptr;
        const char **dst = pass ? envp : argv;
        int n = 0;
        if (uvec) {
            for (;; n++) {
                if (n >= ELF_ARG_MAX_COUNT) { rc = SC_EXEC_E2BIG; break; }
                if (!syscall_validate_ptr((void *)(uintptr_t)(uvec + (uint64_t)n * 8), 8)) { rc = SC_EXEC_EFAULT; break; }
                uint64_t up = ((const uint64_t *)(uintptr_t)uvec)[n];
                if (!up) break;
                if (used + 1 >= ELF_ARG_MAX_BYTES) { rc = SC_EXEC_E2BIG; break; }
                int64_t l = sc_copy_string((const char *)(uintptr_t)up, pool + used, ELF_ARG_MAX_BYTES - used);
                if (l < 0) { rc = SC_EXEC_EFAULT; break; }
                if (used + (size_t)l + 1 >= ELF_ARG_MAX_BYTES) { rc = SC_EXEC_E2BIG; break; }
                dst[n] = pool + used;
                used += (size_t)l + 1;
            }
        }
        dst[n] = NULL;
    }

    int tid = rc;
    if (rc == 0) {
        const char *base = path;
        for (const char *q = path; *q; q++) if (*q == '/') base = q + 1;
        char name[32];
        strncpy(name, base, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        tid = elf64_spawn_ex(path, name, 0, argv, envp);
    }
    kfree(pool);
    kfree(vec);
    return tid;
}

static int64_t sc_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;
    DBG_MSG("SC", "sys_execve");
    DBG_VAL("SC", "execve path ptr", path_ptr);
    int tid = sc_exec_spawn(path, argv_ptr, envp_ptr);
    if (tid < 0) {
        if (tid == SC_EXEC_E2BIG)  return -(int64_t)7;                 
        if (tid == SC_EXEC_EFAULT) return -(int64_t)EFAULT;
        if (tid == ELF_ERR_INTERP) return -(int64_t)8;                                                    
        if (tid == ELF_ERR_IO)    return -(int64_t)ENOENT;
        if (tid == ELF_ERR_NOMEM) return -(int64_t)ENOMEM;
        if (tid == ELF_ERR_MAGIC || tid == ELF_ERR_CLASS ||
            tid == ELF_ERR_ARCH)  return -(int64_t)EACCES;
        return -(int64_t)EINVAL;
    }
                                                                              
                                                                                          
    sc_proc_t *p = sc_cur();
    {                                                                    
        task_t *self = sched_current();
        sc_group_kill_others(p, self ? self->tid : SC_NO_TID);
        sc_thr()->clear_child_tid = 0;
        p->exit_set = false; p->vm_owner = 0;
    }
    for (int fd = 0; fd < SC_FD_MAX; fd++) {
        if (p->files[fd].open && (p->files[fd].fd_flags & FD_CLOEXEC)) {
            sc_release_file(&p->files[fd]);
            p->files[fd].open = false;
        }
    }
    for (int sg = 1; sg < SC_NSIG; sg++)                                                               
        if (p->sigact[sg].handler != SC_SIG_IGN) memset(&p->sigact[sg], 0, sizeof(p->sigact[sg]));
    p->sigpending = 0; p->term_sig = 0; p->clear_child_tid = 0;
    memset(&p->altstack, 0, sizeof(p->altstack));
    memset(p->vma, 0, sizeof(p->vma));
    p->brk_current = p->brk_mapped = USER_HEAP_BASE;
    p->mmap_next   = USER_MMAP_BASE;
    uint64_t fl = sc_irq_save();
    p->tid = (uint32_t)tid;
    g_cur_cache = NULL; g_cur_cache_tid = SC_NO_TID;
    sc_irq_restore(fl);
    sched_exit();
    __builtin_unreachable();
}

 









                                                     
static int64_t sc_reap_child(sc_proc_t *par, sc_proc_t *c, uint64_t wstatus_ptr) {
    int32_t status = c->term_sig ? (c->term_sig & 0x7F)
                   : (c->state == SC_PROC_ZOMBIE) ? ((c->exit_code & 0xFF) << 8) : 9;                   
    uint32_t pid = c->pid;
    if (wstatus_ptr) *(int32_t *)(uintptr_t)wstatus_ptr = status;
    par->child_cpu_ns += c->cpu_ns + c->child_cpu_ns;
    sc_proc_free(c);
    return (int64_t)pid;
}
static bool sc_child_done(const sc_proc_t *c) {
    return c->state == SC_PROC_ZOMBIE || !sc_group_alive(c);
}

static int64_t sc_wait4(uint64_t tid_arg, uint64_t wstatus_ptr, uint64_t options) {
    if (wstatus_ptr != 0 && !syscall_validate_ptr((void*)wstatus_ptr, 4))
        return -(int64_t)EFAULT;
    bool nohang = (options & SC_WNOHANG) != 0;
    sc_proc_t *me = sc_cur();

    if (tid_arg == (uint64_t)-1 || (int64_t)tid_arg == 0) {                         
        for (;;) {
            bool have_child = false;
            for (int i = 0; i < SC_PROC_MAX; i++) {
                sc_proc_t *c = g_procs[i];
                if (!c || c == me || c->leader || c->ppid != me->pid) continue;
                have_child = true;
                if (sc_child_done(c)) return sc_reap_child(me, c, wstatus_ptr);
            }
            if (!have_child) return -(int64_t)ECHILD;
            if (nohang) return 0;
            sc_check_exit_request();
            sched_yield();
        }
    }

    uint32_t pid = (uint32_t)tid_arg;
    sc_proc_t *c = sc_proc_find_pid(pid);
    if (c && c != me && c->ppid == me->pid) {                                          
        for (;;) {
            if (sc_child_done(c)) return sc_reap_child(me, c, wstatus_ptr);
            if (nohang) return 0;
            sc_check_exit_request();
            sched_yield();
        }
    }

                                                                                        
    for (;;) {
        task_t *t = sched_find(pid);
        if (!t || t->state == TASK_ZOMBIE) {
            if (wstatus_ptr) *(int32_t *)(uintptr_t)wstatus_ptr = 0;
            return (int64_t)pid;
        }
        if (nohang) return 0;
        sc_check_exit_request();
        sched_yield();
    }
}

 




static int64_t sc_open(uint64_t path_ptr, uint64_t flags_arg) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int lx = (int)flags_arg;                                                     
    int vf = lx & (O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND);
    if (lx & LX_O_DIRECTORY) vf |= O_DIRECTORY;                                       

    if ((lx & O_CREAT) && (lx & LX_O_EXCL)) {
        vfs_stat_t ex;
        if (vfs_stat(path, &ex) == VFS_OK) return -(int64_t)EEXIST;
    }

    int vfd = vfs_open(path, vf);
    if (vfd == VFS_ERR_ISDIR && (lx & O_ACCMODE) == O_RDONLY)                                        
        vfd = vfs_open(path, vf | O_DIRECTORY);
    if (vfd < 0) {
        if (vfd == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
        if (vfd == VFS_ERR_NOTDIR) return -(int64_t)ENOTDIR;
        if (vfd == VFS_ERR_NOMEM)  return -(int64_t)ENOMEM;
        if (vfd == VFS_ERR_NFILE)  return -(int64_t)ENFILE;
        if (vfd == VFS_ERR_ISDIR)  return -(int64_t)EISDIR;
        if (vfd == VFS_ERR_ROFS)   return -(int64_t)EROFS;
        return -(int64_t)EIO;
    }

    int fd = sc_alloc_fd();
    if (fd < 0) {
        vfs_close(vfd);
        return -(int64_t)EMFILE;
    }

    g_files[fd].open           = true;
    g_files[fd].vfs_fd         = vfd;
    g_files[fd].flags          = lx & ~(O_CREAT | O_TRUNC | LX_O_EXCL | LX_O_CLOEXEC);
    g_files[fd].fd_flags       = (lx & LX_O_CLOEXEC) ? FD_CLOEXEC : 0;
    g_files[fd].is_pipe_read   = false;
    g_files[fd].is_pipe_write  = false;
    g_files[fd].pipe_idx       = -1;
    g_files[fd].dir_pos        = 0;
    if (vfd < VFS_MAX_FD) g_vfs_refs[vfd] = 1;

    DBG_VAL("SC", "sys_open fd", (uint64_t)(uint32_t)fd);
    return (int64_t)fd;
}

 


static int64_t sc_close(uint64_t fd_arg) {
    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f) return -(int64_t)EBADF;

    sc_release_file(f);           
    memset(f, 0, sizeof(sc_file_t));
    return 0;
}

 



 



                                                                           
                                                                     
                                                                             
static inline bool sc_is_console(const sc_file_t *f) {
    return !f->is_pipe_read && !f->is_pipe_write && f->vfs_fd < 0;
}

static int64_t sc_console_write(const void *buf_v, uint64_t count) {
    const char *b = (const char *)buf_v;
    for (uint64_t i = 0; i < count; i++) dbg_putc(b[i]);
    return (int64_t)count;
}

static int64_t sc_console_read(sc_file_t *f, void *buf_v, uint64_t count) {
    task_t *cur = sched_current();
    int32_t tid = cur ? (int32_t)cur->tid : 0;
    char *out = (char *)buf_v;
    uint64_t n = 0;
    for (;;) {
        if (!ui_kbd_focus_owned_by(tid)) return (int64_t)n;                            
        char ch;
        while (n < count && usb_hid_kbd_read_char(&ch)) {
            if (ch == '\r') ch = '\n';
            out[n++] = ch;
            if (ch == '\n') return (int64_t)n;                                       
        }
        if (n > 0) return (int64_t)n;
        if (f->flags & LX_O_NONBLOCK) return -(int64_t)EAGAIN;
        sc_check_exit_request();
        sched_sleep_ns(2000000);
    }
}

static int64_t sc_read_impl(sc_file_t *f, void *buf_v, uint64_t count) {
     
    if (f->is_pipe_read) {
        pipe_t *p = &g_pipes[f->pipe_idx];
        if (!p->valid) return -(int64_t)EBADF;

         
        uint32_t waited = 0;
        while (p->count == 0) {
            if (p->write_closed) return 0;   
            if (f->flags & LX_O_NONBLOCK) return -(int64_t)EAGAIN;
            sched_yield();
            waited++;
            if (waited > 1000000) return -(int64_t)EIO;
        }

        uint8_t *buf = (uint8_t *)buf_v;
        uint64_t n = (count < p->count) ? count : p->count;
        for (uint64_t i = 0; i < n; i++) {
            buf[i] = p->buf[p->read_pos];
            p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        }
        p->count -= (uint32_t)n;
        return (int64_t)n;
    }
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -(int64_t)EBADF;
    if (f->vfs_fd < 0) return sc_console_read(f, buf_v, count);

    int64_t n = vfs_read(f->vfs_fd, buf_v, count);
    if (n == VFS_ERR_BADF)    return -(int64_t)EBADF;
    if (n == VFS_ERR_IO)      return -(int64_t)EIO;
    if (n < 0)                return -(int64_t)EIO;
    return n;
}

static int64_t sc_read(uint64_t fd_arg, uint64_t buf_ptr, uint64_t count) {
    if (count == 0) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)count))
        return -(int64_t)EFAULT;

    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f) return -(int64_t)EBADF;
    return sc_read_impl(f, (void *)(uintptr_t)buf_ptr, count);
}

 



static int64_t sc_write_impl(sc_file_t *f, const void *buf_v, uint64_t count) {
     
    if (f->is_pipe_write) {
        pipe_t *p = &g_pipes[f->pipe_idx];
        if (!p->valid || p->read_closed) { sc_raise_self(SC_SIGPIPE); return -(int64_t)EPIPE; }

        const uint8_t *buf = (const uint8_t *)buf_v;
        uint64_t written = 0;
        while (written < count) {
            if (p->count >= PIPE_BUF_SIZE) {
                if (f->flags & LX_O_NONBLOCK) return written ? (int64_t)written : -(int64_t)EAGAIN;
                if (p->read_closed) { sc_raise_self(SC_SIGPIPE); return -(int64_t)EPIPE; }
                sched_yield();
                continue;
            }
            p->buf[p->write_pos] = buf[written++];
            p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
            p->count++;
        }
        return (int64_t)count;
    }
     
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -(int64_t)EBADF;
    if (f->vfs_fd < 0) return sc_console_write(buf_v, count);

    int64_t n = vfs_write(f->vfs_fd, buf_v, count);
    if (n == VFS_ERR_ROFS)    return -(int64_t)EROFS;
    if (n == VFS_ERR_NOSPC)   return -(int64_t)ENOSPC;
    if (n < 0)                return -(int64_t)EIO;
    return n;
}

static int64_t sc_write(uint64_t fd_arg, uint64_t buf_ptr, uint64_t count) {
    if (count == 0) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)count))
        return -(int64_t)EFAULT;

    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f) return -(int64_t)EBADF;
    return sc_write_impl(f, (const void *)(uintptr_t)buf_ptr, count);
}

 



static int64_t sc_lseek(uint64_t fd_arg, uint64_t offset_arg, uint64_t whence) {
    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f) return -(int64_t)EBADF;
    if (f->is_pipe_read || f->is_pipe_write || f->vfs_fd < 0) return -(int64_t)ESPIPE;

    int64_t offset = (int64_t)offset_arg;
    if ((int)whence == 0 && offset == 0) f->dir_pos = 0;    
    int64_t r = vfs_seek(f->vfs_fd, offset, (int)whence);
    if (r == VFS_ERR_OVERFLOW) return -(int64_t)EOVERFLOW;
    if (r == VFS_ERR_INVAL)    return -(int64_t)EINVAL;
    if (r < 0)                 return -(int64_t)EINVAL;
    return r;
}

 



static int64_t sc_stat_fex(uint64_t path_ptr, uint64_t stat_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)stat_ptr, sizeof(sc_stat_t)))
        return -(int64_t)EFAULT;

    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    if (r == VFS_ERR_NOENT) return -(int64_t)ENOENT;
    if (r < 0)              return -(int64_t)EIO;

    sc_stat_t *out = (sc_stat_t *)(uintptr_t)stat_ptr;
    out->type       = vs.type;
    out->size       = vs.size;
    out->alloc_size = vs.alloc_size;
    out->ctime      = vs.ctime;
    out->mtime      = vs.mtime;
    out->atime      = vs.atime;
    out->attr       = vs.attr;
    out->cluster    = vs.cluster;
    return 0;
}

 



static int64_t sc_fstat_fex(uint64_t fd_arg, uint64_t stat_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)stat_ptr, sizeof(sc_stat_t)))
        return -(int64_t)EFAULT;

    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f || f->is_pipe_read || f->is_pipe_write) return -(int64_t)EBADF;

    vfs_stat_t vs;
    int r = vfs_fstat(f->vfs_fd, &vs);
    if (r < 0) return -(int64_t)EBADF;

    sc_stat_t *out = (sc_stat_t *)(uintptr_t)stat_ptr;
    out->type       = vs.type;
    out->size       = vs.size;
    out->alloc_size = vs.alloc_size;
    out->ctime      = vs.ctime;
    out->mtime      = vs.mtime;
    out->atime      = vs.atime;
    out->attr       = vs.attr;
    out->cluster    = vs.cluster;
    return 0;
}

 



static int64_t sc_readdir(uint64_t fd_arg, uint64_t index, uint64_t dirent_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)dirent_ptr, sizeof(vfs_dirent_t)))
        return -(int64_t)EFAULT;

    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f || f->is_pipe_read || f->is_pipe_write) return -(int64_t)EBADF;

    int r = vfs_readdir(f->vfs_fd, (uint32_t)index, (vfs_dirent_t *)(uintptr_t)dirent_ptr);
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r == VFS_ERR_NOTDIR) return -(int64_t)ENOTDIR;
    if (r < 0)               return -(int64_t)EIO;
    return 0;
}

 



static int64_t sc_mkdir(uint64_t path_ptr, uint64_t mode) {
    (void)mode;
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int r = vfs_mkdir(path);
    if (r == VFS_ERR_EXIST)  return -(int64_t)EEXIST;
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r == VFS_ERR_ROFS)   return -(int64_t)EROFS;
    if (r < 0)               return -(int64_t)EIO;
    return 0;
}

 



static int64_t sc_rmdir(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int r = vfs_rmdir(path);
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r == VFS_ERR_NOTDIR) return -(int64_t)ENOTDIR;
    if (r == VFS_ERR_BUSY)   return -(int64_t)ENOTEMPTY;
    if (r == VFS_ERR_ROFS)   return -(int64_t)EROFS;
    if (r < 0)               return -(int64_t)EIO;
    return 0;
}

 



static int64_t sc_unlink(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int r = vfs_unlink(path);
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r == VFS_ERR_ISDIR)  return -(int64_t)EISDIR;
    if (r == VFS_ERR_ROFS)   return -(int64_t)EROFS;
    if (r < 0)               return -(int64_t)EIO;
    return 0;
}

 




static int64_t sc_rename(uint64_t old_ptr, uint64_t new_ptr) {
                                                                                
                                                                                     
    char *op = (char *)kmalloc(VFS_MAX_PATH);
    char *np = (char *)kmalloc(VFS_MAX_PATH);
    uint8_t *buf = (uint8_t *)kmalloc(4096);
    int64_t rc = 0;
    if (!op || !np || !buf) { rc = -(int64_t)ENOMEM; goto out; }
    if (sc_copy_path((const char *)(uintptr_t)old_ptr, op, VFS_MAX_PATH) < 0 ||
        sc_copy_path((const char *)(uintptr_t)new_ptr, np, VFS_MAX_PATH) < 0) { rc = -(int64_t)EFAULT; goto out; }
    if (strcmp(op, np) == 0) goto out;

    vfs_stat_t st;
    int r = vfs_stat(op, &st);
    if (r != VFS_OK) { rc = sc_vfs_errno(r); goto out; }
    if (st.type == VFS_TYPE_DIR) { rc = -(int64_t)EXDEV; goto out; }

    vfs_stat_t dst;
    if (vfs_stat(np, &dst) == VFS_OK && dst.type == VFS_TYPE_DIR) { rc = -(int64_t)EISDIR; goto out; }

    int in = vfs_open(op, O_RDONLY);
    if (in < 0) { rc = sc_vfs_errno(in); goto out; }
    int outfd = vfs_open(np, O_WRONLY | O_CREAT | O_TRUNC);
    if (outfd < 0) { vfs_close(in); rc = sc_vfs_errno(outfd); goto out; }
    for (;;) {
        int64_t n = vfs_read(in, buf, 4096);
        if (n < 0) { rc = -(int64_t)EIO; break; }
        if (n == 0) break;
        int64_t w = vfs_write(outfd, buf, (uint64_t)n);
        if (w != n) { rc = (w == VFS_ERR_NOSPC) ? -(int64_t)ENOSPC : -(int64_t)EIO; break; }
    }
    vfs_close(in);
    vfs_close(outfd);
    if (rc == 0) {
        r = vfs_unlink(op);
        if (r != VFS_OK) rc = sc_vfs_errno(r);
    } else {
        vfs_unlink(np);
    }
out:
    if (op) kfree(op);
    if (np) kfree(np);
    if (buf) kfree(buf);
    return rc;
}

 







static int64_t sc_chdir(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r < 0)               return -(int64_t)EIO;
    if (vs.type != VFS_TYPE_DIR) return -(int64_t)ENOTDIR;

    strncpy(g_cwd, path, VFS_MAX_PATH - 1);
    g_cwd[VFS_MAX_PATH - 1] = '\0';
    return 0;
}

 



static int64_t sc_ftruncate(uint64_t fd_arg, uint64_t length) {
    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f || f->is_pipe_read || f->is_pipe_write) return -(int64_t)EBADF;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -(int64_t)EBADF;

    int r = vfs_ftruncate(f->vfs_fd, length);
    if (r == VFS_ERR_ROFS)  return -(int64_t)EROFS;
    if (r == VFS_ERR_NOSPC) return -(int64_t)ENOSPC;
    if (r < 0)              return -(int64_t)EIO;
    return 0;
}

 



static int64_t sc_fcntl(uint64_t fd_arg, uint64_t cmd, uint64_t arg) {
    int fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(fd);
    if (!f) return -(int64_t)EBADF;

    switch ((int)cmd) {
    case F_DUPFD: {
         
        int new_fd = sc_alloc_fd_from((int)arg);
        if (new_fd < 0) return -(int64_t)EMFILE;
        sc_dup_into(new_fd, f);
        return (int64_t)new_fd;
    }
    case 1030: {                                                        
        int new_fd = sc_alloc_fd_from((int)arg);
        if (new_fd < 0) return -(int64_t)EMFILE;
        sc_dup_into(new_fd, f);
        g_files[new_fd].fd_flags = FD_CLOEXEC;
        return (int64_t)new_fd;
    }
    case 5:                                                                                      
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, 32)) return -(int64_t)EFAULT;
        *(int16_t *)(uintptr_t)arg = 2;                         
        return 0;
    case 6: case 7:                                                                       
        return 0;
    case F_GETFD:
        return (int64_t)f->fd_flags;
    case F_SETFD:
        f->fd_flags = (int)(arg & FD_CLOEXEC);
        return 0;
    case F_GETFL:
        return (int64_t)f->flags;
    case F_SETFL:
         
        f->flags = (f->flags & O_ACCMODE) | ((int)arg & ~(int)O_ACCMODE);
        return 0;
    default:
        return -(int64_t)EINVAL;
    }
}

 



static int64_t sc_brk(uint64_t new_brk) {
    if (new_brk == 0)
        return (int64_t)g_brk_current;

    if (new_brk < USER_HEAP_BASE)
        return (int64_t)g_brk_current;   
    if (new_brk > USER_HEAP_MAX)
        return -(int64_t)ENOMEM;

     
    if (new_brk > g_brk_mapped) {
        uint64_t map_start = ROUND_UP(g_brk_mapped, PAGE_SIZE);
        uint64_t map_end   = ROUND_UP(new_brk, PAGE_SIZE);
        uint64_t npages    = (map_end - map_start) >> PAGE_SHIFT;

        if (npages > 0) {
            uint64_t pa = pmm_alloc_frames((size_t)npages, 1);
            if (pa == (uint64_t)-1) return -(int64_t)ENOMEM;

             
            uint8_t *kp = (uint8_t *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
            memset(kp, 0, (size_t)(npages << PAGE_SHIFT));

            for (uint64_t i = 0; i < npages; i++) {
                int r = vmm_map_user(map_start + (i << PAGE_SHIFT),
                                     pa + (i << PAGE_SHIFT), true, false);
                if (r != 0) {
                    pmm_free_frames(pa, (size_t)npages);
                    return -(int64_t)ENOMEM;
                }
            }
            g_brk_mapped = map_end;
        }
    }

    g_brk_current = new_brk;
    return (int64_t)new_brk;
}

 









static int64_t sc_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                       uint64_t flags, uint64_t fd_arg, uint64_t offset) {
    if (length == 0) return -(int64_t)EINVAL;

    uint64_t npages = (ROUND_UP(length, PAGE_SIZE)) >> PAGE_SHIFT;
    bool rw   = (prot & PROT_WRITE) != 0;
    bool exec = (prot & PROT_EXEC)  != 0;

     
    uint64_t va;
    if ((flags & MAP_FIXED) && addr != 0) {
        va = ROUND_DOWN(addr, PAGE_SIZE);
        if (va > USER_VIRT_LIMIT) return -(int64_t)EINVAL;
        for (uint64_t i = 0; i < npages; i++)                                                        
            vmm_user_release_page(va + (i << PAGE_SHIFT));
    } else {
        va = mmap_bump_alloc(npages << PAGE_SHIFT);
        if (va == (uint64_t)-1) return -(int64_t)ENOMEM;
    }

    if (flags & MAP_ANONYMOUS) {
         
        uint64_t pa = pmm_alloc_frames((size_t)npages, 1);
        if (pa == (uint64_t)-1) return -(int64_t)ENOMEM;

        uint8_t *kp = (uint8_t *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
        memset(kp, 0, (size_t)(npages << PAGE_SHIFT));

        int pkind = (flags & MAP_SHARED) ? VMM_PAGE_SHARED : VMM_PAGE_OWNED;
        for (uint64_t i = 0; i < npages; i++) {
            int r = vmm_map_user_ex(va + (i << PAGE_SHIFT),
                                    pa + (i << PAGE_SHIFT), rw, exec, pkind);
            if (r != 0) {
                pmm_free_frames(pa, (size_t)npages);
                return -(int64_t)ENOMEM;
            }
        }
        sc_vma_record(SC_VMA_ANON, va, pa, npages, rw, exec);
        return (int64_t)va;
    }

     
    int sc_fd = (int)(uint32_t)fd_arg;
    sc_file_t *f = sc_get_file(sc_fd);
    if (!f || f->is_pipe_read || f->is_pipe_write) return -(int64_t)EBADF;

     
    uint64_t pa = pmm_alloc_frames((size_t)npages, 1);
    if (pa == (uint64_t)-1) return -(int64_t)ENOMEM;

     
    uint8_t *kbuf = (uint8_t *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
    memset(kbuf, 0, (size_t)(npages << PAGE_SHIFT));

     
    int64_t saved_pos = vfs_seek(f->vfs_fd, 0, 1);
    int64_t seek_r = vfs_seek(f->vfs_fd, (int64_t)offset, 0);
    if (seek_r < 0) {
        pmm_free_frames(pa, (size_t)npages);
        return -(int64_t)EINVAL;
    }

    int64_t nr = vfs_read(f->vfs_fd, kbuf, (uint64_t)(npages << PAGE_SHIFT));
    if (saved_pos >= 0) vfs_seek(f->vfs_fd, saved_pos, 0);
    if (nr < 0) {
        pmm_free_frames(pa, (size_t)npages);
        return -(int64_t)EIO;
    }

     
    for (uint64_t i = 0; i < npages; i++) {
        int r = vmm_map_user(va + (i << PAGE_SHIFT),
                             pa + (i << PAGE_SHIFT), rw, exec);
        if (r != 0) {
            pmm_free_frames(pa, (size_t)npages);
            return -(int64_t)ENOMEM;
        }
    }
    sc_vma_record(SC_VMA_FILECOPY, va, pa, npages, rw, exec);
    return (int64_t)va;
}

 






static int64_t sc_munmap(uint64_t addr, uint64_t length) {
    if (length == 0) return -(int64_t)EINVAL;
    if (addr > USER_VIRT_LIMIT) return -(int64_t)EINVAL;
    uint64_t va     = ROUND_DOWN(addr, PAGE_SIZE);
    uint64_t npages = ROUND_UP(length, PAGE_SIZE) >> PAGE_SHIFT;
                                                                                                              
    for (uint64_t i = 0; i < npages; i++)
        vmm_user_release_page(va + (i << PAGE_SHIFT));
    sc_vma_t *v = sc_vma_containing(va);
    if (v) {
        uint64_t v_end = v->va + (v->npages << PAGE_SHIFT);
        uint64_t u_end = va + (npages << PAGE_SHIFT);
        if (va <= v->va && u_end >= v_end) {                                       
            memset(v, 0, sizeof(*v));
        } else if (va > v->va && u_end >= v_end) {                            
            v->npages = (va - v->va) >> PAGE_SHIFT;
        } else if (va <= v->va && u_end < v_end) {                            
            uint64_t cut = (u_end - v->va) >> PAGE_SHIFT;
            v->va += cut << PAGE_SHIFT;
            v->pa += cut << PAGE_SHIFT;
            v->npages -= cut;
        } else {                                                                                           
            memset(v, 0, sizeof(*v));
        }
    }
    return 0;
}
static int64_t sc_mprotect(uint64_t addr, uint64_t length, uint64_t prot) {
    if (addr > USER_VIRT_LIMIT || (addr & (PAGE_SIZE - 1))) return -(int64_t)EINVAL;
    if (length == 0) return 0;
    if (prot & ~(uint64_t)(PROT_READ | PROT_WRITE | PROT_EXEC)) return -(int64_t)EINVAL;
    uint64_t npages = ROUND_UP(length, PAGE_SIZE) >> PAGE_SHIFT;
    if (addr + (npages << PAGE_SHIFT) - 1 > USER_VIRT_LIMIT) return -(int64_t)ENOMEM;

    for (uint64_t i = 0; i < npages; i++)                                                          
        if (!sc_user_page_present(addr + (i << PAGE_SHIFT), NULL)) return -(int64_t)ENOMEM;

    const uint64_t PTE_US  = 1ULL << 2;                            
    const uint64_t PTE_WR  = 1ULL << 1;
    const uint64_t PTE_CW  = 1ULL << 9;                                                 
    for (uint64_t i = 0; i < npages; i++) {
        uint64_t va = addr + (i << PAGE_SHIFT);
        uint64_t *pte = sc_find_pte(va);
        uint64_t e = *pte;
        if (prot == PROT_NONE) e &= ~PTE_US;                                                           
        else                   e |=  PTE_US;
        if (e & PTE_CW) {
                                                                                
                                                                                 
            e &= ~PTE_WR;
        } else if (prot & PROT_WRITE) e |= PTE_WR;
        else                          e &= ~PTE_WR;
        if (prot & PROT_EXEC) e &= ~PTE_NX; else e |= PTE_NX;
        *pte = e;
        __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");
    }
    for (int i = 0; i < SC_VMA_MAX; i++) {                                                            
        sc_vma_t *v = &g_vma[i];
        if (v->used && addr <= v->va && addr + (npages << PAGE_SHIFT) >= v->va + (v->npages << PAGE_SHIFT)) {
            v->rw = (prot & PROT_WRITE) != 0; v->exec = (prot & PROT_EXEC) != 0;
        }
    }
    return 0;
}

 




static int64_t sc_arch_prctl(uint64_t code, uint64_t addr) {
    const uint32_t MSR_FS = 0xC0000100u, MSR_GS = 0xC0000101u;
    switch (code) {
    case ARCH_SET_FS:
    case ARCH_SET_GS: {
        if (addr > USER_VIRT_LIMIT) return -(int64_t)EPERM;                                                 
        uint32_t msr = (code == ARCH_SET_FS) ? MSR_FS : MSR_GS;
        __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)addr), "d"((uint32_t)(addr >> 32)) : "memory");
        if (code == ARCH_SET_FS) { task_t *t = sched_current(); if (t) t->fs_base = addr; }
        return 0;
    }
    case ARCH_GET_FS:
    case ARCH_GET_GS: {
        if (!syscall_validate_ptr((void *)(uintptr_t)addr, 8)) return -(int64_t)EFAULT;
        uint32_t msr = (code == ARCH_GET_FS) ? MSR_FS : MSR_GS, lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
        *(uint64_t *)(uintptr_t)addr = ((uint64_t)hi << 32) | lo;
        return 0;
    }
    default:
        return -(int64_t)EINVAL;
    }
}

 



static int64_t sc_pipe(uint64_t pipefd_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)pipefd_ptr, 8))
        return -(int64_t)EFAULT;

    int pidx = pipe_alloc();
    if (pidx < 0) return -(int64_t)ENFILE;

    int fd_r = sc_alloc_fd();
    if (fd_r < 0) {
        g_pipes[pidx].valid = false;
        return -(int64_t)EMFILE;
    }
                                                                                    
    memset(&g_files[fd_r], 0, sizeof(sc_file_t));
    g_files[fd_r].open = true;

    int fd_w = sc_alloc_fd();
    if (fd_w < 0) {
        g_pipes[pidx].valid = false;
        memset(&g_files[fd_r], 0, sizeof(sc_file_t));
        return -(int64_t)EMFILE;
    }

     
    g_files[fd_r].open          = true;
    g_files[fd_r].vfs_fd        = -1;
    g_files[fd_r].flags         = O_RDONLY;
    g_files[fd_r].is_pipe_read  = true;
    g_files[fd_r].is_pipe_write = false;
    g_files[fd_r].pipe_idx      = pidx;
    g_pipes[pidx].rd_refs       = 1;

     
    g_files[fd_w].open          = true;
    g_files[fd_w].vfs_fd        = -1;
    g_files[fd_w].flags         = O_WRONLY;
    g_files[fd_w].is_pipe_read  = false;
    g_files[fd_w].is_pipe_write = true;
    g_files[fd_w].pipe_idx      = pidx;
    g_pipes[pidx].wr_refs       = 1;

    int32_t *pfd = (int32_t *)(uintptr_t)pipefd_ptr;
    pfd[0] = fd_r;
    pfd[1] = fd_w;

    DBG_VAL("SC", "sys_pipe fd_r", (uint64_t)(uint32_t)fd_r);
    DBG_VAL("SC", "sys_pipe fd_w", (uint64_t)(uint32_t)fd_w);
    return 0;
}

 




static int64_t sc_shm_get(uint64_t name_ptr, uint64_t size, uint64_t flags) {
    char name[SHM_MAX_NAME];
    if (sc_copy_string((const char *)(uintptr_t)name_ptr, name, sizeof(name)) < 0)
        return -(int64_t)EFAULT;

     
    int idx = shm_find_by_name(name);
    if (idx >= 0) {
        g_shm[idx].refcount++;
        return (int64_t)idx;
    }

     
    if (!(flags & 1)) return -(int64_t)ENOENT;

     
    if (size == 0 || size > 64ULL * 1024 * 1024) return -(int64_t)EINVAL;

    idx = shm_alloc_slot();
    if (idx < 0) return -(int64_t)ENFILE;

    uint64_t npages = ROUND_UP(size, PAGE_SIZE) >> PAGE_SHIFT;
    uint64_t pa = pmm_alloc_frames((size_t)npages, 1);
    if (pa == (uint64_t)-1) return -(int64_t)ENOMEM;

     
    uint8_t *kp = (uint8_t *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
    memset(kp, 0, (size_t)(npages << PAGE_SHIFT));

    strncpy(g_shm[idx].name, name, SHM_MAX_NAME - 1);
    g_shm[idx].name[SHM_MAX_NAME - 1] = '\0';
    g_shm[idx].phys_base    = pa;
    g_shm[idx].virt_kernel  = pa + DIRECT_MAP_OFFSET;
    g_shm[idx].size         = size;
    g_shm[idx].pages        = (uint32_t)npages;
    g_shm[idx].refcount     = 1;
    g_shm[idx].valid        = true;

    DBG_VAL("SC", "sys_shm_get idx", (uint64_t)(uint32_t)idx);
    return (int64_t)idx;
}

 




static int64_t sc_shm_at(uint64_t shm_idx_arg, uint64_t addr_hint, uint64_t prot) {
    int idx = (int)(uint32_t)shm_idx_arg;
    if (idx < 0 || idx >= SHM_MAX_REGIONS || !g_shm[idx].valid)
        return -(int64_t)EINVAL;

    shm_region_t *shm = &g_shm[idx];
    bool rw   = (prot & PROT_WRITE) != 0;
    bool exec = (prot & PROT_EXEC)  != 0;

     
    uint64_t va;
    if (addr_hint != 0 && addr_hint <= USER_VIRT_LIMIT) {
        va = ROUND_DOWN(addr_hint, PAGE_SIZE);
    } else {
        va = mmap_bump_alloc(shm->pages << PAGE_SHIFT);
        if (va == (uint64_t)-1) return -(int64_t)ENOMEM;
    }

     
    for (uint32_t i = 0; i < shm->pages; i++) {
        int r = vmm_map_user_ex(va + ((uint64_t)i << PAGE_SHIFT),
                                shm->phys_base + ((uint64_t)i << PAGE_SHIFT),
                                rw, exec, VMM_PAGE_FOREIGN);
        if (r != 0) return -(int64_t)ENOMEM;
    }

    DBG_VAL("SC", "sys_shm_at va", va);
    return (int64_t)va;
}

 






static int64_t sc_shm_detach(uint64_t va, uint64_t shm_idx_arg) {
    int idx = (int)(uint32_t)shm_idx_arg;
    if (idx < 0 || idx >= SHM_MAX_REGIONS || !g_shm[idx].valid)
        return -(int64_t)EINVAL;

    shm_region_t *shm = &g_shm[idx];

     
    for (uint32_t i = 0; i < shm->pages; i++) {
        sc_user_unmap_page(ROUND_DOWN(va, PAGE_SIZE) + ((uint64_t)i << PAGE_SHIFT));
    }

    shm->refcount--;
    if (shm->refcount == 0) {
        pmm_free_frames(shm->phys_base, (size_t)shm->pages);
        memset(shm, 0, sizeof(shm_region_t));
    }
    return 0;
}

 

























#define FB_USER_VA_BASE   0x0000700000000000ULL

static int64_t sc_get_framebuffer(uint64_t fb_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)fb_ptr, sizeof(sc_framebuffer_t)))
        return -(int64_t)EFAULT;

    sc_framebuffer_t *fb = (sc_framebuffer_t *)(uintptr_t)fb_ptr;

    if (fb_get_mode() == FB_MODE_LINEAR_ && fb_phys_addr() != 0) {
        void *back = fb_back_ptr();
        if (!back) {
            DBG_MSG("SC", "sys_get_framebuffer: double-buffer disabled, nothing to map");
            return -(int64_t)ENOMEM;
        }

         















        uint64_t back_pa = vmm_virt_to_phys((uint64_t)(uintptr_t)back);
        if (back_pa == (uint64_t)-1) {
            DBG_MSG("SC", "sys_get_framebuffer: vmm_virt_to_phys(back) failed - back buffer not mapped?!");
            return -(int64_t)ENOMEM;
        }
        uint32_t pitch     = fb_pitch();
        uint32_t height    = fb_height();
        uint64_t fb_bytes  = (uint64_t)pitch * (uint64_t)height;

        uint64_t page_base = ROUND_DOWN(back_pa, PAGE_SIZE);
        uint64_t page_off  = back_pa - page_base;
        uint64_t npages    = (fb_bytes + page_off + PAGE_SIZE - 1) / PAGE_SIZE;

        for (uint64_t i = 0; i < npages; i++) {
            int r = vmm_map_user_ex(FB_USER_VA_BASE + (i << PAGE_SHIFT),
                                    page_base + (i << PAGE_SHIFT),
                                    true  , false  , VMM_PAGE_FOREIGN);
            if (r != 0) {
                DBG_MSG("SC", "sys_get_framebuffer: vmm_map_user failed");
                return -(int64_t)ENOMEM;
            }
        }

        fb->phys_addr = back_pa;
        fb->virt_addr = FB_USER_VA_BASE + page_off;   
        fb->width     = fb_width();
        fb->height    = height;
        fb->pitch     = pitch;
        fb->bpp       = fb_bpp();
        fb->mode      = 1;    
        fb_get_channel_shifts(&fb->r_shift, &fb->g_shift, &fb->b_shift);

        DBG_MSG("SC", "sys_get_framebuffer: linear GOP (back buffer mapped)");
        return 0;
    }

    fb->phys_addr = VGA_TEXT_PHYS;
    fb->virt_addr = VGA_TEXT_PHYS;   
    fb->width     = VGA_TEXT_COLS;
    fb->height    = VGA_TEXT_ROWS;
    fb->pitch     = VGA_TEXT_COLS * 2;   
    fb->bpp       = 4;                   
    fb->mode      = 0;                   
    fb->r_shift = fb->g_shift = fb->b_shift = 0;    

    DBG_MSG("SC", "sys_get_framebuffer: VGA fallback (linear FB not ready)");
    return 0;
}

 












#define UI_WIN_TITLE_MAX_SC   64

 

static bool sc_win_is_mine(uint64_t win_id) {
    task_t *cur = sched_current();
    if (!cur) return false;
    return ui_win_owner_pid((int)win_id) == (int32_t)cur->tid;
}

static int64_t sc_win_create(uint64_t title_ptr, uint64_t title_len,
                             uint64_t w, uint64_t h) {
    if (title_len == 0 || title_len >= UI_WIN_TITLE_MAX_SC)
        title_len = UI_WIN_TITLE_MAX_SC - 1;

    if (!syscall_validate_ptr((void *)(uintptr_t)title_ptr, (size_t)title_len))
        return -(int64_t)EFAULT;

    if (w == 0 || h == 0 || w > 4096 || h > 4096)
        return -(int64_t)EINVAL;

    char title[UI_WIN_TITLE_MAX_SC];
    const char *src = (const char *)(uintptr_t)title_ptr;
    size_t n = 0;
    while (n < title_len && src[n] != '\0') { title[n] = src[n]; n++; }
    title[n] = '\0';

    task_t *cur = sched_current();
    int32_t owner_pid = cur ? (int32_t)cur->tid : -1;

    int id = ui_win_create(title, (uint32_t)w, (uint32_t)h, owner_pid);
    if (id < 0) {
        DBG_MSG("SC", "sys_win_create: could not create window (no free slots?)");
        return -(int64_t)ENOMEM;
    }

    DBG_VAL("SC", "sys_win_create: id", (uint64_t)id);
    return (int64_t)id;
}

static int64_t sc_win_close(uint64_t win_id) {
    if (!sc_win_is_mine(win_id)) return -(int64_t)EINVAL;
    if (ui_win_close((int)win_id) != 0)
        return -(int64_t)EINVAL;
    return 0;
}

static int64_t sc_win_move(uint64_t win_id, uint64_t x, uint64_t y) {
    if (!sc_win_is_mine(win_id)) return -(int64_t)EINVAL;
    if (ui_win_move((int)win_id, (int32_t)x, (int32_t)y) != 0)
        return -(int64_t)EINVAL;
    return 0;
}

 










static int64_t sc_win_get_rect(uint64_t win_id, uint64_t rect_ptr) {
    if (!sc_win_is_mine(win_id)) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)rect_ptr, sizeof(sc_win_rect_t)))
        return -(int64_t)EFAULT;

    sc_win_rect_t *out = (sc_win_rect_t *)(uintptr_t)rect_ptr;
    int32_t x, y; uint32_t w, h, redraw_gen;
    if (!ui_win_get_rect((int)win_id, &x, &y, &w, &h, &redraw_gen))
        return -(int64_t)EINVAL;

    out->x = x; out->y = y; out->w = w; out->h = h;
    out->redraw_gen = redraw_gen;
    return 0;
}

 


static int64_t sc_input_poll(uint64_t input_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)input_ptr, sizeof(sc_input_t)))
        return -(int64_t)EFAULT;

    sc_input_t *out = (sc_input_t *)(uintptr_t)input_ptr;
    int32_t x, y;
    ui_get_mouse_pos(&x, &y);
    out->x = x;
    out->y = y;
     





    out->buttons = (uint8_t)(ps2_mouse_buttons() | usb_hid_mouse_buttons());
    return 0;
}

 







 
static int64_t sc_vfs_errno(int r) {
    switch (r) {
    case VFS_ERR_NOENT:  return -(int64_t)ENOENT;
    case VFS_ERR_NOMEM:  return -(int64_t)ENOMEM;
    case VFS_ERR_ISDIR:  return -(int64_t)EISDIR;
    case VFS_ERR_NOTDIR: return -(int64_t)ENOTDIR;
    case VFS_ERR_ROFS:   return -(int64_t)EROFS;
    case VFS_ERR_NOSPC:  return -(int64_t)ENOSPC;
    case VFS_ERR_EXIST:  return -(int64_t)EEXIST;
    case VFS_ERR_INVAL:  return -(int64_t)EINVAL;
    case VFS_ERR_BADF:   return -(int64_t)EBADF;
    case VFS_ERR_NFILE:  return -(int64_t)ENFILE;
    case VFS_ERR_BUSY:   return -(int64_t)ENOTEMPTY;
    case VFS_ERR_UNSUP:  return -(int64_t)ENOSYS;
    default:             return -(int64_t)EIO;
    }
}

 





static uint64_t g_tsc_boot   = 0;
static bool     g_rt_valid   = false;
static int64_t  g_rt_base_ns = 0;       

static inline uint64_t sc_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t sc_mono_ns(void) {
    uint64_t per_ms = usb_tsc_per_ms();
    if (per_ms == 0) per_ms = 3000000ULL;
    uint64_t d   = sc_rdtsc() - g_tsc_boot;
    uint64_t ms  = d / per_ms;
    uint64_t rem = d % per_ms;                        
    return ms * 1000000ULL + (rem * 1000000ULL) / per_ms;
}

static uint8_t sc_cmos_read(uint8_t reg) {
     


    uint64_t fl;
    uint8_t v;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
    __asm__ volatile ("outb %0, $0x70" :: "a"(reg));
    __asm__ volatile ("inb $0x71, %0" : "=a"(v));
    __asm__ volatile ("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
    return v;
}

static inline uint8_t sc_bcd(uint8_t v) { return (uint8_t)((v & 0x0F) + ((v >> 4) * 10)); }

 
static int64_t sc_days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2);
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

 
static int64_t sc_rtc_unix(void) {
    uint8_t sec = 0, mi = 0, h = 0, d = 0, mo = 0, y = 0, cen = 0;
    for (int tries = 0; tries < 8; tries++) {
        uint32_t guard = 0;
        while ((sc_cmos_read(0x0A) & 0x80) && guard++ < 200000) { }    
        sec = sc_cmos_read(0x00); mi = sc_cmos_read(0x02); h = sc_cmos_read(0x04);
        d = sc_cmos_read(0x07); mo = sc_cmos_read(0x08); y = sc_cmos_read(0x09);
        cen = sc_cmos_read(0x32);
        if (sc_cmos_read(0x00) == sec && sc_cmos_read(0x02) == mi && sc_cmos_read(0x04) == h &&
            sc_cmos_read(0x07) == d)
            break;                                                     
    }
    uint8_t b = sc_cmos_read(0x0B);
    bool binary = (b & 0x04) != 0;
    bool h24    = (b & 0x02) != 0;
    bool pm     = !h24 && (h & 0x80);
    if (pm) h &= 0x7F;
    if (!binary) {
        sec = sc_bcd(sec); mi = sc_bcd(mi); h = sc_bcd(h); d = sc_bcd(d);
        mo = sc_bcd(mo); y = sc_bcd(y); cen = sc_bcd(cen);
    }
    if (!h24) { h = (uint8_t)(h % 12); if (pm) h = (uint8_t)(h + 12); }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 59) return 0;

    int64_t year = (cen >= 19 && cen <= 21) ? (int64_t)cen * 100 + y : 2000 + (int64_t)y;
    int64_t days = sc_days_from_civil(year, mo, d);
    return days * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + sec;
}

static int64_t sc_realtime_ns(void) {
    uint64_t mono = sc_mono_ns();
    if (!g_rt_valid) {
        int64_t rt = sc_rtc_unix();
        g_rt_base_ns = rt * 1000000000LL - (int64_t)mono;
        g_rt_valid = true;
    }
    return g_rt_base_ns + (int64_t)mono;
}

 
static int64_t sc_clock_gettime(uint64_t clk, uint64_t ts_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)ts_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;

    int64_t ns;
    switch ((int)clk) {
    case SC_CLOCK_REALTIME:
        ns = sc_realtime_ns();
        break;
    case SC_CLOCK_MONOTONIC:
    case SC_CLOCK_MONOTONIC_RAW:
    case SC_CLOCK_BOOTTIME:
        ns = (int64_t)sc_mono_ns();
        break;
    case SC_CLOCK_PROCESS_CPUTIME_ID:
    case SC_CLOCK_THREAD_CPUTIME_ID: {
        task_t *t = sched_current();
        ns = t ? (int64_t)t->total_runtime : 0;
        break;
    }
    default:
        return -(int64_t)EINVAL;
    }
    sc_timespec_t *ts = (sc_timespec_t *)(uintptr_t)ts_ptr;
    ts->tv_sec  = ns / 1000000000LL;
    ts->tv_nsec = ns % 1000000000LL;
    return 0;
}

 
static int64_t sc_gettimeofday(uint64_t tv_ptr, uint64_t tz_ptr) {
    if (tv_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)tv_ptr, sizeof(sc_timeval_t)))
            return -(int64_t)EFAULT;
        int64_t ns = sc_realtime_ns();
        sc_timeval_t *tv = (sc_timeval_t *)(uintptr_t)tv_ptr;
        tv->tv_sec  = ns / 1000000000LL;
        tv->tv_usec = (ns % 1000000000LL) / 1000;
    }
    if (tz_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)tz_ptr, 8))
            return -(int64_t)EFAULT;
        memset((void *)(uintptr_t)tz_ptr, 0, 8);    
    }
    return 0;
}

 




static int64_t sc_nanosleep(uint64_t req_ptr, uint64_t rem_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)req_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;
    if (rem_ptr && !syscall_validate_ptr((void *)(uintptr_t)rem_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;

    const sc_timespec_t *req = (const sc_timespec_t *)(uintptr_t)req_ptr;
    int64_t sec = req->tv_sec, nsec = req->tv_nsec;
    if (sec < 0 || nsec < 0 || nsec >= 1000000000LL) return -(int64_t)EINVAL;
    if (sec > 100000000LL) sec = 100000000LL;                  

    uint64_t deadline = sc_mono_ns() + (uint64_t)sec * 1000000000ULL + (uint64_t)nsec;
    while (sc_mono_ns() < deadline) {
        sc_check_exit_request();       
        sched_yield();
    }

    if (rem_ptr) {
        sc_timespec_t *rem = (sc_timespec_t *)(uintptr_t)rem_ptr;
        rem->tv_sec = 0; rem->tv_nsec = 0;
    }
    return 0;
}

 
                                                                              
static void sc_set_realtime_ns(int64_t ns) {
    g_rt_base_ns = ns - (int64_t)sc_mono_ns();
    g_rt_valid   = true;
}
static void sc_sleep_until_mono(uint64_t deadline_ns) {
    for (;;) {
        uint64_t now = sc_mono_ns();
        if (now >= deadline_ns) break;
        sc_check_exit_request();
        uint64_t rem = deadline_ns - now;
        sched_sleep_ns(rem > 10000000ULL ? 10000000ULL : rem);                                                   
    }
}
static bool sc_clock_valid(int64_t clk) {
    switch ((int)clk) {
    case SC_CLOCK_REALTIME: case SC_CLOCK_MONOTONIC: case SC_CLOCK_PROCESS_CPUTIME_ID:
    case SC_CLOCK_THREAD_CPUTIME_ID: case SC_CLOCK_MONOTONIC_RAW: case SC_CLOCK_BOOTTIME:
        return true;
    default: return false;
    }
}

                                                        
static int64_t sc_time(uint64_t tloc_ptr) {
    int64_t sec = sc_realtime_ns() / 1000000000LL;
    if (tloc_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)tloc_ptr, sizeof(int64_t)))
            return -(int64_t)EFAULT;
        *(int64_t *)(uintptr_t)tloc_ptr = sec;
    }
    return sec;
}

                                                                             
static int64_t sc_times(uint64_t buf_ptr) {
    const uint64_t ns_per_tick = 1000000000ULL / SC_TICKS_PER_SEC;
    if (buf_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, sizeof(sc_tms_t)))
            return -(int64_t)EFAULT;
        task_t *t = sched_current();
        sc_tms_t *tm = (sc_tms_t *)(uintptr_t)buf_ptr;
        tm->tms_utime  = (int64_t)((t ? t->total_runtime : 0) / ns_per_tick);
        tm->tms_stime  = 0;                                                          
        tm->tms_cutime = (int64_t)(sc_cur()->child_cpu_ns / ns_per_tick);
        tm->tms_cstime = 0;
    }
    return (int64_t)(sc_mono_ns() / ns_per_tick);
}

                                     
static int64_t sc_getrusage(uint64_t who, uint64_t ru_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)ru_ptr, sizeof(sc_rusage_t)))
        return -(int64_t)EFAULT;
    uint64_t ns;
    switch ((int64_t)who) {
    case SC_RUSAGE_SELF: case SC_RUSAGE_THREAD: {
        task_t *t = sched_current();
        ns = t ? t->total_runtime : 0;
        break;
    }
    case SC_RUSAGE_CHILDREN:
        ns = sc_cur()->child_cpu_ns;
        break;
    default:
        return -(int64_t)EINVAL;
    }
    sc_rusage_t *ru = (sc_rusage_t *)(uintptr_t)ru_ptr;
    memset(ru, 0, sizeof(*ru));
    ru->ru_utime.tv_sec  = (int64_t)(ns / 1000000000ULL);
    ru->ru_utime.tv_usec = (int64_t)((ns % 1000000000ULL) / 1000ULL);
    return 0;
}

                                                                             
static int64_t sc_settimeofday(uint64_t tv_ptr, uint64_t tz_ptr) {
    (void)tz_ptr;
    if (!tv_ptr) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)tv_ptr, sizeof(sc_timeval_t)))
        return -(int64_t)EFAULT;
    const sc_timeval_t *tv = (const sc_timeval_t *)(uintptr_t)tv_ptr;
    if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -(int64_t)EINVAL;
    sc_set_realtime_ns(tv->tv_sec * 1000000000LL + tv->tv_usec * 1000LL);
    return 0;
}

                                                            
static int64_t sc_clock_settime(uint64_t clk, uint64_t ts_ptr) {
    if ((int)clk != SC_CLOCK_REALTIME) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)ts_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;
    const sc_timespec_t *ts = (const sc_timespec_t *)(uintptr_t)ts_ptr;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) return -(int64_t)EINVAL;
    sc_set_realtime_ns(ts->tv_sec * 1000000000LL + ts->tv_nsec);
    return 0;
}

                                           
static int64_t sc_clock_getres(uint64_t clk, uint64_t ts_ptr) {
    if (!sc_clock_valid((int64_t)(int)clk)) return -(int64_t)EINVAL;
    if (ts_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)ts_ptr, sizeof(sc_timespec_t)))
            return -(int64_t)EFAULT;
        sc_timespec_t *ts = (sc_timespec_t *)(uintptr_t)ts_ptr;
        ts->tv_sec = 0; ts->tv_nsec = 1;
    }
    return 0;
}

                                           
static int64_t sc_clock_nanosleep(uint64_t clk, uint64_t flags, uint64_t req_ptr, uint64_t rem_ptr) {
    switch ((int)clk) {
    case SC_CLOCK_REALTIME: case SC_CLOCK_MONOTONIC:
    case SC_CLOCK_MONOTONIC_RAW: case SC_CLOCK_BOOTTIME: break;
    default: return -(int64_t)EINVAL;
    }
    if (!syscall_validate_ptr((void *)(uintptr_t)req_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;
    if (rem_ptr && !syscall_validate_ptr((void *)(uintptr_t)rem_ptr, sizeof(sc_timespec_t)))
        return -(int64_t)EFAULT;
    const sc_timespec_t *req = (const sc_timespec_t *)(uintptr_t)req_ptr;
    int64_t sec = req->tv_sec, nsec = req->tv_nsec;
    if (sec < 0 || nsec < 0 || nsec >= 1000000000LL) return -(int64_t)EINVAL;
    if (sec > 100000000000LL) sec = 100000000000LL;                                             

    if (flags & SC_TIMER_ABSTIME) {
        int64_t abs_ns = sec * 1000000000LL + nsec;
        if ((int)clk == SC_CLOCK_REALTIME) {
            (void)sc_realtime_ns();                                                                     
            abs_ns -= g_rt_base_ns;                                                                    
        }
        if (abs_ns > 0) sc_sleep_until_mono((uint64_t)abs_ns);
        return 0;                                                                                    
    }
    if (sec > 100000000LL) sec = 100000000LL;
    sc_sleep_until_mono(sc_mono_ns() + (uint64_t)sec * 1000000000ULL + (uint64_t)nsec);
    if (rem_ptr) {
        sc_timespec_t *rem = (sc_timespec_t *)(uintptr_t)rem_ptr;
        rem->tv_sec = 0; rem->tv_nsec = 0;
    }
    return 0;
}

static void sc_uts_set(char *dst, const char *src) {
    size_t i = 0;
    while (src[i] && i < SC_UTS_LEN - 1) { dst[i] = src[i]; i++; }
    while (i < SC_UTS_LEN) dst[i++] = '\0';
}

static int64_t sc_uname(uint64_t buf_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, sizeof(sc_utsname_t)))
        return -(int64_t)EFAULT;
    sc_utsname_t *u = (sc_utsname_t *)(uintptr_t)buf_ptr;
    sc_uts_set(u->sysname,    "Fexox OS");
    sc_uts_set(u->nodename,   "fexox-os");
    sc_uts_set(u->release,    "1.0");
    sc_uts_set(u->version,    "#1 Fexox OS x86_64");
    sc_uts_set(u->machine,    "x86_64");
    sc_uts_set(u->domainname, "(none)");
    return 0;
}

 



static uint64_t g_rng_state = 0;
static int      g_have_rdrand = -1;

static bool sc_rdrand64(uint64_t *out) {
    if (g_have_rdrand < 0) {
        uint32_t a = 1, b, c = 0, d;
        __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        g_have_rdrand = (c >> 30) & 1;
    }
    if (!g_have_rdrand) return false;
    for (int i = 0; i < 10; i++) {
        uint64_t v; uint8_t ok;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) :: "cc");
        if (ok) { *out = v; return true; }
    }
    return false;
}

static uint64_t sc_rand64(void) {
    uint64_t v;
    if (sc_rdrand64(&v)) return v;
    if (g_rng_state == 0)
        g_rng_state = sc_rdtsc() ^ ((uint64_t)sc_rtc_unix() * 0x9E3779B97F4A7C15ULL) ^ 0xD1B54A32D192ED03ULL;
    g_rng_state ^= sc_rdtsc();
    uint64_t x = g_rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    g_rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int64_t sc_getrandom(uint64_t buf_ptr, uint64_t len, uint64_t flags) {
    if (flags & ~(uint64_t)(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE))
        return -(int64_t)EINVAL;
    if (len == 0) return 0;
    if (len > (1ULL << 20)) len = 1ULL << 20;                  
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)len))
        return -(int64_t)EFAULT;

    uint8_t *p = (uint8_t *)(uintptr_t)buf_ptr;
    uint64_t done = 0;
    while (done < len) {
        uint64_t r = sc_rand64();
        for (int i = 0; i < 8 && done < len; i++) { p[done++] = (uint8_t)r; r >>= 8; }
    }
    return (int64_t)len;
}

 



 
static int64_t sc_sysinfo(uint64_t buf_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, sizeof(sc_sysinfo_t)))
        return -(int64_t)EFAULT;

    uint64_t total_fr = 0, free_fr = 0, heap_used = 0;
    pmm_get_stats(&total_fr, &free_fr);
    kmalloc_get_diag(NULL, NULL, &heap_used);

    sc_sysinfo_t *si = (sc_sysinfo_t *)(uintptr_t)buf_ptr;
    memset(si, 0, sizeof(*si));
    si->uptime   = (int64_t)(sc_mono_ns() / 1000000000ULL);
    si->totalram = total_fr * PAGE_SIZE;
    si->freeram  = free_fr * PAGE_SIZE;
    si->bufferram = heap_used;               
    uint32_t nt = sched_task_count();
    si->procs    = (uint16_t)(nt > 0xFFFF ? 0xFFFF : nt);
    si->mem_unit = 1;
    return 0;
}

 


static int64_t sc_mincore(uint64_t addr, uint64_t length, uint64_t vec_ptr) {
    if (addr & (PAGE_SIZE - 1)) return -(int64_t)EINVAL;
    if (length == 0) return 0;
    if (length > (1ULL << 40) || addr > USER_VIRT_LIMIT || addr + length - 1 > USER_VIRT_LIMIT)
        return -(int64_t)ENOMEM;

    uint64_t npages = ROUND_UP(length, PAGE_SIZE) >> PAGE_SHIFT;
    if (!syscall_validate_ptr((void *)(uintptr_t)vec_ptr, (size_t)npages))
        return -(int64_t)EFAULT;

    uint8_t *vec = (uint8_t *)(uintptr_t)vec_ptr;
    bool hole = false;
    for (uint64_t i = 0; i < npages; i++) {
        bool p = sc_user_page_present(addr + (i << PAGE_SHIFT), NULL);
        vec[i] = p ? 1 : 0;
        if (!p) hole = true;
    }
    return hole ? -(int64_t)ENOMEM : 0;
}

 




static int64_t sc_madvise(uint64_t addr, uint64_t length, uint64_t advice) {
    if (addr & (PAGE_SIZE - 1)) return -(int64_t)EINVAL;
    switch ((int)advice) {
    case MADV_NORMAL: case MADV_RANDOM: case MADV_SEQUENTIAL: case MADV_WILLNEED:
    case MADV_DONTNEED: case MADV_FREE: case MADV_DONTFORK: case MADV_DOFORK:
    case MADV_HUGEPAGE: case MADV_NOHUGEPAGE:
        break;
    default:
        return -(int64_t)EINVAL;
    }
    if (length == 0) return 0;
    if (addr > USER_VIRT_LIMIT || addr + length - 1 > USER_VIRT_LIMIT)
        return -(int64_t)ENOMEM;

    uint64_t npages = ROUND_UP(length, PAGE_SIZE) >> PAGE_SHIFT;
    for (uint64_t i = 0; i < npages; i++)
        if (!sc_user_page_present(addr + (i << PAGE_SHIFT), NULL)) return -(int64_t)ENOMEM;

    if ((int)advice == MADV_DONTNEED) {
        for (uint64_t i = 0; i < npages; i++) {
            uint64_t va = addr + (i << PAGE_SHIFT), pa;
            sc_vma_t *v = sc_vma_containing(va);
            if (!v || v->kind != SC_VMA_ANON) continue;
            vmm_cow_break(va);                                                        
            if (sc_user_page_present(va, &pa))
                memset((void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET), 0, PAGE_SIZE);
        }
    }
    return 0;
}

 







static int64_t sc_mremap(uint64_t old_addr, uint64_t old_size, uint64_t new_size, uint64_t flags) {
    if (flags & ~(uint64_t)MREMAP_MAYMOVE) return -(int64_t)EINVAL;    
    if (old_addr & (PAGE_SIZE - 1)) return -(int64_t)EINVAL;
    if (old_size == 0 || new_size == 0) return -(int64_t)EINVAL;
    if (old_addr > USER_VIRT_LIMIT) return -(int64_t)EFAULT;
    if (new_size > (1ULL << 32)) return -(int64_t)ENOMEM;

    uint64_t old_np = ROUND_UP(old_size, PAGE_SIZE) >> PAGE_SHIFT;
    uint64_t new_np = ROUND_UP(new_size, PAGE_SIZE) >> PAGE_SHIFT;

    sc_vma_t *v = sc_vma_containing(old_addr);
    if (!v || v->va != old_addr) return -(int64_t)EFAULT;             
    if (v->npages != old_np)     return -(int64_t)EINVAL;
    if (!sc_user_page_present(old_addr, NULL)) return -(int64_t)EFAULT;

    if (new_np == old_np) return (int64_t)old_addr;

    if (new_np < old_np) {                                             
        for (uint64_t i = new_np; i < old_np; i++)
            vmm_user_release_page(old_addr + (i << PAGE_SHIFT));
        v->npages = new_np;
        return (int64_t)old_addr;
    }

    if (!(flags & MREMAP_MAYMOVE)) return -(int64_t)ENOMEM;            

    uint64_t new_pa = pmm_alloc_frames((size_t)new_np, 1);
    if (new_pa == (uint64_t)-1) return -(int64_t)ENOMEM;
    uint8_t *kn = (uint8_t *)(uintptr_t)(new_pa + DIRECT_MAP_OFFSET);
    memset(kn, 0, (size_t)(new_np << PAGE_SHIFT));
    for (uint64_t i = 0; i < old_np; i++) {                                                                 
        uint64_t opa;
        if (sc_user_page_present(old_addr + (i << PAGE_SHIFT), &opa))
            memcpy(kn + (i << PAGE_SHIFT), (const void *)(uintptr_t)(opa + DIRECT_MAP_OFFSET), PAGE_SIZE);
    }

    uint64_t new_va = mmap_bump_alloc(new_np << PAGE_SHIFT);
    if (new_va == (uint64_t)-1) { pmm_free_frames(new_pa, (size_t)new_np); return -(int64_t)ENOMEM; }

    for (uint64_t i = 0; i < new_np; i++) {
        int r = vmm_map_user(new_va + (i << PAGE_SHIFT), new_pa + (i << PAGE_SHIFT), v->rw, v->exec);
        if (r != 0) {
            for (uint64_t j = 0; j < i; j++) sc_user_unmap_page(new_va + (j << PAGE_SHIFT));
            pmm_free_frames(new_pa, (size_t)new_np);
            return -(int64_t)ENOMEM;
        }
    }
    for (uint64_t i = 0; i < old_np; i++)
        vmm_user_release_page(old_addr + (i << PAGE_SHIFT));

    v->va = new_va; v->pa = new_pa; v->npages = new_np;
    return (int64_t)new_va;
}

 



 
static int64_t sc_dup(uint64_t fd_arg) {
    sc_file_t *f = sc_get_file((int)(uint32_t)fd_arg);
    if (!f) return -(int64_t)EBADF;
    int nfd = sc_alloc_fd_from(0);
    if (nfd < 0) return -(int64_t)EMFILE;
    sc_dup_into(nfd, f);
    return (int64_t)nfd;
}

 
static int64_t sc_dup2(uint64_t old_arg, uint64_t new_arg) {
    sc_file_t *f = sc_get_file((int)(uint32_t)old_arg);
    if (!f) return -(int64_t)EBADF;
    if (new_arg >= SC_FD_MAX) return -(int64_t)EBADF;
    int nfd = (int)new_arg;
    if (nfd == (int)(uint32_t)old_arg) return (int64_t)nfd;
    if (g_files[nfd].open) {
        sc_release_file(&g_files[nfd]);
        memset(&g_files[nfd], 0, sizeof(sc_file_t));
    }
    sc_dup_into(nfd, f);
    return (int64_t)nfd;
}

 


static int64_t sc_pio_common(uint64_t fd_arg, uint64_t buf_ptr, uint64_t count,
                             uint64_t offset, bool is_write) {
    if (count == 0) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)count))
        return -(int64_t)EFAULT;
    sc_file_t *f = sc_get_file((int)(uint32_t)fd_arg);
    if (!f) return -(int64_t)EBADF;
    if (f->is_pipe_read || f->is_pipe_write || f->vfs_fd < 0) return -(int64_t)ESPIPE;
    if ((int64_t)offset < 0) return -(int64_t)EINVAL;

    int64_t cur = vfs_seek(f->vfs_fd, 0, 1);
    if (cur < 0) return -(int64_t)ESPIPE;
    if (vfs_seek(f->vfs_fd, (int64_t)offset, 0) < 0) return -(int64_t)EINVAL;

    int64_t n = is_write ? sc_write_impl(f, (const void *)(uintptr_t)buf_ptr, count)
                         : sc_read_impl(f, (void *)(uintptr_t)buf_ptr, count);
    vfs_seek(f->vfs_fd, cur, 0);
    return n;
}
static int64_t sc_pread64(uint64_t fd, uint64_t buf, uint64_t count, uint64_t off)  { return sc_pio_common(fd, buf, count, off, false); }
static int64_t sc_pwrite64(uint64_t fd, uint64_t buf, uint64_t count, uint64_t off) { return sc_pio_common(fd, buf, count, off, true); }

 


static int64_t sc_iov_common(uint64_t fd_arg, uint64_t iov_ptr, uint64_t iovcnt, bool is_write) {
    if (iovcnt == 0) return 0;
    if (iovcnt > SC_IOV_MAX) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)iov_ptr, (size_t)(iovcnt * sizeof(sc_iovec_t))))
        return -(int64_t)EFAULT;

    const sc_iovec_t *iov = (const sc_iovec_t *)(uintptr_t)iov_ptr;
    int64_t total = 0;
    for (uint64_t i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        int64_t r = is_write ? sc_write(fd_arg, iov[i].iov_base, iov[i].iov_len)
                             : sc_read(fd_arg, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((uint64_t)r < iov[i].iov_len) break;                      
    }
    return total;
}
static int64_t sc_readv(uint64_t fd, uint64_t iov, uint64_t cnt)  { return sc_iov_common(fd, iov, cnt, false); }
static int64_t sc_writev(uint64_t fd, uint64_t iov, uint64_t cnt) { return sc_iov_common(fd, iov, cnt, true); }

 

static int64_t sc_access(uint64_t path_ptr, uint64_t mode) {
    if (mode & ~7ULL) return -(int64_t)EINVAL;
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;
    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    if (r != VFS_OK) return sc_vfs_errno(r);
    if ((mode & SC_W_OK) && (vs.attr & 0x01u)) return -(int64_t)EACCES;    
    return 0;
}

 
static int64_t sc_truncate(uint64_t path_ptr, uint64_t length) {
    if ((int64_t)length < 0) return -(int64_t)EINVAL;
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;
    int r = vfs_truncate(path, length);
    return r == VFS_OK ? 0 : sc_vfs_errno(r);
}

 
static int64_t sc_getcwd(uint64_t buf_ptr, uint64_t size) {
    size_t len = strlen(g_cwd);
    if (size < len + 1) return -(int64_t)ERANGE;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, len + 1)) return -(int64_t)EFAULT;
    memcpy((void *)(uintptr_t)buf_ptr, g_cwd, len + 1);
    return (int64_t)(len + 1);
}

 


static int64_t sc_sendfile(uint64_t out_fd, uint64_t in_fd, uint64_t off_ptr, uint64_t count) {
    if (count == 0) return 0;
    sc_file_t *fo = sc_get_file((int)(uint32_t)out_fd);
    sc_file_t *fi = sc_get_file((int)(uint32_t)in_fd);
    if (!fo || !fi) return -(int64_t)EBADF;
    if (fi->is_pipe_read || fi->is_pipe_write || fi->vfs_fd < 0) return -(int64_t)EINVAL;
    if ((fi->flags & O_ACCMODE) == O_WRONLY || (fo->flags & O_ACCMODE) == O_RDONLY) return -(int64_t)EBADF;

    int64_t off = 0, saved = 0;
    bool use_off = (off_ptr != 0);
    if (use_off) {
        if (!syscall_validate_ptr((void *)(uintptr_t)off_ptr, 8)) return -(int64_t)EFAULT;
        off = *(int64_t *)(uintptr_t)off_ptr;
        if (off < 0) return -(int64_t)EINVAL;
        saved = vfs_seek(fi->vfs_fd, 0, 1);
        if (saved < 0) return -(int64_t)EINVAL;
        if (vfs_seek(fi->vfs_fd, off, 0) < 0) return -(int64_t)EINVAL;
    }

    uint8_t *tmp = (uint8_t *)kmalloc(4096);
    if (!tmp) {
        if (use_off) vfs_seek(fi->vfs_fd, saved, 0);
        return -(int64_t)ENOMEM;
    }

    int64_t total = 0;
    int64_t err = 0;
    while ((uint64_t)total < count) {
        uint64_t want = count - (uint64_t)total;
        if (want > 4096) want = 4096;
        int64_t r = sc_read_impl(fi, tmp, want);
        if (r < 0) { err = r; break; }
        if (r == 0) break;                                            
        int64_t w = sc_write_impl(fo, tmp, (uint64_t)r);
        if (w < 0) { err = w; break; }
        total += w;
        if (w < r) break;
    }
    kfree(tmp);

    if (use_off) {
        *(int64_t *)(uintptr_t)off_ptr = off + total;
        vfs_seek(fi->vfs_fd, saved, 0);
    }
    return total > 0 ? total : err;
}

 



static int64_t sc_getdents64(uint64_t fd_arg, uint64_t buf_ptr, uint64_t count) {
    if (count < 24) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)count))
        return -(int64_t)EFAULT;
    sc_file_t *f = sc_get_file((int)(uint32_t)fd_arg);
    if (!f || f->is_pipe_read || f->is_pipe_write || f->vfs_fd < 0) return -(int64_t)EBADF;

    vfs_stat_t st;
    if (vfs_fstat(f->vfs_fd, &st) != VFS_OK) return -(int64_t)EBADF;
    if (st.type != VFS_TYPE_DIR) return -(int64_t)ENOTDIR;

    uint8_t *out = (uint8_t *)(uintptr_t)buf_ptr;
    uint64_t used = 0;
    vfs_dirent_t ent;

    for (;;) {
        int r = vfs_readdir(f->vfs_fd, f->dir_pos, &ent);
        if (r == VFS_ERR_NOENT) break;                                
        if (r != VFS_OK) return used > 0 ? (int64_t)used : sc_vfs_errno(r);

        size_t nlen = strlen(ent.name);
        uint64_t reclen = ROUND_UP(19 + nlen + 1, 8);
        if (used + reclen > count) {
            if (used == 0) return -(int64_t)EINVAL;                   
            break;
        }
        uint8_t *rec = out + used;
        uint64_t ino = ent.cluster ? ent.cluster : (uint64_t)f->dir_pos + 1;
        int64_t  doff = (int64_t)f->dir_pos + 1;
        uint16_t rl = (uint16_t)reclen;
        uint8_t  dt = (ent.type == VFS_TYPE_DIR) ? SC_DT_DIR :
                      (ent.type == VFS_TYPE_FILE) ? SC_DT_REG : SC_DT_UNKNOWN;
        memcpy(rec + 0,  &ino,  8);
        memcpy(rec + 8,  &doff, 8);
        memcpy(rec + 16, &rl,   2);
        rec[18] = dt;
        memcpy(rec + 19, ent.name, nlen + 1);
        for (uint64_t k = 19 + nlen + 1; k < reclen; k++) rec[k] = 0;    

        used += reclen;
        f->dir_pos++;
    }
    return (int64_t)used;
}

 









static int64_t sc_kbd_read_char(uint64_t out_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)out_ptr, 1))
        return -(int64_t)EFAULT;
     



    task_t *cur = sched_current();
    if (!cur || !ui_kbd_focus_owned_by((int32_t)cur->tid)) {
         

        extern int ui_kbd_dbg_focus(int32_t *owner, int *minimized);
        static uint32_t s_deny = 0;
        if ((s_deny++ % 200) == 0) {
            int32_t ow; int mn;
            int fw = ui_kbd_dbg_focus(&ow, &mn);
            DBG_VAL("KBD", "sys_kbd_read_char DENIED, caller tid", cur ? cur->tid : 0);
            DBG_VAL("KBD", "  focus win (-1=none)", (uint64_t)(int64_t)fw);
            DBG_VAL("KBD", "  focus owner_pid", (uint64_t)(int64_t)ow);
            DBG_VAL("KBD", "  focus minimized", (uint64_t)mn);
        }
        return 0;
    }

    char ch;
    bool got = usb_hid_kbd_read_char(&ch);
    if (got) {
        *(char *)(uintptr_t)out_ptr = ch;
        DBG_VAL("KBD", "sys_kbd_read_char delivered char to tid", cur->tid);    
    }
    return got ? 1 : 0;
}

 






extern long fexox_os_klog_read(uint64_t *pos_io, char *ubuf, uint64_t max);

static int64_t sc_klog_read(uint64_t pos_ptr, uint64_t buf_ptr, uint64_t size) {
    if (size == 0 || size > (1u << 20)) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)pos_ptr, sizeof(uint64_t)) ||
        !syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)size))
        return -(int64_t)EFAULT;
    uint64_t pos = *(volatile uint64_t *)(uintptr_t)pos_ptr;
    long n = fexox_os_klog_read(&pos, (char *)(uintptr_t)buf_ptr, size);
    *(volatile uint64_t *)(uintptr_t)pos_ptr = pos;
    return (int64_t)n;
}

 

static inline bool sc_is_noisy(uint64_t nr) {
    return nr == SYS_KLOG_READ   || nr == SYS_INPUT_POLL || nr == SYS_KBD_READ_CHAR ||
           nr == SYS_WIN_GET_RECT|| nr == SYS_SCHED_YIELD|| nr == SYS_NANOSLEEP ||
           nr == SYS_CLOCK_GETTIME || nr == SYS_GETTIMEOFDAY || nr == SYS_GET_FRAMEBUFFER ||
           nr == SYS_TIME || nr == SYS_TIMES || nr == SYS_CLOCK_GETRES || nr == SYS_CLOCK_NANOSLEEP;
}

 










static int64_t sc_net_socket(uint64_t type) {
    if (type != SC_NET_SOCK_STREAM && type != SC_NET_SOCK_DGRAM) return -(int64_t)EINVAL;
    return net_socket((int)type);
}

static int64_t sc_net_connect(uint64_t sock, uint64_t ip, uint64_t port, uint64_t timeout_ms) {
    if (port > 0xFFFFu) return -(int64_t)EINVAL;
    return net_connect((int)sock, (ip4_t)ip, (uint16_t)port, (uint32_t)timeout_ms);
}

static int64_t sc_net_send(uint64_t sock, uint64_t buf_ptr, uint64_t len, uint64_t timeout_ms) {
    if (len == 0) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)len)) return -(int64_t)EFAULT;
    return net_send((int)sock, (const void *)(uintptr_t)buf_ptr, (size_t)len, (uint32_t)timeout_ms);
}

static int64_t sc_net_recv(uint64_t sock, uint64_t buf_ptr, uint64_t len, uint64_t timeout_ms) {
    if (len == 0) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)len)) return -(int64_t)EFAULT;
    return net_recv((int)sock, (void *)(uintptr_t)buf_ptr, (size_t)len, (uint32_t)timeout_ms);
}

static int64_t sc_net_close(uint64_t sock) {
    return net_close((int)sock);
}

 


static int64_t sc_net_resolve(uint64_t name_ptr, uint64_t out_ptr, uint64_t timeout_ms) {
    char name[256];
    if (sc_copy_string((const char *)(uintptr_t)name_ptr, name, sizeof(name)) < 0)
        return -(int64_t)EFAULT;
    if (!syscall_validate_ptr((void *)(uintptr_t)out_ptr, sizeof(ip4_t)))
        return -(int64_t)EFAULT;
    ip4_t ip;
    int r = dns_resolve(name, &ip, (uint32_t)timeout_ms);
    if (r == 0) *(ip4_t *)(uintptr_t)out_ptr = ip;
    return r;
}

static int64_t sc_net_status(uint64_t buf_ptr, uint64_t bufsz) {
    if (bufsz == 0) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)buf_ptr, (size_t)bufsz)) return -(int64_t)EFAULT;
    net_status_text((char *)(uintptr_t)buf_ptr, (size_t)bufsz);
    return 0;
}

 


                                                                         
                          
                                                                            
                                     
                                                                            
#define SC_E2BIG        7
#define SC_ENOTTY       25
#define SC_ETIMEDOUT    110

                                                                            
                                                                         
                                                                               
                                                                             
                                                                                
static bool sc_sig_default_fatal(int sig) {
    switch (sig) {
    case SC_SIGCHLD: case SC_SIGURG: case SC_SIGWINCH: case SC_SIGCONT:
    case SC_SIGSTOP: case 20: case 21: case 22:                                         
        return false;
    default:
        return true;
    }
}

static void sc_exit_signal(int sig) {
    sc_cur()->term_sig = sig;
    sc_exit_group((uint64_t)(128 + sig));                                                  
}

static void sc_terminate_other(sc_proc_t *p, int sig) {
    p->term_sig = sig;
    p->exit_code = 128 + sig;
    p->exit_set = true;
    sc_group_kill_others(p, SC_NO_TID);                                  
    sc_files_close_all(p);
    p->state = SC_PROC_ZOMBIE;
}

static int sc_signal_proc(sc_proc_t *p, int sig, bool is_self) {
    if (sig == 0) return 0;
    uint64_t bit = 1ULL << (sig - 1);
    sc_ksigaction_t *a = &p->sigact[sig];
    if (sig != SC_SIGKILL) {
        if (sig == SC_SIGSTOP)           return 0;
        if (a->handler == SC_SIG_IGN)    return 0;
        if (a->handler == SC_SIG_DFL && !sc_sig_default_fatal(sig)) return 0;
        if (p->sigmask & bit)            { p->sigpending |= bit; return 0; }
        if (a->handler != SC_SIG_DFL)    { p->sigpending |= bit; return 0; }                                  
    }
    if (is_self) sc_exit_signal(sig);
    sc_terminate_other(p, sig);
    return 0;
}

static void sc_raise_self(int sig) { sc_signal_proc(sc_cur(), sig, true); }

static int64_t sc_rt_sigaction(uint64_t sig, uint64_t act_ptr, uint64_t old_ptr, uint64_t sz) {
    if (sz != 8 || sig < 1 || sig >= SC_NSIG) return -(int64_t)EINVAL;
    sc_proc_t *p = sc_cur();
    if (old_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)old_ptr, sizeof(sc_ksigaction_t))) return -(int64_t)EFAULT;
        *(sc_ksigaction_t *)(uintptr_t)old_ptr = p->sigact[sig];
    }
    if (act_ptr) {
        if (sig == SC_SIGKILL || sig == SC_SIGSTOP) return -(int64_t)EINVAL;
        if (!syscall_validate_ptr((void *)(uintptr_t)act_ptr, sizeof(sc_ksigaction_t))) return -(int64_t)EFAULT;
        p->sigact[sig] = *(const sc_ksigaction_t *)(uintptr_t)act_ptr;
        if (p->sigact[sig].handler == SC_SIG_IGN ||
            (p->sigact[sig].handler == SC_SIG_DFL && !sc_sig_default_fatal((int)sig)))
            p->sigpending &= ~(1ULL << (sig - 1));
    }
    return 0;
}

static int64_t sc_rt_sigprocmask(uint64_t how, uint64_t set_ptr, uint64_t old_ptr, uint64_t sz) {
    if (sz != 8) return -(int64_t)EINVAL;
    sc_proc_t *p = sc_cur();
    if (old_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)old_ptr, 8)) return -(int64_t)EFAULT;
        *(uint64_t *)(uintptr_t)old_ptr = p->sigmask;
    }
    if (!set_ptr) return 0;
    if (!syscall_validate_ptr((void *)(uintptr_t)set_ptr, 8)) return -(int64_t)EFAULT;
    uint64_t set = *(const uint64_t *)(uintptr_t)set_ptr;
    switch ((int)how) {
    case SC_SIG_BLOCK:   p->sigmask |= set;  break;
    case SC_SIG_UNBLOCK: p->sigmask &= ~set; break;
    case SC_SIG_SETMASK: p->sigmask = set;   break;
    default: return -(int64_t)EINVAL;
    }
    p->sigmask &= ~((1ULL << (SC_SIGKILL - 1)) | (1ULL << (SC_SIGSTOP - 1)));
    for (int sg = 1; sg < SC_NSIG; sg++) {                                                            
        uint64_t b = 1ULL << (sg - 1);
        if ((p->sigpending & b) && !(p->sigmask & b)) {
            p->sigpending &= ~b;
            sc_signal_proc(p, sg, true);
        }
    }
    return 0;
}

static int64_t sc_sigaltstack(uint64_t ss_ptr, uint64_t old_ptr) {
    sc_proc_t *p = sc_cur();
    if (old_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)old_ptr, sizeof(sc_stack_t))) return -(int64_t)EFAULT;
        *(sc_stack_t *)(uintptr_t)old_ptr = p->altstack;
    }
    if (ss_ptr) {
        if (!syscall_validate_ptr((void *)(uintptr_t)ss_ptr, sizeof(sc_stack_t))) return -(int64_t)EFAULT;
        p->altstack = *(const sc_stack_t *)(uintptr_t)ss_ptr;
    }
    return 0;
}

static int64_t sc_kill_pid(int64_t pid, int64_t sig) {
    if (sig < 0 || sig >= SC_NSIG) return -(int64_t)EINVAL;
    if (pid <= 0) return -(int64_t)ESRCH;                                            
    sc_proc_t *me = sc_cur();
    sc_proc_t *p = sc_proc_find_pid((uint32_t)pid);
    if (!p || p->state == SC_PROC_ZOMBIE || !sc_group_alive(p)) return -(int64_t)ESRCH;
    return sc_signal_proc(p, (int)sig, p == me);
}
                                                                               
static int64_t sc_kill_tid(int64_t tid, int64_t sig) {
    if (sig < 0 || sig >= SC_NSIG) return -(int64_t)EINVAL;
    if (tid <= 0) return -(int64_t)ESRCH;
    sc_proc_t *r = sc_proc_find_tid((uint32_t)tid);
    if (!r || !sc_task_alive(r->tid)) return -(int64_t)ESRCH;
    sc_proc_t *p = r->leader ? r->leader : r;
    if (p->state == SC_PROC_ZOMBIE) return -(int64_t)ESRCH;
    return sc_signal_proc(p, (int)sig, p == sc_cur());
}
static int64_t sc_kill(uint64_t pid, uint64_t sig)           { return sc_kill_pid((int64_t)pid, (int64_t)sig); }
static int64_t sc_tkill(uint64_t tid, uint64_t sig)          { return sc_kill_tid((int64_t)(int32_t)tid, (int64_t)sig); }
static int64_t sc_tgkill(uint64_t tgid, uint64_t tid, uint64_t sig) { (void)tgid; return sc_kill_tid((int64_t)(int32_t)tid, (int64_t)sig); }

                                                                            
                                                                              
                                                                                
#define SC_FUTEX_SLOTS 256
typedef struct { bool used, woken; uint64_t key; uint32_t pid; } sc_fwait_t;
static sc_fwait_t g_fwait[SC_FUTEX_SLOTS];

static bool sc_futex_key(uint64_t uaddr, uint64_t *key) {
    uint64_t pa;
    if (!sc_user_page_present(uaddr, &pa)) return false;
    *key = pa + (uaddr & (PAGE_SIZE - 1));
    return true;
}
static int sc_futex_wake_key(uint64_t key, int n) {
    uint32_t tids[SC_FUTEX_SLOTS];
    int c = 0;
    uint64_t fl = sc_irq_save();
    for (int i = 0; i < SC_FUTEX_SLOTS && c < n; i++)
        if (g_fwait[i].used && !g_fwait[i].woken && g_fwait[i].key == key) {
            g_fwait[i].woken = true;
            tids[c++] = g_fwait[i].pid;
        }
    sc_irq_restore(fl);
    for (int i = 0; i < c; i++) sched_wake(tids[i]);                                           
    return c;
}
static void sc_futex_cleanup(uint32_t pid) {
    uint64_t fl = sc_irq_save();
    for (int i = 0; i < SC_FUTEX_SLOTS; i++)
        if (g_fwait[i].used && g_fwait[i].pid == pid) g_fwait[i].used = false;
    sc_irq_restore(fl);
}
                                                                              
static void sc_exit_clear_tid(sc_proc_t *p) {
    uint64_t a = p->clear_child_tid;
    if (!a) return;
    uint64_t key;
    if ((a & 3) == 0 && a <= USER_VIRT_LIMIT && sc_futex_key(a, &key)) {
        *(volatile uint32_t *)(uintptr_t)a = 0;
        sc_futex_wake_key(key, 0x7FFFFFFF);
    }
    p->clear_child_tid = 0;
}

static int64_t sc_futex(uint64_t uaddr, uint64_t op, uint64_t val, uint64_t timeout_ptr,
                        uint64_t uaddr2, uint64_t val3) {
    int cmd = (int)(op & 0x7F);
    if ((uaddr & 3) || !syscall_validate_ptr((void *)(uintptr_t)uaddr, 4)) return -(int64_t)EFAULT;
    uint64_t key;
    if (!sc_futex_key(uaddr, &key)) return -(int64_t)EFAULT;

    switch (cmd) {
    case SC_FUTEX_WAIT:
    case SC_FUTEX_WAIT_BITSET: {
        bool has = false;
        uint64_t deadline = 0;
        if (timeout_ptr) {
            if (!syscall_validate_ptr((void *)(uintptr_t)timeout_ptr, sizeof(sc_timespec_t))) return -(int64_t)EFAULT;
            const sc_timespec_t *ts = (const sc_timespec_t *)(uintptr_t)timeout_ptr;
            if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) return -(int64_t)EINVAL;
            uint64_t ns = (uint64_t)ts->tv_sec * 1000000000ULL + (uint64_t)ts->tv_nsec;
            if (cmd == SC_FUTEX_WAIT) deadline = sc_mono_ns() + ns;
            else if (op & 256) {                                                                 
                int64_t rel = (int64_t)ns - sc_realtime_ns();
                deadline = sc_mono_ns() + (uint64_t)(rel > 0 ? rel : 0);
            } else deadline = ns;                                                                 
            has = true;
        }
        uint32_t pid = sched_current() ? sched_current()->tid : 0;                           
        uint64_t fl = sc_irq_save();                                                                                       
        int slot = -1;
        for (int i = 0; i < SC_FUTEX_SLOTS; i++) if (!g_fwait[i].used) { slot = i; break; }
        if (slot < 0) { sc_irq_restore(fl); return -(int64_t)EAGAIN; }
        g_fwait[slot].used = true; g_fwait[slot].woken = false; g_fwait[slot].key = key; g_fwait[slot].pid = pid;
        uint32_t cur = *(volatile uint32_t *)(uintptr_t)uaddr;
        sc_irq_restore(fl);
        if (cur != (uint32_t)val) { g_fwait[slot].used = false; return -(int64_t)EAGAIN; }
        int64_t rc = 0;
        for (;;) {
            if (g_fwait[slot].woken) break;
            if (has && sc_mono_ns() >= deadline) { rc = -(int64_t)SC_ETIMEDOUT; break; }
            sc_check_exit_request();
            {
                uint64_t chunk = 5000000ULL;
                if (has) {
                    uint64_t now = sc_mono_ns();
                    if (deadline > now && deadline - now < chunk) chunk = deadline - now;
                }
                sched_sleep_ns(chunk);
            }
        }
        g_fwait[slot].used = false;
        return rc;
    }
    case SC_FUTEX_WAKE:
    case SC_FUTEX_WAKE_BITSET:
        return sc_futex_wake_key(key, (int)(int32_t)val < 0 ? 0 : (int)(int32_t)val);
    case SC_FUTEX_REQUEUE:
    case SC_FUTEX_CMP_REQUEUE: {
        uint64_t key2;
        if ((uaddr2 & 3) || !syscall_validate_ptr((void *)(uintptr_t)uaddr2, 4) || !sc_futex_key(uaddr2, &key2))
            return -(int64_t)EFAULT;
        if (cmd == SC_FUTEX_CMP_REQUEUE && *(volatile uint32_t *)(uintptr_t)uaddr != (uint32_t)val3)
            return -(int64_t)EAGAIN;
        int woken = sc_futex_wake_key(key, (int)(int32_t)val);
        int moved = 0, lim = (int)(int32_t)timeout_ptr;                                                  
        uint64_t fl = sc_irq_save();
        for (int i = 0; i < SC_FUTEX_SLOTS && moved < lim; i++)
            if (g_fwait[i].used && !g_fwait[i].woken && g_fwait[i].key == key) { g_fwait[i].key = key2; moved++; }
        sc_irq_restore(fl);
        return woken + moved;
    }
    default:
        return -(int64_t)ENOSYS;
    }
}

static int64_t sc_set_tid_address(uint64_t tidptr) {
    sc_proc_t *p = sc_thr();                                  
    p->clear_child_tid = tidptr;
    task_t *t = sched_current();
    return (int64_t)(t ? t->tid : p->pid);
}

                                                                             
static void sc_fill_kstat(sc_kstat_t *o, const vfs_stat_t *v) {
    memset(o, 0, sizeof(*o));
    bool dir = (v->type == VFS_TYPE_DIR);
    o->st_dev     = 1;
    o->st_ino     = v->cluster ? v->cluster : 1;
    o->st_nlink   = dir ? 2 : 1;
    o->st_mode    = dir ? (SC_S_IFDIR | 0755) : (SC_S_IFREG | ((v->attr & 1u) ? 0444 : 0644));
    o->st_size    = (int64_t)v->size;
    o->st_blksize = 4096;
    uint64_t al   = v->alloc_size ? v->alloc_size : v->size;
    o->st_blocks  = (int64_t)((al + 511) / 512);
    o->st_atime_sec = (int64_t)v->atime;
    o->st_mtime_sec = (int64_t)v->mtime;
    o->st_ctime_sec = (int64_t)v->ctime;
}

static int64_t sc_fstat_file(sc_file_t *f, sc_kstat_t *o) {
    if (f->is_pipe_read || f->is_pipe_write) {
        memset(o, 0, sizeof(*o));
        o->st_dev = 2; o->st_nlink = 1; o->st_mode = SC_S_IFIFO | 0600; o->st_blksize = 4096;
        return 0;
    }
    if (f->vfs_fd < 0) {                                                      
        memset(o, 0, sizeof(*o));
        o->st_dev = 3; o->st_nlink = 1; o->st_mode = SC_S_IFCHR | 0620;
        o->st_rdev = (4ULL << 8) | 1; o->st_blksize = 1024;
        return 0;
    }
    vfs_stat_t vs;
    int r = vfs_fstat(f->vfs_fd, &vs);
    if (r != VFS_OK) return -(int64_t)EBADF;
    sc_fill_kstat(o, &vs);
    return 0;
}

static int64_t sc_stat_lx(uint64_t path_ptr, uint64_t st_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)st_ptr, sizeof(sc_kstat_t))) return -(int64_t)EFAULT;
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0) return -(int64_t)EFAULT;
    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    if (r != VFS_OK) return sc_vfs_errno(r);
    sc_fill_kstat((sc_kstat_t *)(uintptr_t)st_ptr, &vs);
    return 0;
}

static int64_t sc_fstat_lx(uint64_t fd_arg, uint64_t st_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)st_ptr, sizeof(sc_kstat_t))) return -(int64_t)EFAULT;
    sc_file_t *f = sc_get_file((int)(uint32_t)fd_arg);
    if (!f) return -(int64_t)EBADF;
    return sc_fstat_file(f, (sc_kstat_t *)(uintptr_t)st_ptr);
}

                                                                                 
                                                                                    
static bool sc_at_ok(uint64_t dirfd, uint64_t path_ptr) {
    if ((int32_t)dirfd == AT_FDCWD) return true;
    if (!syscall_validate_ptr((void *)(uintptr_t)path_ptr, 1)) return true;                                
    return *(const char *)(uintptr_t)path_ptr == '/';
}

static int64_t sc_newfstatat(uint64_t dirfd, uint64_t path_ptr, uint64_t st_ptr, uint64_t flags) {
    if (!syscall_validate_ptr((void *)(uintptr_t)path_ptr, 1)) return -(int64_t)EFAULT;
    if (*(const char *)(uintptr_t)path_ptr == '\0') {
        if (flags & AT_EMPTY_PATH) return sc_fstat_lx(dirfd, st_ptr);
        return -(int64_t)ENOENT;
    }
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return sc_stat_lx(path_ptr, st_ptr);
}

                                                                             
typedef struct { uint32_t c_iflag, c_oflag, c_cflag, c_lflag; uint8_t c_line; uint8_t c_cc[19]; } sc_ktermios_t;
typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } sc_winsize_t;
static sc_ktermios_t g_ktermios = {
    0x0500, 0x0005, 0x00BF, 0x8A3B, 0,
    { 3, 0x1C, 0x7F, 0x15, 4, 0, 1 }                                                        
};

static int64_t sc_ioctl(uint64_t fd_arg, uint64_t req, uint64_t arg) {
    sc_file_t *f = sc_get_file((int)(uint32_t)fd_arg);
    if (!f) return -(int64_t)EBADF;

    switch ((uint32_t)req) {
    case SC_FIONBIO:
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, 4)) return -(int64_t)EFAULT;
        if (*(const int32_t *)(uintptr_t)arg) f->flags |= LX_O_NONBLOCK; else f->flags &= ~LX_O_NONBLOCK;
        return 0;
    case SC_FIOCLEX:  f->fd_flags |= FD_CLOEXEC;  return 0;
    case SC_FIONCLEX: f->fd_flags &= ~FD_CLOEXEC; return 0;
    case SC_FIONREAD: {
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, 4)) return -(int64_t)EFAULT;
        int32_t n = 0;
        if (f->is_pipe_read) n = (int32_t)g_pipes[f->pipe_idx].count;
        else if (f->vfs_fd >= 0) {
            vfs_stat_t vs; int64_t pos = vfs_seek(f->vfs_fd, 0, 1);
            if (vfs_fstat(f->vfs_fd, &vs) == VFS_OK && pos >= 0 && vs.size > (uint64_t)pos) n = (int32_t)(vs.size - (uint64_t)pos);
        }
        *(int32_t *)(uintptr_t)arg = n;
        return 0;
    }
    default: break;
    }

    if (!sc_is_console(f)) return -(int64_t)SC_ENOTTY;                                                  

    switch ((uint32_t)req) {
    case SC_TCGETS:
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, sizeof(sc_ktermios_t))) return -(int64_t)EFAULT;
        *(sc_ktermios_t *)(uintptr_t)arg = g_ktermios;
        return 0;
    case SC_TCSETS: case SC_TCSETSW: case SC_TCSETSF:
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, sizeof(sc_ktermios_t))) return -(int64_t)EFAULT;
        g_ktermios = *(const sc_ktermios_t *)(uintptr_t)arg;
        return 0;
    case SC_TIOCGWINSZ: {
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, sizeof(sc_winsize_t))) return -(int64_t)EFAULT;
        sc_winsize_t *w = (sc_winsize_t *)(uintptr_t)arg;
        w->ws_row = 25; w->ws_col = 80; w->ws_xpixel = 0; w->ws_ypixel = 0;
        return 0;
    }
    case SC_TIOCSWINSZ: return 0;
    case SC_TIOCGPGRP:
        if (!syscall_validate_ptr((void *)(uintptr_t)arg, 4)) return -(int64_t)EFAULT;
        *(int32_t *)(uintptr_t)arg = (int32_t)sc_cur()->pid;
        return 0;
    case SC_TIOCSPGRP: return 0;
    default: return -(int64_t)SC_ENOTTY;
    }
}

                                                                             
static int sc_poll_one(sc_pollfd_t *p) {
    p->revents = 0;
    if (p->fd < 0) return 0;
    sc_file_t *f = sc_get_file(p->fd);
    if (!f) { p->revents = SC_POLLNVAL; return 1; }
    int16_t r = 0;
    if (f->is_pipe_read) {
        pipe_t *pp = &g_pipes[f->pipe_idx];
        if (pp->count) r |= SC_POLLIN;
        if (pp->write_closed) r |= SC_POLLHUP;
    } else if (f->is_pipe_write) {
        pipe_t *pp = &g_pipes[f->pipe_idx];
        if (pp->read_closed) r |= SC_POLLERR;
        else if (pp->count < PIPE_BUF_SIZE) r |= SC_POLLOUT;
    } else if (f->vfs_fd < 0) {                                               
        task_t *cur = sched_current();
        if ((f->flags & O_ACCMODE) != O_WRONLY && cur && ui_kbd_focus_owned_by((int32_t)cur->tid)) r |= SC_POLLIN;
        if ((f->flags & O_ACCMODE) != O_RDONLY) r |= SC_POLLOUT;
    } else {
        r = SC_POLLIN | SC_POLLOUT;                                                                  
    }
    p->revents = (int16_t)(r & (p->events | SC_POLLERR | SC_POLLHUP | SC_POLLNVAL));
    return p->revents != 0;
}

static int64_t sc_poll(uint64_t fds_ptr, uint64_t nfds, uint64_t timeout_arg) {
    if (nfds > SC_FD_MAX * 2) return -(int64_t)EINVAL;
    if (nfds && !syscall_validate_ptr((void *)(uintptr_t)fds_ptr, (size_t)(nfds * sizeof(sc_pollfd_t))))
        return -(int64_t)EFAULT;
    sc_pollfd_t *fds = (sc_pollfd_t *)(uintptr_t)fds_ptr;
    int64_t tmo = (int64_t)(int32_t)timeout_arg;                                       
    uint64_t start = sc_mono_ns();
    for (;;) {
        int ready = 0;
        for (uint64_t i = 0; i < nfds; i++) ready += sc_poll_one(&fds[i]);
        if (ready) return ready;
        if (tmo == 0) return 0;
        if (tmo > 0 && sc_mono_ns() - start >= (uint64_t)tmo * 1000000ULL) return 0;
        sc_check_exit_request();
        sched_sleep_ns(500000);
    }
}

                                                                               
static int64_t sc_openat(uint64_t dirfd, uint64_t path_ptr, uint64_t flags, uint64_t mode) {
    (void)mode;
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return sc_open(path_ptr, flags);
}
static int64_t sc_mkdirat(uint64_t dirfd, uint64_t path_ptr, uint64_t mode) {
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return sc_mkdir(path_ptr, mode);
}
static int64_t sc_unlinkat(uint64_t dirfd, uint64_t path_ptr, uint64_t flags) {
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return (flags & AT_REMOVEDIR) ? sc_rmdir(path_ptr) : sc_unlink(path_ptr);
}
static int64_t sc_renameat(uint64_t odfd, uint64_t op, uint64_t ndfd, uint64_t np, uint64_t flags) {
    if (flags) return -(int64_t)EINVAL;
    if (!sc_at_ok(odfd, op) || !sc_at_ok(ndfd, np)) return -(int64_t)ENOSYS;
    return sc_rename(op, np);
}
static int64_t sc_faccessat(uint64_t dirfd, uint64_t path_ptr, uint64_t mode) {
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return sc_access(path_ptr, mode);
}
                                                                            
static int64_t sc_readlink(uint64_t path_ptr, uint64_t buf_ptr, uint64_t size) {
    (void)buf_ptr; (void)size;
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0) return -(int64_t)EFAULT;
    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    return r == VFS_OK ? -(int64_t)EINVAL : sc_vfs_errno(r);
}
static int64_t sc_readlinkat(uint64_t dirfd, uint64_t path_ptr, uint64_t buf_ptr, uint64_t size) {
    if (!sc_at_ok(dirfd, path_ptr)) return -(int64_t)ENOSYS;
    return sc_readlink(path_ptr, buf_ptr, size);
}

static int64_t sc_dup3(uint64_t oldfd, uint64_t newfd, uint64_t flags) {
    if (oldfd == newfd) return -(int64_t)EINVAL;
    if (flags & ~(uint64_t)LX_O_CLOEXEC) return -(int64_t)EINVAL;
    int64_t r = sc_dup2(oldfd, newfd);
    if (r >= 0 && (flags & LX_O_CLOEXEC)) g_files[(int)r].fd_flags = FD_CLOEXEC;
    return r;
}
static int64_t sc_pipe2(uint64_t pipefd_ptr, uint64_t flags) {
    if (flags & ~(uint64_t)(LX_O_CLOEXEC | LX_O_NONBLOCK)) return -(int64_t)EINVAL;
    int64_t r = sc_pipe(pipefd_ptr);
    if (r < 0) return r;
    const int32_t *fds = (const int32_t *)(uintptr_t)pipefd_ptr;
    for (int i = 0; i < 2; i++) {
        sc_file_t *f = sc_get_file(fds[i]);
        if (!f) continue;
        if (flags & LX_O_CLOEXEC)  f->fd_flags |= FD_CLOEXEC;
        if (flags & LX_O_NONBLOCK) f->flags |= LX_O_NONBLOCK;
    }
    return 0;
}

                                                                               
static int64_t sc_umask(uint64_t m) { sc_proc_t *p = sc_cur(); uint32_t o = p->umask_val; p->umask_val = (uint32_t)(m & 0777); return o; }

static void sc_rlim_fill(uint64_t res, sc_rlimit_t *o) {
    o->rlim_cur = o->rlim_max = SC_RLIM_INFINITY;
    if (res == SC_RLIMIT_STACK)  o->rlim_cur = o->rlim_max = 128ULL * PAGE_SIZE;                                   
    if (res == SC_RLIMIT_NOFILE) o->rlim_cur = o->rlim_max = SC_FD_MAX;
}
static int64_t sc_getrlimit(uint64_t res, uint64_t ptr) {
    if (res > 15) return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void *)(uintptr_t)ptr, sizeof(sc_rlimit_t))) return -(int64_t)EFAULT;
    sc_rlim_fill(res, (sc_rlimit_t *)(uintptr_t)ptr);
    return 0;
}
static int64_t sc_prlimit64(uint64_t pid, uint64_t res, uint64_t newp, uint64_t oldp) {
    if (pid != 0 && pid != sc_cur()->pid) return -(int64_t)ESRCH;
    if (res > 15) return -(int64_t)EINVAL;
    if (oldp) {
        if (!syscall_validate_ptr((void *)(uintptr_t)oldp, sizeof(sc_rlimit_t))) return -(int64_t)EFAULT;
        sc_rlim_fill(res, (sc_rlimit_t *)(uintptr_t)oldp);
    }
    return newp ? -(int64_t)EPERM : 0;                                                               
}

static int64_t sc_prctl(uint64_t option, uint64_t a2) {
    task_t *t = sched_current();
    if (option == 15 && t) {                                                       
        if (!syscall_validate_ptr((void *)(uintptr_t)a2, 1)) return -(int64_t)EFAULT;
        strncpy(t->name, (const char *)(uintptr_t)a2, 15); t->name[15] = '\0';
        return 0;
    }
    if (option == 16 && t) {                                                       
        if (!syscall_validate_ptr((void *)(uintptr_t)a2, 16)) return -(int64_t)EFAULT;
        strncpy((char *)(uintptr_t)a2, t->name, 15); ((char *)(uintptr_t)a2)[15] = '\0';
        return 0;
    }
    return -(int64_t)EINVAL;
}

                                                                                    
static int64_t sc_perm_path(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_path((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0) return -(int64_t)EFAULT;
    vfs_stat_t vs;
    int r = vfs_stat(path, &vs);
    return r == VFS_OK ? 0 : sc_vfs_errno(r);
}
static int64_t sc_perm_fd(uint64_t fd) { return sc_get_file((int)(uint32_t)fd) ? 0 : -(int64_t)EBADF; }

static int64_t sc_dispatch_inner(uint64_t nr,
                         uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    if (nr != SYS_EXIT) sc_check_exit_request();
    if (!sc_is_noisy(nr)) DBG_VAL("SC", "syscall nr", nr);

     

    if (nr == SYS_WIN_GET_RECT || nr == SYS_INPUT_POLL || nr == SYS_KBD_READ_CHAR || nr == SYS_NANOSLEEP) {
        static uint32_t cnt[4];
        int k = (nr == SYS_WIN_GET_RECT) ? 0 : (nr == SYS_INPUT_POLL) ? 1 : (nr == SYS_KBD_READ_CHAR) ? 2 : 3;
        if (cnt[k] < 3 || (cnt[k] % 500) == 0) {
            task_t *dc = sched_current();
            DBG_VAL("DIAG", "noisy syscall nr", nr);
            DBG_VAL("DIAG", "  caller tid", dc ? dc->tid : 0);
            DBG_VAL("DIAG", "  call count", cnt[k]);
        }
        cnt[k]++;
    }

    switch (nr) {
                         
    case SYS_READ:          return sc_read(a1, a2, a3);
    case SYS_WRITE:         return sc_write(a1, a2, a3);
    case SYS_OPEN:          return sc_open(a1, a2);
    case SYS_OPENAT:        return sc_openat(a1, a2, a3, a4);
    case SYS_CLOSE:         return sc_close(a1);
    case SYS_STAT:
    case SYS_LSTAT:         return sc_stat_lx(a1, a2);
    case SYS_FSTAT:         return sc_fstat_lx(a1, a2);
    case SYS_NEWFSTATAT:    return sc_newfstatat(a1, a2, a3, a4);
    case SYS_STAT_FEX:      return sc_stat_fex(a1, a2);                                    
    case SYS_FSTAT_FEX:     return sc_fstat_fex(a1, a2);
    case SYS_POLL:          return sc_poll(a1, a2, a3);
    case SYS_LSEEK:         return sc_lseek(a1, a2, a3);
    case SYS_IOCTL:         return sc_ioctl(a1, a2, a3);
    case SYS_READDIR:                                                                   
        return (a1 > 0x10000ULL) ? sc_readlink(a1, a2, a3) : sc_readdir(a1, a2, a3);
    case SYS_READLINKAT:    return sc_readlinkat(a1, a2, a3, a4);
    case SYS_FTRUNCATE:     return sc_ftruncate(a1, a2);
    case SYS_FCNTL:         return sc_fcntl(a1, a2, a3);
    case SYS_MKDIR:         return sc_mkdir(a1, a2);
    case SYS_MKDIRAT:       return sc_mkdirat(a1, a2, a3);
    case SYS_RMDIR:         return sc_rmdir(a1);
    case SYS_UNLINK:        return sc_unlink(a1);
    case SYS_UNLINKAT:      return sc_unlinkat(a1, a2, a3);
    case SYS_RENAME:        return sc_rename(a1, a2);
    case SYS_RENAMEAT:      return sc_renameat(a1, a2, a3, a4, 0);
    case SYS_RENAMEAT2:     return sc_renameat(a1, a2, a3, a4, a5);
    case SYS_CHDIR:         return sc_chdir(a1);
    case SYS_PREAD64:       return sc_pread64(a1, a2, a3, a4);
    case SYS_PWRITE64:      return sc_pwrite64(a1, a2, a3, a4);
    case SYS_READV:         return sc_readv(a1, a2, a3);
    case SYS_WRITEV:
    case SYS_WRITEV_LEGACY: return sc_writev(a1, a2, a3);
    case SYS_ACCESS:        return sc_access(a1, a2);
    case SYS_FACCESSAT:
    case SYS_FACCESSAT2:    return sc_faccessat(a1, a2, a3);
    case SYS_DUP:           return sc_dup(a1);
    case SYS_DUP2:          return sc_dup2(a1, a2);
    case SYS_DUP3:          return sc_dup3(a1, a2, a3);
    case SYS_SENDFILE:      return sc_sendfile(a1, a2, a3, a4);
    case SYS_TRUNCATE:      return sc_truncate(a1, a2);
    case SYS_GETCWD:        return sc_getcwd(a1, a2);
    case SYS_GETDENTS64:    return sc_getdents64(a1, a2, a3);
    case SYS_PIPE:
    case SYS_PIPE_LEGACY:   return sc_pipe(a1);
    case SYS_PIPE2:         return sc_pipe2(a1, a2);
    case SYS_FSYNC:
    case SYS_FDATASYNC:     return sc_perm_fd(a1);                                               
    case SYS_SYNC:          return 0;
    case SYS_FADVISE64:     return sc_perm_fd(a1);
    case SYS_CHMOD:
    case SYS_CHOWN:
    case SYS_LCHOWN:        return sc_perm_path(a1);                                           
    case SYS_FCHMOD:
    case SYS_FCHOWN:        return sc_perm_fd(a1);
    case SYS_FCHMODAT:      return sc_at_ok(a1, a2) ? sc_perm_path(a2) : -(int64_t)ENOSYS;
    case SYS_UMASK:         return sc_umask(a1);
                          
    case SYS_BRK:           return sc_brk(a1);
    case SYS_MMAP:          return sc_mmap(a1, a2, a3, a4, a5, a6);
    case SYS_MUNMAP:        return sc_munmap(a1, a2);
    case SYS_MPROTECT:      return sc_mprotect(a1, a2, a3);
    case SYS_MREMAP:        return sc_mremap(a1, a2, a3, a4);
    case SYS_MINCORE:       return sc_mincore(a1, a2, a3);
    case SYS_MADVISE:       return sc_madvise(a1, a2, a3);
    case SYS_SYSINFO:       return sc_sysinfo(a1);
                             
    case SYS_EXIT:          return sc_exit(a1);
    case SYS_EXIT_GROUP:    return sc_exit_group(a1);
    case SYS_FORK:          return sc_fork();
    case SYS_VFORK:         return sc_fork_ex(CLONE_VFORK, 0, 0, 0, 0);
    case SYS_CLONE:         return sc_clone_lx(a1, a2, a3, a4, a5);
    case SYS_EXECVE:        return sc_execve(a1, a2, a3);
    case SYS_WAIT4:         return sc_wait4(a1, a2, a3);
    case SYS_GETPID:        return sc_getpid();
    case SYS_GETPPID:       return sc_getppid();
    case SYS_GETTID:        return sc_gettid();
    case SYS_SET_TID_ADDRESS: return sc_set_tid_address(a1);
    case SYS_GETUID: case SYS_GETEUID: case SYS_GETGID: case SYS_GETEGID:
        return 0;                                                                                          
    case SYS_SETPGID:       return 0;
    case SYS_GETPGRP:
    case SYS_GETPGID:
    case SYS_GETSID:
    case SYS_SETSID:        return (int64_t)sc_cur()->pid;
    case SYS_PRCTL:         return sc_prctl(a1, a2);
    case SYS_GETRLIMIT:     return sc_getrlimit(a1, a2);
    case SYS_PRLIMIT64:     return sc_prlimit64(a1, a2, a3, a4);
    case SYS_SCHED_YIELD:   return sc_sched_yield();
    case SYS_ARCH_PRCTL:    return sc_arch_prctl(a1, a2);
    case SYS_FUTEX:         return sc_futex(a1, a2, a3, a4, a5, a6);
                           
    case SYS_RT_SIGACTION:  return sc_rt_sigaction(a1, a2, a3, a4);
    case SYS_RT_SIGPROCMASK:return sc_rt_sigprocmask(a1, a2, a3, a4);
    case SYS_SIGALTSTACK:   return sc_sigaltstack(a1, a2);
    case SYS_KILL:          return sc_kill(a1, a2);
    case SYS_TKILL:         return sc_tkill(a1, a2);
    case SYS_TGKILL:        return sc_tgkill(a1, a2, a3);
                        
    case SYS_NANOSLEEP:     return sc_nanosleep(a1, a2);
    case SYS_CLOCK_GETTIME: return sc_clock_gettime(a1, a2);
    case SYS_GETTIMEOFDAY:  return sc_gettimeofday(a1, a2);
    case SYS_SETTIMEOFDAY:  return sc_settimeofday(a1, a2);
    case SYS_TIME:          return sc_time(a1);
    case SYS_TIMES:         return sc_times(a1);
    case SYS_GETRUSAGE:     return sc_getrusage(a1, a2);
    case SYS_CLOCK_SETTIME: return sc_clock_settime(a1, a2);
    case SYS_CLOCK_GETRES:  return sc_clock_getres(a1, a2);
    case SYS_CLOCK_NANOSLEEP: return sc_clock_nanosleep(a1, a2, a3, a4);
    case SYS_UNAME:         return sc_uname(a1);
    case SYS_GETRANDOM:     return sc_getrandom(a1, a2, a3);
                                  
    case SYS_SHM_GET:       return sc_shm_get(a1, a2, a3);
    case SYS_SHM_AT:        return sc_shm_at(a1, a2, a3);
    case SYS_SHM_DETACH:    return sc_shm_detach(a1, a2);
    case SYS_GET_FRAMEBUFFER: return sc_get_framebuffer(a1);
    case SYS_WIN_CREATE:      return sc_win_create(a1, a2, a3, a4);
    case SYS_WIN_CLOSE:       return sc_win_close(a1);
    case SYS_WIN_MOVE:        return sc_win_move(a1, a2, a3);
    case SYS_WIN_GET_RECT:    return sc_win_get_rect(a1, a2);
    case SYS_INPUT_POLL:      return sc_input_poll(a1);
    case SYS_KBD_READ_CHAR:   return sc_kbd_read_char(a1);
    case SYS_KLOG_READ:       return sc_klog_read(a1, a2, a3);
    case SYS_NET_SOCKET:      return sc_net_socket(a1);
    case SYS_NET_CONNECT:     return sc_net_connect(a1, a2, a3, a4);
    case SYS_NET_SEND:        return sc_net_send(a1, a2, a3, a4);
    case SYS_NET_RECV:        return sc_net_recv(a1, a2, a3, a4);
    case SYS_NET_CLOSE:       return sc_net_close(a1);
    case SYS_NET_RESOLVE:     return sc_net_resolve(a1, a2, a3);
    case SYS_NET_STATUS:      return sc_net_status(a1, a2);
    default:
        DBG_VAL("SC", "unknown syscall", nr);
        return -(int64_t)ENOSYS;
    }
}

 


void syscall_init(void) {
    memset(g_procs, 0, sizeof(g_procs));
    memset(g_pipes, 0, sizeof(g_pipes));
    memset(g_shm,   0, sizeof(g_shm));
    memset(g_vfs_refs, 0, sizeof(g_vfs_refs));
    memset(&g_proc0, 0, sizeof(g_proc0));
    g_cur_cache     = NULL;
    g_cur_cache_tid = SC_NO_TID;
    g_tsc_boot   = sc_rdtsc();
    g_rt_valid   = false;
    g_rng_state  = 0;
                                                                        
                                                                      

    DBG_MSG("SC", "syscall_init ok");
}

#ifdef __cplusplus
}
#endif


                                                                                 
                                                                                 
                                                                             
                                                                            
                                                                                    
static bool sc_needs_proc_lock(uint64_t nr) {
    switch (nr) {
    case SYS_MMAP: case SYS_MUNMAP: case SYS_MPROTECT: case SYS_MREMAP: case SYS_BRK: case SYS_MADVISE:
    case SYS_SHM_AT: case SYS_SHM_DETACH:
    case SYS_OPEN: case SYS_OPENAT: case SYS_CLOSE: case SYS_DUP: case SYS_DUP2: case SYS_DUP3:
    case SYS_PIPE: case SYS_PIPE2: case SYS_PIPE_LEGACY: case SYS_FCNTL:
    case SYS_CHDIR: case SYS_FCHDIR:
        return true;
    default:
        return false;
    }
}

int64_t syscall_dispatch(uint64_t nr,
                         uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    if (!sc_needs_proc_lock(nr))
        return sc_dispatch_inner(nr, a1, a2, a3, a4, a5, a6);

    sc_proc_t *L = sc_cur();
    task_t *t = sched_current();
    uint32_t me = (t ? t->tid : 0) + 1;
    for (;;) {
        uint64_t fl = sc_irq_save();
        uint32_t o = L->vm_owner;
        if (o == 0 || !sc_task_alive(o - 1)) { L->vm_owner = me; sc_irq_restore(fl); break; }
        sc_irq_restore(fl);
        sc_check_exit_request();
        sched_yield();
    }
    int64_t r = sc_dispatch_inner(nr, a1, a2, a3, a4, a5, a6);
    if (L->vm_owner == me) L->vm_owner = 0;
    return r;
}
