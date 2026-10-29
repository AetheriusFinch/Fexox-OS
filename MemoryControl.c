/* Fexox OS Project, EULA GPLv2 */
 





#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "debug_out.h"

 
#ifndef FEXOX_OS_KM_VERBOSE
#define FEXOX_OS_KM_VERBOSE 0
#endif

 
extern void dbg_puts(const char *s);
extern void dbg_hex64(uint64_t v);

 


typedef uint64_t           phys_addr_t;
typedef uint64_t           virt_addr_t;
typedef uint64_t           pte_t;  

#define true  1
#define false 0

 
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
typedef _Bool bool;
#endif

#define NULL ((void*)0)
#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1ULL))
#define ROUND_UP(n, align) (((n) + (align) - 1) & ~((align) - 1ULL))

 
#define KERNEL_VIRT_BASE      0xFFFFFFFF80000000ULL
#define DIRECT_MAP_OFFSET     0xFFFF880000000000ULL
#define TEMP_MAP_VADDR        0xFFFFFFFFFFE00000ULL
#define USER_VIRT_LIMIT       0x00007FFFFFFFFFFFULL
#define HEAP_START_VADDR      0xFFFF880000000000ULL

 
typedef struct {
    uint32_t type; uint32_t pad;
    phys_addr_t physical_start; virt_addr_t virtual_start;
    uint64_t num_pages; uint32_t attribute;
} efi_memory_desc_t;

#define EFI_USABLE      7
#define EFI_ACPI_RECLAIM 11
#define EFI_ACPI_NVS    12
#define EFI_MMIO        13

 
static inline void *kmemset(void *dst, int c, size_t n) {
    uint8_t *p = dst; while (n--) *p++ = (uint8_t)c; return dst;
}
static inline void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src; while (n--) *d++ = *s++; return dst;
}
static inline uint32_t ctz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctzll(v);
#else
    uint32_t n = 0;
    if ((v & 0xFFFFFFFF) == 0) { v >>= 32; n += 32; }
    if ((v & 0xFFFF) == 0) { v >>= 16; n += 16; }
    if ((v & 0xFF) == 0) { v >>= 8; n += 8; }
    if ((v & 0xF) == 0) { v >>= 4; n += 4; }
    if ((v & 0x3) == 0) { v >>= 2; n += 2; }
    return n + (uint32_t)(v & 1);
#endif
}

 









typedef volatile uint32_t spinlock_t;
static inline void spinlock_init(spinlock_t *l) { *l = 0; }
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
    *l = (uint32_t)flags;
}
static inline void spinlock_release(spinlock_t *l) {
    if (*l & 0x200) {
        __asm__ volatile ("sti" ::: "memory");
    }
}

 


typedef struct {
    uint64_t *words; size_t total_frames; size_t free_frames; size_t num_words;
    spinlock_t lock; phys_addr_t bitmap_phys; size_t bitmap_pages;
} pmm_t;

static pmm_t g_pmm;

 




















static bool g_pmm_low4g_only = false;

#define WORD_IDX(idx) ((idx) >> 6)
#define BIT_IDX(idx)  ((idx) & 63)

static inline bool pmm_test(size_t idx) { return (g_pmm.words[WORD_IDX(idx)] >> BIT_IDX(idx)) & 1; }
static inline void pmm_set(size_t idx)  { g_pmm.words[WORD_IDX(idx)] |= (1ULL << BIT_IDX(idx)); }
static inline void pmm_clear(size_t idx){ g_pmm.words[WORD_IDX(idx)] &= ~(1ULL << BIT_IDX(idx)); }

static size_t pmm_find_next_zero(size_t start, size_t limit) {
    size_t w_start = WORD_IDX(start), w_limit = WORD_IDX(limit - 1) + 1;
    uint64_t skip_mask = ~(~0ULL << (start & 63));
    for (size_t w = w_start; w < w_limit; w++) {
        uint64_t val = g_pmm.words[w] | skip_mask;
        if (val != ~0ULL) {
            size_t bit = ctz64(~val);
            size_t idx = (w << 6) | bit;
            if (idx < limit) return idx;
        }
        skip_mask = 0;
    }
    return (size_t)-1;
}

static size_t pmm_find_contiguous_zero(size_t count, size_t align) {
    size_t idx = 0, limit = g_pmm.total_frames;
    if (g_pmm_low4g_only) {
        size_t low4g_frames = (4ULL << 30) >> PAGE_SHIFT;    
        if (low4g_frames < limit) limit = low4g_frames;
    }
    while (idx + count <= limit) {
        size_t cand = pmm_find_next_zero(idx, limit);
        if (cand == (size_t)-1) break;
        size_t aligned = (cand + align - 1) & ~(align - 1);
        if (aligned + count > limit) { idx = aligned; continue; }
        bool ok = true;
        for (size_t i = 0; i < count; i++) {
            if (pmm_test(aligned + i)) { ok = false; break; }
        }
        if (ok) {
            for (size_t i = 0; i < count; i++) pmm_set(aligned + i);
            g_pmm.free_frames -= count; return aligned;
        }
        idx = aligned + count;   
    }
    return (size_t)-1;
}

