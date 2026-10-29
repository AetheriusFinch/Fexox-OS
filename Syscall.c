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

static sc_file_t g_files[SC_FD_MAX];

 
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

 



static uint64_t g_mmap_next = USER_MMAP_BASE;

static uint64_t mmap_bump_alloc(uint64_t size) {
    uint64_t addr = g_mmap_next;
    if (addr + size > USER_MMAP_LIMIT) return (uint64_t)-1;
    g_mmap_next = ROUND_UP(addr + size, PAGE_SIZE);
    return addr;
}

 











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

static sc_vma_t g_vma[SC_VMA_MAX];

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

 


static uint64_t g_brk_current = USER_HEAP_BASE;
static uint64_t g_brk_mapped  = USER_HEAP_BASE;

 


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

    sched_exit();             
    __builtin_unreachable();
}

 


static int64_t sc_getpid(void) {
    task_t *t = sched_current();
    if (!t) return 1;
    return (int64_t)(uint32_t)t->tid;
}

 


static int64_t sc_sched_yield(void) {
    sched_yield();
    return 0;
}

 












static int64_t sc_fork(void) {
     









    DBG_MSG("SC", "sys_fork: not fully implemented (no per-process PML4)");
    return -(int64_t)ENOSYS;
}

 














typedef struct {
    uint64_t entry_rip;     
    uint64_t user_rsp;      
    uint64_t tls_base;      
} clone_arg_t;

static void clone_trampoline(void *arg) {
    clone_arg_t *ca = (clone_arg_t *)arg;
    uint64_t rip = ca->entry_rip;
    uint64_t rsp = ca->user_rsp;
    uint64_t tls = ca->tls_base;
    kfree(ca);

     
    if (tls != 0) {
        __asm__ volatile ("wrfsbase %0" :: "r"(tls));
    }

     
    __asm__ volatile (
        "push $0x23\n\t"
        "push %[rsp]\n\t"
        "pushfq\n\t"
        "pop %%rax\n\t"
        "or $0x200, %%rax\n\t"
        "push %%rax\n\t"
        "push $0x1B\n\t"
        "push %[rip]\n\t"
        "xor %%rax, %%rax\n\t"    
        "xor %%rbx, %%rbx\n\t"
        "xor %%rcx, %%rcx\n\t"
        "xor %%rdx, %%rdx\n\t"
        "xor %%rsi, %%rsi\n\t"
        "xor %%rdi, %%rdi\n\t"
        "xor %%r8,  %%r8\n\t"
        "xor %%r9,  %%r9\n\t"
        "xor %%r10, %%r10\n\t"
        "xor %%r11, %%r11\n\t"
        "xor %%r12, %%r12\n\t"
        "xor %%r13, %%r13\n\t"
        "xor %%r14, %%r14\n\t"
        "xor %%r15, %%r15\n\t"
        "xor %%rbp, %%rbp\n\t"
        "iretq\n\t"
        :
        : [rip] "r"(rip), [rsp] "r"(rsp)
        : "rax", "memory"
    );
    __builtin_unreachable();
}

 

static int64_t sc_clone(uint64_t flags, uint64_t child_stack,
                        uint64_t user_rip, uint64_t tls) {
    (void)flags;  

    if (child_stack == 0 || !syscall_validate_ptr((void*)child_stack, 8))
        return -(int64_t)EINVAL;
    if (!syscall_validate_ptr((void*)user_rip, 1))
        return -(int64_t)EINVAL;

    clone_arg_t *ca = (clone_arg_t *)kmalloc(sizeof(clone_arg_t));
    if (!ca) return -(int64_t)ENOMEM;
    ca->entry_rip = user_rip;
    ca->user_rsp  = child_stack;
    ca->tls_base  = tls;

    int tid = sched_create_kthread(clone_trampoline, ca, "clone_thread", 0);
    if (tid < 0) {
        kfree(ca);
        return -(int64_t)ENOMEM;
    }
    DBG_VAL("SC", "sys_clone tid", (uint64_t)(uint32_t)tid);
    return (int64_t)(uint32_t)tid;
}

 










static int64_t sc_execve(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr) {
    (void)argv_ptr; (void)envp_ptr;

    char path[VFS_MAX_PATH];
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    DBG_MSG("SC", "sys_execve");
    DBG_VAL("SC", "execve path ptr", path_ptr);

    int tid = elf64_spawn(path, "execve", 0);
    if (tid < 0) {
        if (tid == ELF_ERR_IO)    return -(int64_t)ENOENT;
        if (tid == ELF_ERR_NOMEM) return -(int64_t)ENOMEM;
        if (tid == ELF_ERR_MAGIC || tid == ELF_ERR_CLASS ||
            tid == ELF_ERR_ARCH)  return -(int64_t)EACCES;
        return -(int64_t)EINVAL;
    }

     
    sched_exit();
    __builtin_unreachable();
}

 









