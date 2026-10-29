/* Fexox OS Project, EULA GPLv2 */
 


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "ELF64.h"
#include "VFS.h"
#include "Scheduler.h"
#include "debug_out.h"

 


extern void    *kmalloc(size_t size);
extern void     kfree(void *ptr);
extern uint64_t pmm_alloc_frames(size_t count, size_t align);
extern void     pmm_free_frames(uint64_t pa, size_t count);
 
extern int      vmm_map_user(uint64_t va, uint64_t pa, bool rw, bool exec);
 


extern uint64_t vmm_create_user_pml4(void);
extern int      vmm_map_user_in(uint64_t pml4_phys, uint64_t va, uint64_t pa, bool rw, bool exec);
extern void     vmm_free_user_pml4(uint64_t pml4_phys);

 


#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1ULL))
#define ROUND_UP(n, a)  (((uint64_t)(n) + (uint64_t)(a) - 1ULL) & ~((uint64_t)(a) - 1ULL))
#define ROUND_DOWN(n,a) ((uint64_t)(n) & ~((uint64_t)(a) - 1ULL))

 
#define USER_STACK_TOP      0x00007FFF00000000ULL
#define USER_STACK_PAGES    64       

 

#define PIE_LOAD_BASE       0x0000000000400000ULL

 
#define ELF_MAX_PHDRS       64
#define ELF_MAX_PHDR_BYTES  (ELF_MAX_PHDRS * sizeof(Elf64_Phdr))

 
#define ELF_MAX_SEG_BYTES   (128ULL * 1024 * 1024)

 


int elf64_validate_header(const Elf64_Ehdr *ehdr) {
    if (!ehdr) return ELF_ERR_INVAL;

     
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        DBG_MSG("ELF", "bad magic");
        return ELF_ERR_MAGIC;
    }

     
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        DBG_MSG("ELF", "not ELF64");
        return ELF_ERR_CLASS;
    }

     
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        DBG_MSG("ELF", "not LE");
        return ELF_ERR_ENDIAN;
    }

     
    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT || ehdr->e_version != EV_CURRENT) {
        DBG_MSG("ELF", "bad version");
        return ELF_ERR_INVAL;
    }

     
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        DBG_MSG("ELF", "not ET_EXEC/ET_DYN");
        return ELF_ERR_TYPE;
    }

     
    if (ehdr->e_machine != EM_X86_64) {
        DBG_MSG("ELF", "not x86_64");
        return ELF_ERR_ARCH;
    }

     
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
        DBG_MSG("ELF", "bad phentsize");
        return ELF_ERR_INVAL;
    }

     
    if (ehdr->e_phnum == 0 || ehdr->e_phnum > ELF_MAX_PHDRS) {
        DBG_MSG("ELF", "phnum out of range");
        return ELF_ERR_INVAL;
    }

    return ELF_OK;
}

 



extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);
extern size_t strlen(const char *s);

 