static phys_addr_t efi_find_bitmap_region(const efi_memory_desc_t *map, size_t map_size, size_t desc_size, size_t needed_pages) {
     










    const phys_addr_t low4g_limit = (4ULL << 30);
    for (size_t i = 0; i < map_size; i += desc_size) {
        const efi_memory_desc_t *d = (const efi_memory_desc_t *)((uintptr_t)map + i);
        if (d->type != EFI_USABLE || d->num_pages < needed_pages) continue;
        phys_addr_t start = ROUND_UP(d->physical_start, PAGE_SIZE);
        phys_addr_t end   = start + ((phys_addr_t)needed_pages << PAGE_SHIFT);
        if (end > low4g_limit) continue;    
        return start;
    }
    return (phys_addr_t)-1;
}

int pmm_init_from_efi(const efi_memory_desc_t *map, size_t map_size, size_t desc_size) {
    phys_addr_t max_phys = 0;
    for (size_t i = 0; i < map_size; i += desc_size) {
        const efi_memory_desc_t *d = (const efi_memory_desc_t *)((uintptr_t)map + i);
        phys_addr_t end = d->physical_start + (d->num_pages << PAGE_SHIFT);
        if (end > max_phys) max_phys = end;
    }
    size_t total_frames = (max_phys + PAGE_SIZE - 1) >> PAGE_SHIFT;
    size_t bm_bytes = (total_frames + 63) >> 3;
    size_t bm_pages = (bm_bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
    phys_addr_t bm_pa = efi_find_bitmap_region(map, map_size, desc_size, bm_pages);
    if (bm_pa == (phys_addr_t)-1) return -1;

    g_pmm.bitmap_phys = bm_pa; g_pmm.bitmap_pages = bm_pages;
    g_pmm.total_frames = total_frames;
    g_pmm.free_frames = 0;   
    g_pmm.num_words = (total_frames + 63) >> 6;
    g_pmm.words = (uint64_t *)(uintptr_t)bm_pa;

    kmemset(g_pmm.words, 0xFF, g_pmm.num_words * sizeof(uint64_t));
     









    spinlock_init(&g_pmm.lock);
    for (size_t i = 0; i < map_size; i += desc_size) {
        const efi_memory_desc_t *d = (const efi_memory_desc_t *)((uintptr_t)map + i);
        if (d->type == EFI_USABLE) {
            size_t start_idx = d->physical_start >> PAGE_SHIFT;
            size_t count = d->num_pages;
            if (start_idx + count <= total_frames) {
                phys_addr_t bm_end = bm_pa + (bm_pages << PAGE_SHIFT);
                for (size_t p = 0; p < count; p++) {
                    phys_addr_t frame = d->physical_start + (p << PAGE_SHIFT);
                    if (frame >= bm_end) {
                        size_t idx = frame >> PAGE_SHIFT;
                        if (pmm_test(idx)) { pmm_clear(idx); g_pmm.free_frames++; }
                    }
                }
            }
        }
    }
    return 0;
}

phys_addr_t pmm_alloc_frames(size_t count, size_t align) {
    if (count == 0 || (align & (align - 1)) != 0) return (phys_addr_t)-1;
    spinlock_acquire(&g_pmm.lock);
    size_t idx = pmm_find_contiguous_zero(count, align ? align : 1);
    spinlock_release(&g_pmm.lock);
    return (idx == (size_t)-1) ? (phys_addr_t)-1 : (phys_addr_t)idx << PAGE_SHIFT;
}
phys_addr_t pmm_alloc_frame(void) { return pmm_alloc_frames(1, 1); }

 







phys_addr_t pmm_alloc_frames_above(size_t count, size_t align, phys_addr_t min_phys) {
    if (count == 0 || (align & (align - 1)) != 0) return (phys_addr_t)-1;
     
    size_t min_frame = (size_t)((min_phys + PAGE_SIZE - 1) >> PAGE_SHIFT);
     
    size_t a = align ? align : 1;

    spinlock_acquire(&g_pmm.lock);
    size_t limit = g_pmm.total_frames;
    size_t idx = min_frame;
    phys_addr_t result = (phys_addr_t)-1;

    while (idx + count <= limit) {
        size_t cand = pmm_find_next_zero(idx, limit);
        if (cand == (size_t)-1) break;
         
        size_t aligned = (cand + a - 1) & ~(a - 1);
         
        if (aligned < min_frame) aligned = (min_frame + a - 1) & ~(a - 1);
        if (aligned + count > limit) break;
        bool ok = true;
        for (size_t i = 0; i < count; i++) {
            if (pmm_test(aligned + i)) { ok = false; break; }
        }
        if (ok) {
            for (size_t i = 0; i < count; i++) pmm_set(aligned + i);
            g_pmm.free_frames -= count;
            result = (phys_addr_t)aligned << PAGE_SHIFT;
            break;
        }
        idx = aligned + count;
    }
    spinlock_release(&g_pmm.lock);
    return result;
}

 












phys_addr_t pmm_alloc_frames_range(size_t count, size_t align,
                                   phys_addr_t min_phys, phys_addr_t max_phys) {
    if (count == 0 || (align & (align - 1)) != 0) return (phys_addr_t)-1;
    if (max_phys <= min_phys) return (phys_addr_t)-1;

    size_t min_frame = (size_t)((min_phys + PAGE_SIZE - 1) >> PAGE_SHIFT);
    size_t max_frame = (size_t)(max_phys >> PAGE_SHIFT);    
    size_t a = align ? align : 1;

    spinlock_acquire(&g_pmm.lock);
    size_t limit = g_pmm.total_frames < max_frame ? g_pmm.total_frames : max_frame;
    size_t idx = min_frame;
    phys_addr_t result = (phys_addr_t)-1;

    while (idx + count <= limit) {
        size_t cand = pmm_find_next_zero(idx, limit);
        if (cand == (size_t)-1) break;
        size_t aligned = (cand + a - 1) & ~(a - 1);
        if (aligned < min_frame) aligned = (min_frame + a - 1) & ~(a - 1);
        if (aligned + count > limit) break;
        bool ok = true;
        for (size_t i = 0; i < count; i++) {
            if (pmm_test(aligned + i)) { ok = false; break; }
        }
        if (ok) {
            for (size_t i = 0; i < count; i++) pmm_set(aligned + i);
            g_pmm.free_frames -= count;
            result = (phys_addr_t)aligned << PAGE_SHIFT;
            break;
        }
        idx = aligned + count;
    }
    spinlock_release(&g_pmm.lock);
    return result;
}

void pmm_free_frames(phys_addr_t pa, size_t count) {
    if (pa == 0 || pa == (phys_addr_t)-1 || count == 0) return;
    size_t idx = pa >> PAGE_SHIFT;
    spinlock_acquire(&g_pmm.lock);
    for (size_t i = 0; i < count; i++) {
        if (pmm_test(idx + i)) { pmm_clear(idx + i); g_pmm.free_frames++; }
    }
    spinlock_release(&g_pmm.lock);
}
void pmm_free_frame(phys_addr_t pa) { pmm_free_frames(pa, 1); }

 

void pmm_get_stats(uint64_t *total_frames, uint64_t *free_frames) {
    if (total_frames) *total_frames = (uint64_t)g_pmm.total_frames;
    if (free_frames)  *free_frames  = (uint64_t)g_pmm.free_frames;
}

 


#define PTE_P     (1ULL << 0)
#define PTE_W     (1ULL << 1)
#define PTE_U     (1ULL << 2)
#define PTE_PWT   (1ULL << 3)
#define PTE_PCD   (1ULL << 4)
#define PTE_A     (1ULL << 5)
#define PTE_D     (1ULL << 6)
#define PTE_PAT   (1ULL << 7)
#define PTE_G     (1ULL << 8)
#define PTE_NX    (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v) (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

typedef struct {
    pte_t *kernel_pml4;  
    spinlock_t lock;
    bool paging_active;
} vmm_t;

static vmm_t g_vmm;

 




static phys_addr_t g_kernel_pml4_phys = (phys_addr_t)-1;

 







extern uint64_t sched_current_pml4_phys(void);

static inline pte_t pte_make(phys_addr_t pa, uint64_t flags) { return (pa & PTE_ADDR_MASK) | flags | PTE_A; }
static void cpu_invlpg(virt_addr_t addr) { __asm__ volatile ("invlpg (%0)" :: "r"(addr) : "memory"); }
static void vmm_load_cr3(phys_addr_t pml4_pa) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_pa) : "memory");
    g_vmm.paging_active = true;
}
static inline void *phys_to_kvirt(phys_addr_t pa) { return (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET); }

 
static inline void *phys_access(phys_addr_t pa) {
    if (!g_vmm.paging_active)
        return (void *)(uintptr_t)pa;
    return phys_to_kvirt(pa);
}

 
int vmm_map(virt_addr_t va, phys_addr_t pa, uint64_t flags);
int vmm_unmap(virt_addr_t va);

 