static int64_t sc_wait4(uint64_t tid_arg, uint64_t wstatus_ptr, uint64_t options) {
    (void)options;

    if (wstatus_ptr != 0 && !syscall_validate_ptr((void*)wstatus_ptr, 4))
        return -(int64_t)EFAULT;

    if (tid_arg == (uint64_t)-1) {
         
        return -(int64_t)ECHILD;
    }

    uint32_t tid = (uint32_t)tid_arg;

     
    uint32_t spin = 0;
    while (1) {
        task_t *t = sched_find(tid);
        if (!t) {
             
            if (wstatus_ptr)
                *(int32_t *)(uintptr_t)wstatus_ptr = 0;
            return (int64_t)(uint32_t)tid;
        }
        if (t->state == TASK_ZOMBIE) {
            if (wstatus_ptr)
                *(int32_t *)(uintptr_t)wstatus_ptr = 0;
            return (int64_t)(uint32_t)tid;
        }
         
        sched_yield();
        spin++;
        if (spin > 100000) return -(int64_t)ECHILD;  
    }
}

 




static int64_t sc_open(uint64_t path_ptr, uint64_t flags) {
    char path[VFS_MAX_PATH];
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int vfd = vfs_open(path, (int)flags);
    if (vfd < 0) {
        if (vfd == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
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
    g_files[fd].flags          = (int)flags;
    g_files[fd].fd_flags       = 0;
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

 



 


static int64_t sc_read_impl(sc_file_t *f, void *buf_v, uint64_t count) {
     
    if (f->is_pipe_read) {
        pipe_t *p = &g_pipes[f->pipe_idx];
        if (!p->valid) return -(int64_t)EBADF;

         
        uint32_t waited = 0;
        while (p->count == 0) {
            if (p->write_closed) return 0;   
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
    if (f->vfs_fd < 0) return -(int64_t)EBADF;       

     
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -(int64_t)EBADF;

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
        if (!p->valid || p->read_closed) return -(int64_t)EPIPE;

        const uint8_t *buf = (const uint8_t *)buf_v;
        uint64_t written = 0;
        while (written < count) {
            if (p->count >= PIPE_BUF_SIZE) {
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
    if (f->is_pipe_read || f->is_pipe_write) return -(int64_t)EINVAL;

    int64_t offset = (int64_t)offset_arg;
    if ((int)whence == 0 && offset == 0) f->dir_pos = 0;    
    int64_t r = vfs_seek(f->vfs_fd, offset, (int)whence);
    if (r == VFS_ERR_OVERFLOW) return -(int64_t)EOVERFLOW;
    if (r == VFS_ERR_INVAL)    return -(int64_t)EINVAL;
    if (r < 0)                 return -(int64_t)EINVAL;
    return r;
}

 



static int64_t sc_stat(uint64_t path_ptr, uint64_t stat_ptr) {
    if (!syscall_validate_ptr((void *)(uintptr_t)stat_ptr, sizeof(sc_stat_t)))
        return -(int64_t)EFAULT;

    char path[VFS_MAX_PATH];
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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

 



static int64_t sc_fstat(uint64_t fd_arg, uint64_t stat_ptr) {
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
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
        return -(int64_t)EFAULT;

    int r = vfs_unlink(path);
    if (r == VFS_ERR_NOENT)  return -(int64_t)ENOENT;
    if (r == VFS_ERR_ISDIR)  return -(int64_t)EISDIR;
    if (r == VFS_ERR_ROFS)   return -(int64_t)EROFS;
    if (r < 0)               return -(int64_t)EIO;
    return 0;
}

 




static int64_t sc_rename(uint64_t old_ptr, uint64_t new_ptr) {
     




    (void)old_ptr; (void)new_ptr;
    DBG_MSG("SC", "sys_rename: ENOSYS (vfs_rename not implemented)");
    return -(int64_t)ENOSYS;
}

 






static char g_cwd[VFS_MAX_PATH] = "/";

static int64_t sc_chdir(uint64_t path_ptr) {
    char path[VFS_MAX_PATH];
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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
    } else {
        va = mmap_bump_alloc(npages << PAGE_SHIFT);
        if (va == (uint64_t)-1) return -(int64_t)ENOMEM;
    }

    if (flags & MAP_ANONYMOUS) {
         
        uint64_t pa = pmm_alloc_frames((size_t)npages, 1);
        if (pa == (uint64_t)-1) return -(int64_t)ENOMEM;

        uint8_t *kp = (uint8_t *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
        memset(kp, 0, (size_t)(npages << PAGE_SHIFT));

        for (uint64_t i = 0; i < npages; i++) {
            int r = vmm_map_user(va + (i << PAGE_SHIFT),
                                 pa + (i << PAGE_SHIFT), rw, exec);
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

     
    int64_t seek_r = vfs_seek(f->vfs_fd, (int64_t)offset, 0);
    if (seek_r < 0) {
        pmm_free_frames(pa, (size_t)npages);
        return -(int64_t)EINVAL;
    }

    int64_t nr = vfs_read(f->vfs_fd, kbuf, (uint64_t)(npages << PAGE_SHIFT));
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

    for (uint64_t i = 0; i < npages; i++) {
        uint64_t pva = va + (i << PAGE_SHIFT);
        if (!sc_user_unmap_page(pva))
            vmm_unmap(pva);    
    }

     



    sc_vma_t *v = sc_vma_containing(va);
    if (v) {
        uint64_t v_end = v->va + (v->npages << PAGE_SHIFT);
        uint64_t u_end = va + (npages << PAGE_SHIFT);
        if (va <= v->va && u_end >= v_end) {                        
            pmm_free_frames(v->pa, (size_t)v->npages);
            memset(v, 0, sizeof(*v));
        } else if (va > v->va && u_end >= v_end) {                  
            uint64_t keep = (va - v->va) >> PAGE_SHIFT;
            pmm_free_frames(v->pa + (keep << PAGE_SHIFT), (size_t)(v->npages - keep));
            v->npages = keep;
        } else if (va <= v->va && u_end < v_end) {                  
            uint64_t cut = (u_end - v->va) >> PAGE_SHIFT;
            pmm_free_frames(v->pa, (size_t)cut);
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
     






    if (addr > USER_VIRT_LIMIT) return -(int64_t)EINVAL;
    if (length == 0)            return -(int64_t)EINVAL;
    (void)prot;

    DBG_MSG("SC", "sys_mprotect: stub (no vma tracking)");
    return 0;
}

 




static int64_t sc_arch_prctl(uint64_t code, uint64_t addr) {
    switch (code) {
    case ARCH_SET_FS:
         

        __asm__ volatile ("wrfsbase %0" :: "r"(addr));
        return 0;
    case ARCH_GET_FS: {
        if (!syscall_validate_ptr((void *)(uintptr_t)addr, 8))
            return -(int64_t)EFAULT;
        uint64_t fs;
        __asm__ volatile ("rdfsbase %0" : "=r"(fs));
        *(uint64_t *)(uintptr_t)addr = fs;
        return 0;
    }
    case ARCH_SET_GS:
        __asm__ volatile ("wrgsbase %0" :: "r"(addr));
        return 0;
    case ARCH_GET_GS: {
        if (!syscall_validate_ptr((void *)(uintptr_t)addr, 8))
            return -(int64_t)EFAULT;
        uint64_t gs;
        __asm__ volatile ("rdgsbase %0" : "=r"(gs));
        *(uint64_t *)(uintptr_t)addr = gs;
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
        int r = vmm_map_user(va + ((uint64_t)i << PAGE_SHIFT),
                             shm->phys_base + ((uint64_t)i << PAGE_SHIFT),
                             rw, exec);
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
        vmm_unmap(ROUND_DOWN(va, PAGE_SIZE) + ((uint64_t)i << PAGE_SHIFT));
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
            int r = vmm_map_user(FB_USER_VA_BASE + (i << PAGE_SHIFT),
                                 page_base + (i << PAGE_SHIFT),
                                 true  , false  );
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
            sc_user_unmap_page(old_addr + (i << PAGE_SHIFT));
        pmm_free_frames(v->pa + (new_np << PAGE_SHIFT), (size_t)(old_np - new_np));
        v->npages = new_np;
        return (int64_t)old_addr;
    }

    if (!(flags & MREMAP_MAYMOVE)) return -(int64_t)ENOMEM;            

    uint64_t new_pa = pmm_alloc_frames((size_t)new_np, 1);
    if (new_pa == (uint64_t)-1) return -(int64_t)ENOMEM;
    uint8_t *kn = (uint8_t *)(uintptr_t)(new_pa + DIRECT_MAP_OFFSET);
    memset(kn, 0, (size_t)(new_np << PAGE_SHIFT));
    memcpy(kn, (const void *)(uintptr_t)(v->pa + DIRECT_MAP_OFFSET), (size_t)(old_np << PAGE_SHIFT));

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
        sc_user_unmap_page(old_addr + (i << PAGE_SHIFT));
    pmm_free_frames(v->pa, (size_t)old_np);

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
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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
    if (sc_copy_string((const char *)(uintptr_t)path_ptr, path, sizeof(path)) < 0)
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
           nr == SYS_CLOCK_GETTIME || nr == SYS_GETTIMEOFDAY || nr == SYS_GET_FRAMEBUFFER;
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

 


int64_t syscall_dispatch(uint64_t nr,
                         uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5) {
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
    case SYS_CLOSE:         return sc_close(a1);
    case SYS_STAT:          return sc_stat(a1, a2);
    case SYS_FSTAT:         return sc_fstat(a1, a2);
    case SYS_LSEEK:         return sc_lseek(a1, a2, a3);
    case SYS_READDIR:       return sc_readdir(a1, a2, a3);
    case SYS_FTRUNCATE:     return sc_ftruncate(a1, a2);
    case SYS_FCNTL:         return sc_fcntl(a1, a2, a3);
    case SYS_MKDIR:         return sc_mkdir(a1, a2);
    case SYS_RMDIR:         return sc_rmdir(a1);
    case SYS_UNLINK:        return sc_unlink(a1);
    case SYS_RENAME:        return sc_rename(a1, a2);
    case SYS_CHDIR:         return sc_chdir(a1);
    case SYS_PREAD64:       return sc_pread64(a1, a2, a3, a4);
    case SYS_PWRITE64:      return sc_pwrite64(a1, a2, a3, a4);
    case SYS_READV:         return sc_readv(a1, a2, a3);
    case SYS_WRITEV:        return sc_writev(a1, a2, a3);
    case SYS_ACCESS:        return sc_access(a1, a2);
    case SYS_DUP:           return sc_dup(a1);
    case SYS_DUP2:          return sc_dup2(a1, a2);
    case SYS_SENDFILE:      return sc_sendfile(a1, a2, a3, a4);
    case SYS_TRUNCATE:      return sc_truncate(a1, a2);
    case SYS_GETCWD:        return sc_getcwd(a1, a2);
    case SYS_GETDENTS64:    return sc_getdents64(a1, a2, a3);
     
    case SYS_BRK:           return sc_brk(a1);
    case SYS_MMAP:          return sc_mmap(a1, a2, a3, a4, a5, 0);
    case SYS_MUNMAP:        return sc_munmap(a1, a2);
    case SYS_MPROTECT:      return sc_mprotect(a1, a2, a3);
    case SYS_MREMAP:        return sc_mremap(a1, a2, a3, a4);
    case SYS_MINCORE:       return sc_mincore(a1, a2, a3);
    case SYS_MADVISE:       return sc_madvise(a1, a2, a3);
    case SYS_SYSINFO:       return sc_sysinfo(a1);
     
    case SYS_EXIT:          return sc_exit(a1);
    case SYS_FORK:          return sc_fork();
    case SYS_CLONE:         return sc_clone(a1, a2, a3, a4);
    case SYS_EXECVE:        return sc_execve(a1, a2, a3);
    case SYS_WAIT4:         return sc_wait4(a1, a2, a3);
    case SYS_GETPID:
    case SYS_GETPID2:       return sc_getpid();
    case SYS_SCHED_YIELD:   return sc_sched_yield();
    case SYS_NANOSLEEP:     return sc_nanosleep(a1, a2);
    case SYS_CLOCK_GETTIME: return sc_clock_gettime(a1, a2);
    case SYS_GETTIMEOFDAY:  return sc_gettimeofday(a1, a2);
    case SYS_UNAME:         return sc_uname(a1);
    case SYS_GETRANDOM:     return sc_getrandom(a1, a2, a3);
     
    case SYS_ARCH_PRCTL:    return sc_arch_prctl(a1, a2);
    case SYS_PIPE:          return sc_pipe(a1);
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
    memset(g_files, 0, sizeof(g_files));
    memset(g_pipes, 0, sizeof(g_pipes));
    memset(g_shm,   0, sizeof(g_shm));
    memset(g_vma,   0, sizeof(g_vma));
    memset(g_vfs_refs, 0, sizeof(g_vfs_refs));
    g_tsc_boot   = sc_rdtsc();       
    g_rt_valid   = false;            
    g_rng_state  = 0;

     



    g_files[0].open  = true; g_files[0].vfs_fd = -1; g_files[0].flags = O_RDONLY;
    g_files[1].open  = true; g_files[1].vfs_fd = -1; g_files[1].flags = O_WRONLY;
    g_files[2].open  = true; g_files[2].vfs_fd = -1; g_files[2].flags = O_WRONLY;

    g_brk_current = USER_HEAP_BASE;
    g_brk_mapped  = USER_HEAP_BASE;
    g_mmap_next   = USER_MMAP_BASE;

    DBG_MSG("SC", "syscall_init ok");
}

#ifdef __cplusplus
}
#endif