int elf64_load(const char *path, elf64_image_t *out) {
    if (!path || !out) return ELF_ERR_INVAL;

     
    memset(out, 0, sizeof(elf64_image_t));

     






    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0 && path[0] != '/') {
        char abspath[256];
        size_t n = strlen(path);
        if (n < sizeof(abspath) - 1) {
            abspath[0] = '/';
            memcpy(abspath + 1, path, n + 1);
            fd = vfs_open(abspath, O_RDONLY);
        }
    }
    if (fd < 0) {
        DBG_MSG("ELF", "vfs_open failed");
        return ELF_ERR_IO;
    }

     
    Elf64_Ehdr ehdr;
    int64_t nr = vfs_read(fd, &ehdr, sizeof(Elf64_Ehdr));
    if (nr != (int64_t)sizeof(Elf64_Ehdr)) {
        DBG_MSG("ELF", "short read on ehdr");
        vfs_close(fd);
        return ELF_ERR_IO;
    }

     
    int vr = elf64_validate_header(&ehdr);
    if (vr != ELF_OK) {
        vfs_close(fd);
        return vr;
    }

    DBG_VAL("ELF", "e_type",  ehdr.e_type);
    DBG_VAL("ELF", "e_entry", ehdr.e_entry);
    DBG_VAL("ELF", "e_phnum", ehdr.e_phnum);

     
    size_t phdr_bytes = (size_t)ehdr.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *phdrs = (Elf64_Phdr *)kmalloc(phdr_bytes);
    if (!phdrs) {
        DBG_MSG("ELF", "kmalloc phdrs OOM");
        vfs_close(fd);
        return ELF_ERR_NOMEM;
    }

    if (vfs_seek(fd, (int64_t)ehdr.e_phoff, 0) < 0) {
        DBG_MSG("ELF", "seek to phoff failed");
        kfree(phdrs);
        vfs_close(fd);
        return ELF_ERR_IO;
    }

    nr = vfs_read(fd, phdrs, phdr_bytes);
    if (nr != (int64_t)phdr_bytes) {
        DBG_MSG("ELF", "short read on phdrs");
        kfree(phdrs);
        vfs_close(fd);
        return ELF_ERR_IO;
    }

     
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_INTERP) {
            DBG_MSG("ELF", "PT_INTERP found: dynamic linking not supported");
            kfree(phdrs);
            vfs_close(fd);
            return ELF_ERR_INTERP;
        }
    }

     



    out->pml4_phys = vmm_create_user_pml4();
    if (out->pml4_phys == (uint64_t)-1) {
        DBG_MSG("ELF", "vmm_create_user_pml4 OOM");
        out->pml4_phys = 0;
        kfree(phdrs);
        vfs_close(fd);
        return ELF_ERR_NOMEM;
    }

     



    uint64_t load_base = (ehdr.e_type == ET_DYN) ? PIE_LOAD_BASE : 0ULL;
    out->load_base = load_base;
    out->entry     = ehdr.e_entry + load_base;

    DBG_VAL("ELF", "load_base",  load_base);
    DBG_VAL("ELF", "entry_virt", out->entry);

     
    uint32_t nseg = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        if (nseg >= ELF64_MAX_SEGS) {
            DBG_MSG("ELF", "too many PT_LOAD segments");
             
            goto err_free_segs;
        }

        if (ph->p_memsz == 0) continue;

         
        if (ph->p_memsz > ELF_MAX_SEG_BYTES || ph->p_filesz > ph->p_memsz) {
            DBG_MSG("ELF", "invalid segment sizes");
            goto err_free_segs;
        }

         
        uint64_t seg_vaddr = ROUND_DOWN(ph->p_vaddr + load_base, PAGE_SIZE);
        uint64_t seg_vend  = ROUND_UP(ph->p_vaddr + load_base + ph->p_memsz, PAGE_SIZE);
        uint64_t seg_pages = (seg_vend - seg_vaddr) >> PAGE_SHIFT;

        DBG_VAL("ELF", "seg vaddr",  seg_vaddr);
        DBG_VAL("ELF", "seg pages",  seg_pages);

         
        uint64_t phys = pmm_alloc_frames((size_t)seg_pages, 1);
        if (phys == (uint64_t)-1) {
            DBG_MSG("ELF", "pmm_alloc_frames OOM for segment");
            goto err_free_segs;
        }

         

        #define ELF_DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
        uint8_t *kva = (uint8_t *)(uintptr_t)(phys + ELF_DIRECT_MAP_OFFSET);

         
        memset(kva, 0, (size_t)(seg_pages << PAGE_SHIFT));

         
        if (ph->p_filesz > 0) {
             
            uint64_t page_offset = (ph->p_vaddr + load_base) - seg_vaddr;
            uint8_t *dst = kva + page_offset;

            if (vfs_seek(fd, (int64_t)ph->p_offset, 0) < 0) {
                DBG_MSG("ELF", "seek to segment data failed");
                pmm_free_frames(phys, (size_t)seg_pages);
                goto err_free_segs;
            }

             
            uint64_t remain = ph->p_filesz;
            uint64_t written = 0;
            while (remain > 0) {
                int64_t chunk = vfs_read(fd, dst + written, remain);
                if (chunk <= 0) {
                    DBG_MSG("ELF", "vfs_read segment data failed");
                    pmm_free_frames(phys, (size_t)seg_pages);
                    goto err_free_segs;
                }
                written += (uint64_t)chunk;
                remain  -= (uint64_t)chunk;
            }
        }

         
        bool seg_rw   = (ph->p_flags & PF_W) != 0;
        bool seg_exec = (ph->p_flags & PF_X) != 0;

        for (uint64_t p = 0; p < seg_pages; p++) {
            uint64_t va = seg_vaddr + (p << PAGE_SHIFT);
            uint64_t pa = phys      + (p << PAGE_SHIFT);
            int mr = vmm_map_user_in(out->pml4_phys, va, pa, seg_rw, seg_exec);
            if (mr != 0) {
                DBG_MSG("ELF", "vmm_map_user_in failed");
                 
                 
                pmm_free_frames(phys, (size_t)seg_pages);
                goto err_free_segs;
            }
        }

         
        out->segs[nseg].vaddr      = seg_vaddr;
        out->segs[nseg].num_pages  = seg_pages;
        out->segs[nseg].phys_base  = phys;
        out->segs[nseg].exec       = seg_exec;
        out->segs[nseg].write      = seg_rw;
        nseg++;
    }

    out->nseg = nseg;
    kfree(phdrs);
    vfs_close(fd);

    if (nseg == 0) {
        DBG_MSG("ELF", "no PT_LOAD segments found");
        return ELF_ERR_INVAL;
    }

     
    {
        uint64_t stack_pages = USER_STACK_PAGES;
        uint64_t stack_phys  = pmm_alloc_frames((size_t)stack_pages, 1);
        if (stack_phys == (uint64_t)-1) {
            DBG_MSG("ELF", "OOM for user stack");
            elf64_free(out);
            return ELF_ERR_NOMEM;
        }

         
        uint8_t *sk = (uint8_t *)(uintptr_t)(stack_phys + ELF_DIRECT_MAP_OFFSET);
        memset(sk, 0, (size_t)(stack_pages << PAGE_SHIFT));

        uint64_t stack_va_base = USER_STACK_TOP - (stack_pages << PAGE_SHIFT);

         
        for (uint64_t p = 0; p < stack_pages; p++) {
            uint64_t va = stack_va_base + (p << PAGE_SHIFT);
            uint64_t pa = stack_phys    + (p << PAGE_SHIFT);
            if (vmm_map_user_in(out->pml4_phys, va, pa, true, false) != 0) {
                DBG_MSG("ELF", "vmm_map_user_in stack failed");
                pmm_free_frames(stack_phys, (size_t)stack_pages);
                elf64_free(out);
                return ELF_ERR_NOMEM;
            }
        }

         


        out->stack_top   = (USER_STACK_TOP - 8ULL) & ~0xFULL;
        out->stack_pages = stack_pages;
        out->stack_phys  = stack_phys;

        DBG_VAL("ELF", "stack_top",  out->stack_top);
    }

    DBG_MSG("ELF", "elf64_load OK");
    return ELF_OK;