static inline bool pa_in_ram(phys_addr_t pa) {
    return (pa >> PAGE_SHIFT) < g_pmm.total_frames;
}

static pte_t *vmm_walk_table(pte_t *pml4, virt_addr_t va, bool create) {
    size_t idx_p4 = PML4_IDX(va);
    size_t idx_p3 = PDPT_IDX(va);
    size_t idx_p2 = PD_IDX(va);
    size_t idx_p1 = PT_IDX(va);

     














    if (!(pml4[idx_p4] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PML4E) returned garbage, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        pml4[idx_p4] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
     









    phys_addr_t p3_pa = pml4[idx_p4] & PTE_ADDR_MASK;
    if (!pa_in_ram(p3_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PML4E points outside RAM, pa", (uint64_t)p3_pa);
        return NULL;
    }
    pte_t *p3t = (pte_t *)phys_access(p3_pa);
    if (!(p3t[idx_p3] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PDPTE) returned garbage, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        p3t[idx_p3] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
    phys_addr_t p2_pa = p3t[idx_p3] & PTE_ADDR_MASK;
    if (!pa_in_ram(p2_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PDPTE points outside RAM, pa", (uint64_t)p2_pa);
        return NULL;
    }
    pte_t *p2t = (pte_t *)phys_access(p2_pa);
    if (!(p2t[idx_p2] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PDE) returned garbage, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        p2t[idx_p2] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
    phys_addr_t p1_pa = p2t[idx_p2] & PTE_ADDR_MASK;
    if (!pa_in_ram(p1_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PDE points outside RAM, pa", (uint64_t)p1_pa);
        return NULL;
    }
    pte_t *p1t = (pte_t *)phys_access(p1_pa);
    return &p1t[idx_p1];
}

 
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void *vmm_temp_map(phys_addr_t pa) {
    size_t idx_p4 = PML4_IDX(TEMP_MAP_VADDR);
    size_t idx_p3 = PDPT_IDX(TEMP_MAP_VADDR);
    size_t idx_p2 = PD_IDX(TEMP_MAP_VADDR);
    size_t idx_p1 = PT_IDX(TEMP_MAP_VADDR);
    pte_t *pml4 = g_vmm.kernel_pml4;
    if (!(pml4[idx_p4] & PTE_P)) return NULL;
    pte_t *pdpt = (pte_t *)phys_access(pml4[idx_p4] & PTE_ADDR_MASK);
    if (!(pdpt[idx_p3] & PTE_P)) return NULL;
    pte_t *pd = (pte_t *)phys_access(pdpt[idx_p3] & PTE_ADDR_MASK);
    if (!(pd[idx_p2] & PTE_P)) return NULL;
    pte_t *pt = (pte_t *)phys_access(pd[idx_p2] & PTE_ADDR_MASK);

    pt[idx_p1] = pte_make(pa, PTE_P | PTE_W);
    cpu_invlpg(TEMP_MAP_VADDR);
    return (void *)TEMP_MAP_VADDR;
}

static void vmm_temp_unmap(void) {
    size_t idx_p4 = PML4_IDX(TEMP_MAP_VADDR);
    size_t idx_p3 = PDPT_IDX(TEMP_MAP_VADDR);
    size_t idx_p2 = PD_IDX(TEMP_MAP_VADDR);
    size_t idx_p1 = PT_IDX(TEMP_MAP_VADDR);
    pte_t *pml4 = g_vmm.kernel_pml4;
    pte_t *pdpt = (pte_t *)phys_access(pml4[idx_p4] & PTE_ADDR_MASK);
    pte_t *pd = (pte_t *)phys_access(pdpt[idx_p3] & PTE_ADDR_MASK);
    pte_t *pt = (pte_t *)phys_access(pd[idx_p2] & PTE_ADDR_MASK);
    pt[idx_p1] = 0;
    cpu_invlpg(TEMP_MAP_VADDR);
}

#pragma GCC diagnostic pop

#define KERNEL_PHYS_BASE 0x200000ULL
 










#define KERNEL_MAP_BYTES (64ULL * 1024 * 1024)

 
static int vmm_map_2mb(virt_addr_t va, phys_addr_t pa, uint64_t flags) {
     
    size_t idx_p4 = PML4_IDX(va);
    size_t idx_p3 = PDPT_IDX(va);
    size_t idx_p2 = PD_IDX(va);

    pte_t *pml4 = g_vmm.kernel_pml4;

    if (!(pml4[idx_p4] & PTE_P)) {
        phys_addr_t child = pmm_alloc_frame();
        if (child == (phys_addr_t)-1) return -1;
        kmemset(phys_access(child), 0, PAGE_SIZE);
        pml4[idx_p4] = pte_make(child, PTE_P | PTE_W);
    }
    pte_t *p3t = (pte_t *)phys_access(pml4[idx_p4] & PTE_ADDR_MASK);

    if (!(p3t[idx_p3] & PTE_P)) {
        phys_addr_t child = pmm_alloc_frame();
        if (child == (phys_addr_t)-1) return -1;
        kmemset(phys_access(child), 0, PAGE_SIZE);
        p3t[idx_p3] = pte_make(child, PTE_P | PTE_W);
    }
    pte_t *p2t = (pte_t *)phys_access(p3t[idx_p3] & PTE_ADDR_MASK);

     
    p2t[idx_p2] = (pa & 0x000FFFFFFFE00000ULL) | flags | (1ULL << 7)  ;
    return 0;
}

#define HUGE_PAGE_SIZE (2ULL << 20)   
#define HUGE_PAGE_SHIFT 21

 










static inline void enable_nx(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
    lo |= (1u << 11);    
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080U));
}

int vmm_init(void) {
    spinlock_init(&g_vmm.lock);
    g_vmm.paging_active = false;
    enable_nx();

     




    g_pmm_low4g_only = true;

    phys_addr_t pml4_pa = pmm_alloc_frame();
    if (pml4_pa == (phys_addr_t)-1) { g_pmm_low4g_only = false; return -1; }
    g_vmm.kernel_pml4 = (pte_t *)phys_access(pml4_pa);
    kmemset(g_vmm.kernel_pml4, 0, PAGE_SIZE);
    g_kernel_pml4_phys = pml4_pa;

     
    uint64_t dm_flags = PTE_P | PTE_W | PTE_G | PTE_NX;
    for (size_t i = 0; i < (4ULL << 30) >> HUGE_PAGE_SHIFT; i++) {
        phys_addr_t pa = (phys_addr_t)i << HUGE_PAGE_SHIFT;
        int r = vmm_map_2mb(DIRECT_MAP_OFFSET + pa, pa, dm_flags);
        if (r) { g_pmm_low4g_only = false; return r; }
    }

     
    uint64_t kflags = PTE_P | PTE_W | PTE_G;
    for (size_t off = 0; off < KERNEL_MAP_BYTES; off += HUGE_PAGE_SIZE) {
        int r = vmm_map_2mb(KERNEL_VIRT_BASE + off, KERNEL_PHYS_BASE + off, kflags);
        if (r) { g_pmm_low4g_only = false; return r; }
    }

    g_pmm_low4g_only = false;
    return 0;
}

int vmm_map(virt_addr_t va, phys_addr_t pa, uint64_t flags) {
    if ((va & ~PAGE_MASK) || (pa & ~PAGE_MASK) || !(flags & PTE_P)) return -1;
    spinlock_acquire(&g_vmm.lock);
    pte_t *entry = vmm_walk_table(g_vmm.kernel_pml4, va, true);
    if (!entry) { spinlock_release(&g_vmm.lock); return -2; }
    *entry = pte_make(pa, flags);
    if (g_vmm.paging_active) cpu_invlpg(va);
    spinlock_release(&g_vmm.lock);
    return 0;
}

int vmm_unmap(virt_addr_t va) {
    if (va & ~PAGE_MASK) return -1;
    spinlock_acquire(&g_vmm.lock);
    pte_t *entry = vmm_walk_table(g_vmm.kernel_pml4, va, false);
    if (!entry || !(*entry & PTE_P)) { spinlock_release(&g_vmm.lock); return -1; }
    *entry = 0;
    if (g_vmm.paging_active) cpu_invlpg(va);
    spinlock_release(&g_vmm.lock);
    return 0;
}

 




















phys_addr_t vmm_virt_to_phys(virt_addr_t va) {
    spinlock_acquire(&g_vmm.lock);
    pte_t *entry = vmm_walk_table(g_vmm.kernel_pml4, va & PAGE_MASK, false);
    if (!entry || !(*entry & PTE_P)) { spinlock_release(&g_vmm.lock); return (phys_addr_t)-1; }
    phys_addr_t pa = (*entry & PTE_ADDR_MASK) | (va & ~PAGE_MASK);
    spinlock_release(&g_vmm.lock);
    return pa;
}

void vmm_activate(void) {
    phys_addr_t cr3_pa = (phys_addr_t)(uintptr_t)g_vmm.kernel_pml4;
    vmm_load_cr3(cr3_pa);
    g_vmm.kernel_pml4 = (pte_t *)phys_to_kvirt(cr3_pa);
    g_pmm.words = (uint64_t *)phys_to_kvirt(g_pmm.bitmap_phys);
}

 


#define KM_MIN_SIZE 16
#define KM_MAX_SIZE (4096 * 16)
#define KM_SIZE_CLASSES 12
#define KM_CLASS_SIZE(i) (KM_MIN_SIZE << (i))

 









typedef struct km_block {
    size_t size;
    bool free;
    struct km_block *next;        
    struct km_block *prev;        
    struct km_block *phys_prev;   
} km_block_t;
typedef struct {
    km_block_t *free_lists[KM_SIZE_CLASSES];
    spinlock_t lock;
    virt_addr_t heap_end;
    size_t heap_used;
    km_block_t *last_block;       
} km_ctx_t;

static km_ctx_t g_km;

static void km_list_unlink(km_block_t *b);

static inline size_t km_size_class(size_t sz) {
    if (sz <= KM_MIN_SIZE) return 0;
    size_t a = ROUND_UP(sz, KM_MIN_SIZE);
    size_t c = 0;
    while (KM_CLASS_SIZE(c) < a && c < (size_t)(KM_SIZE_CLASSES - 1)) c++;
    return c;
}

static km_block_t *km_find_best(size_t req) {
    size_t c = km_size_class(req);
    
#if FEXOX_OS_KM_VERBOSE
    dbg_puts("[KM] km_find_best: req=");
    dbg_hex64(req);
    dbg_puts(" class=");
    dbg_hex64(c);
    dbg_puts("\n");
#endif
    
    km_block_t *best = NULL;
    size_t best_diff = (size_t)-1;
    for (size_t i = c; i < KM_SIZE_CLASSES; i++) {
        km_block_t *b = g_km.free_lists[i];
        while (b) {
             
            if ((uint64_t)(uintptr_t)b < (uint64_t)(uintptr_t)DIRECT_MAP_OFFSET) {
                dbg_puts("[KM] km_find_best: INVALID ptr=");
                dbg_hex64((uint64_t)(uintptr_t)b);
                dbg_puts(" in list[");
                dbg_hex64(i);
                dbg_puts("]\n");
                break;
            }
            
            if (b->free && b->size >= req) {
                size_t diff = b->size - req;
                if (diff < best_diff) { best_diff = diff; best = b; }
                if (diff == 0) goto found;
            }
            b = b->next;
        }
    }
    if (best) {
        goto found;
    }
    return NULL;

found:
    km_list_unlink(best);

    if (best->size >= req + sizeof(km_block_t) + KM_MIN_SIZE) {
        size_t orig_size = best->size;
        km_block_t *rem = (km_block_t *)((uint8_t *)best + sizeof(km_block_t) + req);
        rem->size = orig_size - sizeof(km_block_t) - req;
        rem->free = true;
        rem->next = NULL;
        rem->prev = NULL;
        rem->phys_prev = best;

         

        km_block_t *old_next = (km_block_t *)((uint8_t *)best + sizeof(km_block_t) + orig_size);
        if ((uint8_t *)old_next < (uint8_t *)g_km.heap_end) {
            old_next->phys_prev = rem;
        } else {
             
            g_km.last_block = rem;
        }

        size_t r_class = km_size_class(rem->size);
        rem->next = g_km.free_lists[r_class];
        if (g_km.free_lists[r_class]) g_km.free_lists[r_class]->prev = rem;
        g_km.free_lists[r_class] = rem;

        best->size = req;
    }
    best->free = false;
     

    best->next = NULL;
    best->prev = NULL;
    return best;
}

static void km_insert_free(km_block_t *b) {
    b->free = true;
    size_t c = km_size_class(b->size);
    b->next = g_km.free_lists[c];
    b->prev = NULL;
    if (g_km.free_lists[c]) g_km.free_lists[c]->prev = b;
    g_km.free_lists[c] = b;
}

 





static void km_list_unlink(km_block_t *b) {
    size_t c = km_size_class(b->size);
    if (b->prev) {
        b->prev->next = b->next;
    } else if (g_km.free_lists[c] == b) {
        g_km.free_lists[c] = b->next;
    }
    if (b->next) {
        b->next->prev = b->prev;
    }
    b->next = NULL;
    b->prev = NULL;
}

static int km_expand(size_t min_bytes) {
    size_t pages = ROUND_UP(min_bytes + sizeof(km_block_t), PAGE_SIZE) >> PAGE_SHIFT;
    phys_addr_t pa = pmm_alloc_frames(pages, 1);
    if (pa == (phys_addr_t)-1) return -1;
    
    virt_addr_t va = g_km.heap_end;
    size_t mapped = 0;
    uint64_t flags = PTE_P | PTE_W | PTE_NX;
    
    for (size_t i = 0; i < pages; i++) {
        if (vmm_map(va + (i << PAGE_SHIFT), pa + (i << PAGE_SHIFT), flags) != 0) {
            for (size_t j = 0; j < mapped; j++) vmm_unmap(va + (j << PAGE_SHIFT));
            pmm_free_frames(pa, pages);
            return -2;
        }
        mapped++;
    }
    
     




    kmemset((void *)(uintptr_t)va, 0, pages << PAGE_SHIFT);

    km_block_t *blk = (km_block_t *)(uintptr_t)va;
    blk->size = (pages << PAGE_SHIFT) - sizeof(km_block_t);
    blk->free = true;
    blk->next = NULL;
    blk->prev = NULL;
     


    blk->phys_prev = g_km.last_block;
    g_km.last_block = blk;
    km_insert_free(blk);
    g_km.heap_end = va + (pages << PAGE_SHIFT);
    return 0;
}

 
static uint32_t g_km_fail_count = 0;
static uint64_t g_km_fail_last_size = 0;
void kmalloc_get_diag(uint32_t *fails, uint64_t *last_fail_size, uint64_t *heap_used) {
    if (fails)          *fails          = g_km_fail_count;
    if (last_fail_size) *last_fail_size = g_km_fail_last_size;
    if (heap_used)      *heap_used      = (uint64_t)g_km.heap_used;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = ROUND_UP(size, 16);
    
     
    if (!g_km.heap_end) {
        dbg_puts("[KM] kmalloc: heap_end=0\n");
        return NULL;
    }
    
#if FEXOX_OS_KM_VERBOSE
    dbg_puts("[KM] kmalloc: size=");
    dbg_hex64(size);
    dbg_puts(" heap_end=");
    dbg_hex64((uint64_t)(uintptr_t)g_km.heap_end);
    dbg_puts(" &g_km=");
    dbg_hex64((uint64_t)(uintptr_t)&g_km);
    dbg_puts("\n");
    
#endif
    spinlock_acquire(&g_km.lock);
    
    
    km_block_t *b = km_find_best(size);
    
    
    if (!b) {
        if (km_expand(size) != 0) {
            dbg_puts("[KM] kmalloc: expand FAILED size="); dbg_dec64(size); dbg_puts("\n");
            g_km_fail_count++; g_km_fail_last_size = size;
            spinlock_release(&g_km.lock);
            return NULL;
        }
        b = km_find_best(size);
    }
    spinlock_release(&g_km.lock);
    
    if (!b) {
        dbg_puts("[KM] kmalloc: still NULL\n");
        g_km_fail_count++; g_km_fail_last_size = size;
        return NULL;
    }
    
    g_km.heap_used += b->size + sizeof(km_block_t);
    void *result = (void *)((uint8_t *)b + sizeof(km_block_t));
#if FEXOX_OS_KM_VERBOSE
    dbg_puts("[KM] kmalloc: return=");
    dbg_hex64((uint64_t)(uintptr_t)result);
    dbg_puts("\n");
#else
    if (size >= 65536u) {    
        dbg_puts("[KM] kmalloc big: size="); dbg_dec64(size); dbg_puts("\n");
    }
#endif
    return result;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    km_block_t *b = (km_block_t *)((uint8_t *)ptr - sizeof(km_block_t));

     







#if FEXOX_OS_KM_VERBOSE
    DBG_VAL("KM", "kfree: ptr", (uint64_t)(uintptr_t)ptr);
    DBG_VAL("KM", "kfree: block hdr addr", (uint64_t)(uintptr_t)b);
    DBG_VAL("KM", "kfree: b->size", (uint64_t)b->size);
    DBG_VAL("KM", "kfree: b->free", (uint64_t)b->free);
    DBG_VAL("KM", "kfree: b->next", (uint64_t)(uintptr_t)b->next);
    DBG_VAL("KM", "kfree: b->prev", (uint64_t)(uintptr_t)b->prev);
    DBG_VAL("KM", "kfree: b->phys_prev", (uint64_t)(uintptr_t)b->phys_prev);
    DBG_VAL("KM", "kfree: g_km.heap_end", (uint64_t)g_km.heap_end);
    DBG_VAL("KM", "kfree: g_km.last_block", (uint64_t)(uintptr_t)g_km.last_block);

#endif
    if (b->free) { DBG_MSG("KM", "kfree: double free ignored"); return; }  

    spinlock_acquire(&g_km.lock);
    b->free = true;
    g_km.heap_used -= b->size + sizeof(km_block_t);

     
    km_block_t *next = (km_block_t *)((uint8_t *)b + sizeof(km_block_t) + b->size);
    if ((uint8_t *)next < (uint8_t *)g_km.heap_end && next->free) {
            km_list_unlink(next);            
        b->size += sizeof(km_block_t) + next->size;

        km_block_t *after_next = (km_block_t *)((uint8_t *)next + sizeof(km_block_t) + next->size);
        if ((uint8_t *)after_next < (uint8_t *)g_km.heap_end) {
            after_next->phys_prev = b;    
        } else {
            g_km.last_block = b;          
        }
        }

     


    km_block_t *prevb = b->phys_prev;
    if (prevb && prevb->free) {
            km_list_unlink(prevb);
        prevb->size += sizeof(km_block_t) + b->size;

        km_block_t *after_b = (km_block_t *)((uint8_t *)b + sizeof(km_block_t) + b->size);
        if ((uint8_t *)after_b < (uint8_t *)g_km.heap_end) {
            after_b->phys_prev = prevb;
        } else {
            g_km.last_block = prevb;
        }
        b = prevb;  
        }

    km_insert_free(b);
    spinlock_release(&g_km.lock);
}

int kmalloc_init(void) {
    kmemset(&g_km, 0, sizeof(g_km));
    spinlock_init(&g_km.lock);
    g_km.heap_end = DIRECT_MAP_OFFSET + (4ULL << 30);
    g_km.heap_used = 0;
    return 0;
}

 


 









 




static void boot_delay_spin(void) {
    for (volatile uint64_t i = 0; i < 150000000ULL; i++) { }
}

static void mc_beacon(uint64_t fb_phys, uint32_t fb_w, uint32_t fb_h, uint32_t fb_pitch,
                      uint8_t fb_mode, uint32_t band_idx, uint32_t color, bool use_direct_map) {
    if (fb_mode != 2   || !fb_phys || !fb_w || !fb_h || !fb_pitch) return;

    uint64_t ptr_val = use_direct_map ? (DIRECT_MAP_OFFSET + fb_phys) : fb_phys;
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)ptr_val;

    uint32_t band_h    = fb_h / 8u;
    uint32_t row_start  = (fb_h / 4u) + band_idx * band_h;    
    uint32_t row_end    = row_start + band_h;
    if (row_end > fb_h) row_end = fb_h;

    for (uint32_t row = row_start; row < row_end; row++) {
        volatile uint32_t *px = (volatile uint32_t *)(fb + (uint64_t)row * fb_pitch);
        for (uint32_t col = 0; col < fb_w; col++) px[col] = color;
    }

    boot_delay_spin();    
}

 


int mem_control_init(const void *efi_map, uint64_t map_size, uint64_t desc_size,
                     uint64_t fb_phys, uint32_t fb_w, uint32_t fb_h,
                     uint32_t fb_pitch, uint8_t fb_mode) {
    int r;
    DBG_MSG("KR", "12a pmm_init");
    r = pmm_init_from_efi((const efi_memory_desc_t *)efi_map, (size_t)map_size, (size_t)desc_size);
    if (r != 0) return -1;
    mc_beacon(fb_phys, fb_w, fb_h, fb_pitch, fb_mode, 0, 0x00FFFF00u, false);  

    DBG_MSG("KR", "12b vmm_init");
    r = vmm_init();
    if (r != 0) return -2;
    mc_beacon(fb_phys, fb_w, fb_h, fb_pitch, fb_mode, 1, 0x00FFA500u, false);  

    DBG_MSG("KR", "12c kmalloc_init");
    r = kmalloc_init();
    if (r != 0) return -3;
    mc_beacon(fb_phys, fb_w, fb_h, fb_pitch, fb_mode, 2, 0x0000FFFFu, false);  

    DBG_MSG("KR", "12d vmm_activate");
    vmm_activate();
     
    mc_beacon(fb_phys, fb_w, fb_h, fb_pitch, fb_mode, 3, 0x00FF0000u, true);   

    DBG_MSG("KR", "12e mem done");
    return 0;
}

 




























 


phys_addr_t vmm_kernel_pml4_phys(void) { return g_kernel_pml4_phys; }

 





phys_addr_t vmm_create_user_pml4(void) {
    phys_addr_t pa = pmm_alloc_frame();
    if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) return (phys_addr_t)-1;
    pte_t *pml4 = (pte_t *)phys_access(pa);
    kmemset(pml4, 0, PAGE_SIZE);
    for (size_t i = 256; i < 512; i++) pml4[i] = g_vmm.kernel_pml4[i];
    return pa;
}

 










void vmm_free_user_pml4(phys_addr_t pml4_phys) {
    if (pml4_phys && pml4_phys != (phys_addr_t)-1) pmm_free_frames(pml4_phys, 1);
}

 



int vmm_map_user_in(phys_addr_t pml4_phys, virt_addr_t va, phys_addr_t pa, bool rw, bool exec) {
    if (va > USER_VIRT_LIMIT) return -1;
    if ((va & ~PAGE_MASK) || (pa & ~PAGE_MASK)) return -1;
    if (pml4_phys == (phys_addr_t)-1 || !pa_in_ram(pml4_phys)) return -1;

    uint64_t flags = PTE_P | PTE_U;
    if (rw) flags |= PTE_W;
    if (!exec) flags |= PTE_NX;

    spinlock_acquire(&g_vmm.lock);
    pte_t *pml4 = (pte_t *)phys_access(pml4_phys);
    pte_t *entry = vmm_walk_table(pml4, va, true);
    if (!entry) { spinlock_release(&g_vmm.lock); return -2; }
    *entry = pte_make(pa, flags);
     


    if (g_vmm.paging_active) cpu_invlpg(va);
    spinlock_release(&g_vmm.lock);
    return 0;
}

 







int vmm_map_user(virt_addr_t va, phys_addr_t pa, bool rw, bool exec) {
    uint64_t cur = sched_current_pml4_phys();
    phys_addr_t target = cur ? (phys_addr_t)cur : g_kernel_pml4_phys;
    return vmm_map_user_in(target, va, pa, rw, exec);
}

#ifdef __cplusplus
}
#endif