err_free_segs:
    kfree(phdrs);
    vfs_close(fd);
     
    out->nseg = nseg;
    elf64_free(out);
    return ELF_ERR_IO;
}

 


void elf64_free(elf64_image_t *img) {
    if (!img) return;

     
    for (uint32_t i = 0; i < img->nseg; i++) {
        if (img->segs[i].phys_base && img->segs[i].num_pages) {
            pmm_free_frames(img->segs[i].phys_base, (size_t)img->segs[i].num_pages);
            img->segs[i].phys_base = 0;
            img->segs[i].num_pages = 0;
        }
    }
    img->nseg = 0;

     
    if (img->stack_phys && img->stack_pages) {
        pmm_free_frames(img->stack_phys, (size_t)img->stack_pages);
        img->stack_phys  = 0;
        img->stack_pages = 0;
    }

     



    if (img->pml4_phys) {
        vmm_free_user_pml4(img->pml4_phys);
        img->pml4_phys = 0;
    }
}

 


typedef struct {
    elf64_image_t image;
} elf64_spawn_arg_t;

 













static void user_entry_trampoline(void *arg) {
    elf64_spawn_arg_t *sa = (elf64_spawn_arg_t *)arg;

    uint64_t user_rip = sa->image.entry;
    uint64_t user_rsp = sa->image.stack_top;

    DBG_VAL("ELF", "trampoline rip", user_rip);
    DBG_VAL("ELF", "trampoline rsp", user_rsp);

     
    kfree(sa);

     
    __asm__ volatile (
         
        "push $0x23\n\t"             
        "push %[ursp]\n\t"           
        "pushfq\n\t"                 
        "pop %%rax\n\t"
        "or $0x200, %%rax\n\t"       
        "push %%rax\n\t"             
        "push $0x2B\n\t"             
        "push %[urip]\n\t"           

         


         
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
        : [urip] "r"(user_rip), [ursp] "r"(user_rsp)
        : "rax", "memory"
    );

     
    __builtin_unreachable();
}

 


int elf64_spawn(const char *path, const char *name, int prio) {
    if (!path || !name) return ELF_ERR_INVAL;

    dbg_puts("[ELF] elf64_spawn: loading '"); dbg_puts(path);
    dbg_puts("' as '"); dbg_puts(name); dbg_puts("'\n");

     
    elf64_spawn_arg_t *sa = (elf64_spawn_arg_t *)kmalloc(sizeof(elf64_spawn_arg_t));
    if (!sa) {
        DBG_MSG("ELF", "elf64_spawn: OOM for spawn arg");
        return ELF_ERR_NOMEM;
    }
    memset(sa, 0, sizeof(elf64_spawn_arg_t));

     
    int r = elf64_load(path, &sa->image);
    if (r != ELF_OK) {
        DBG_RC("ELF", "elf64_spawn: elf64_load failed, rc", r);
        kfree(sa);
        return r;
    }

    DBG_VAL("ELF", "spawn entry", sa->image.entry);

     
    int tid = sched_create_kthread(user_entry_trampoline, sa, name, prio);
    if (tid < 0) {
        DBG_MSG("ELF", "elf64_spawn: sched_create_kthread failed");
        elf64_free(&sa->image);
        kfree(sa);
        return ELF_ERR_NOMEM;
    }

     








    task_t *t = sched_find((uint32_t)tid);
    if (t) {
        t->pml4_phys = sa->image.pml4_phys;
    } else {
         



        DBG_MSG("ELF", "elf64_spawn: sched_find(tid) did not find the newly created task");
    }

    dbg_puts("[ELF] spawned '"); dbg_puts(name); dbg_puts("' tid=");
    dbg_dec64((uint64_t)(uint32_t)tid); dbg_puts("\n");
    return tid;
}

#ifdef __cplusplus
}
#endif
