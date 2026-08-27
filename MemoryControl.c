/*
MemoryControl.c
Production-ready x86_64 Memory Manager for UEFI Monolithic Kernel
Freestanding C, strict compliance, no stdlib
ИСПРАВЛЕНО: Совместимость с C23 (bool), устранены warning'и индентации и знаковых сравнений.
*/
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "debug_out.h"

/* Прямые вызовы для логирования */
extern void dbg_puts(const char *s);
extern void dbg_hex64(uint64_t v);

/* ==========================================================================
БАЗОВЫЕ ТИПЫ И КОНСТАНТЫ
========================================================================== */
typedef uint64_t           phys_addr_t;
typedef uint64_t           virt_addr_t;
typedef uint64_t           pte_t; /* ✅ ЕДИНЫЙ ТИП ДЛЯ ВСЕХ УРОВНЕЙ ТАБЛИЦ */

#define true  1
#define false 0

/* В C23 'bool' является ключевым словом. Для старых стандартов используем _Bool */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
typedef _Bool bool;
#endif

#define NULL ((void*)0)
#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1ULL))
#define ROUND_UP(n, align) (((n) + (align) - 1) & ~((align) - 1ULL))

/* Higher-Half Layout */
#define KERNEL_VIRT_BASE      0xFFFFFFFF80000000ULL
#define DIRECT_MAP_OFFSET     0xFFFF880000000000ULL
#define TEMP_MAP_VADDR        0xFFFFFFFFFFE00000ULL
#define USER_VIRT_LIMIT       0x00007FFFFFFFFFFFULL
#define HEAP_START_VADDR      0xFFFF880000000000ULL

/* UEFI Types */
typedef struct {
    uint32_t type; uint32_t pad;
    phys_addr_t physical_start; virt_addr_t virtual_start;
    uint64_t num_pages; uint32_t attribute;
} efi_memory_desc_t;

#define EFI_USABLE      7
#define EFI_ACPI_RECLAIM 11
#define EFI_ACPI_NVS    12
#define EFI_MMIO        13

/* Helpers */
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

/* Spinlock — раньше был пустой no-op в расчёте на то, что "прерывания
 * запрещены на уровне ядра" (UP). Это допущение перестало выполняться, как
 * только где-то в main.c начинает работать sti() — kmalloc/pmm/vmm-код
 * вызывается постоянно и дальше, уже с ВКЛЮЧЁННЫМИ прерываниями. Клик мышью
 * обрабатывается в IRQ-контексте и может дойти до elf64_spawn() (масса
 * kmalloc-вызовов) прямо ПОСРЕДИ kmalloc()/pmm/vmm-вызова в основном потоке
 * — без реальной блокировки это рекурсивно ломает внутренние структуры кучи
 * на середине обновления. Теперь acquire реально выключает прерывания (и
 * запоминает, был ли включён IF), release включает обратно только если он
 * был включён — безопасно и при вызове изнутри уже прерванного контекста. */
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

/* ==========================================================================
PHYSICAL MEMORY MANAGER
========================================================================== */
typedef struct {
    uint64_t *words; size_t total_frames; size_t free_frames; size_t num_words;
    spinlock_t lock; phys_addr_t bitmap_phys; size_t bitmap_pages;
} pmm_t;

static pmm_t g_pmm;

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
        idx = aligned + count;  /* пропускаем занятый диапазон */
    }
    return (size_t)-1;
}

static phys_addr_t efi_find_bitmap_region(const efi_memory_desc_t *map, size_t map_size, size_t desc_size, size_t needed_pages) {
    for (size_t i = 0; i < map_size; i += desc_size) {
        const efi_memory_desc_t *d = (const efi_memory_desc_t *)((uintptr_t)map + i);
        if (d->type == EFI_USABLE && d->num_pages >= needed_pages)
            return ROUND_UP(d->physical_start, PAGE_SIZE);
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
    g_pmm.free_frames = 0;  /* считается в цикле ниже */
    g_pmm.num_words = (total_frames + 63) >> 6;
    g_pmm.words = (uint64_t *)(uintptr_t)bm_pa;

    kmemset(g_pmm.words, 0xFF, g_pmm.num_words * sizeof(uint64_t));
    /* ВАЖНО: страницы, на которых лежит сам битмап (g_pmm.words), должны
     * остаться "занятыми" — они и так помечены как used исходным
     * 0xFF-заполнением выше. Раньше здесь стоял pmm_clear() по этим же
     * страницам, то есть аллокатор считал их свободными и мог выдать их
     * pmm_alloc_frame()'ом под что угодно (PML4, kmalloc-кучу, сегмент
     * ELF и т.д.) — первая же запись в такую "свежевыделенную" страницу
     * стирала бы часть самого битмапа, после чего pmm начинал путать
     * занято/свободно и выдавать одну и ту же физическую страницу
     * дважды под разные структуры. Никакого действия здесь не требуется. */

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

/*
 * pmm_alloc_frames_above — выделить count физически непрерывных фреймов,
 * все из которых >= min_phys. Используется для DMA-буферов которые не должны
 * попадать в conventional memory (0–1MB), где QEMU/OVMF/BIOS имеют свои
 * структуры и QEMU может не иметь доступа к этим физическим адресам.
 *
 * min_phys: минимальный физический адрес (обычно 0x100000 = 1MB).
 */
phys_addr_t pmm_alloc_frames_above(size_t count, size_t align, phys_addr_t min_phys) {
    if (count == 0 || (align & (align - 1)) != 0) return (phys_addr_t)-1;
    /* Минимальный фрейм (округляем вверх) */
    size_t min_frame = (size_t)((min_phys + PAGE_SIZE - 1) >> PAGE_SHIFT);
    /* align должен быть степенью двойки и >= 1 */
    size_t a = align ? align : 1;

    spinlock_acquire(&g_pmm.lock);
    size_t limit = g_pmm.total_frames;
    size_t idx = min_frame;
    phys_addr_t result = (phys_addr_t)-1;

    while (idx + count <= limit) {
        size_t cand = pmm_find_next_zero(idx, limit);
        if (cand == (size_t)-1) break;
        /* Выравниваем кандидата */
        size_t aligned = (cand + a - 1) & ~(a - 1);
        /* Не опускаемся ниже min_frame */
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

/*
 * pmm_alloc_frames_range — то же самое, что pmm_alloc_frames_above, но с
 * ВЕРХНЕЙ границей (max_phys, исключительно) в дополнение к нижней
 * (min_phys). Нужна контроллерам с 32-битным DMA (UHCI и, в общем случае,
 * EHCI без выставленного бита 64-bit addressing capability — см. USB.c) —
 * их структуры (frame list, QH, TD) обязаны лежать физически ниже 4 ГБ,
 * иначе адрес просто не влезет в 32-битное поле указателя в дескрипторе.
 * pmm_alloc_frames_above() такой гарантии не даёт (только нижнюю границу),
 * поэтому для USB-контроллеров использовать нужно именно эту функцию, а
 * не её.
 *
 * max_phys: верхняя граница (не включительно), обычно 0x100000000ULL (4 ГБ).
 */
phys_addr_t pmm_alloc_frames_range(size_t count, size_t align,
                                   phys_addr_t min_phys, phys_addr_t max_phys) {
    if (count == 0 || (align & (align - 1)) != 0) return (phys_addr_t)-1;
    if (max_phys <= min_phys) return (phys_addr_t)-1;

    size_t min_frame = (size_t)((min_phys + PAGE_SIZE - 1) >> PAGE_SHIFT);
    size_t max_frame = (size_t)(max_phys >> PAGE_SHIFT);   /* не включительно */
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

/* ==========================================================================
VIRTUAL MEMORY MANAGER (FLAT pte_t*)
========================================================================== */
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
    pte_t *kernel_pml4; /* ✅ УКАЗАТЕЛЬ НА ПЛОСКИЙ МАССИВ */
    spinlock_t lock;
    bool paging_active;
} vmm_t;

static vmm_t g_vmm;

/* g_kernel_pml4_phys — ФИЗИЧЕСКИЙ адрес g_vmm.kernel_pml4. Нужен отдельно
 * от g_vmm.kernel_pml4 (это указатель, kernel-virtual — после
 * vmm_activate() это уже phys_to_kvirt(pa), физический адрес из него
 * так просто не достать) — используется при переключении CR3 между
 * процессами (см. vmm_kernel_pml4_phys() ниже, Scheduler.c). */
static phys_addr_t g_kernel_pml4_phys = (phys_addr_t)-1;

/* sched_current_pml4_phys — реализована в Scheduler.c. Возвращает
 * pml4_phys ТЕКУЩЕЙ (исполняющей syscall) задачи, либо 0, если задачи
 * ещё нет или это kernel-поток без своего адресного пространства.
 * Нужна vmm_map_user() ниже, чтобы SYS_MMAP/SYS_SHM_AT/
 * sys_get_framebuffer из Syscall.c маппили память именно в адресное
 * пространство ВЫЗВАВШЕГО процесса, а не всегда в общую (уже более не
 * единственную!) g_vmm.kernel_pml4 — см. большой комментарий у
 * vmm_create_user_pml4(). */
extern uint64_t sched_current_pml4_phys(void);

static inline pte_t pte_make(phys_addr_t pa, uint64_t flags) { return (pa & PTE_ADDR_MASK) | flags | PTE_A; }
static void cpu_invlpg(virt_addr_t addr) { __asm__ volatile ("invlpg (%0)" :: "r"(addr) : "memory"); }
static void vmm_load_cr3(phys_addr_t pml4_pa) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_pa) : "memory");
    g_vmm.paging_active = true;
}
static inline void *phys_to_kvirt(phys_addr_t pa) { return (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET); }

/* До vmm_activate() активны таблицы загрузчика (identity); direct map ещё нет */
static inline void *phys_access(phys_addr_t pa) {
    if (!g_vmm.paging_active)
        return (void *)(uintptr_t)pa;
    return phys_to_kvirt(pa);
}

/* Forward declarations */
int vmm_map(virt_addr_t va, phys_addr_t pa, uint64_t flags);
int vmm_unmap(virt_addr_t va);

/* pa_in_ram — физический адрес реально лежит в границах RAM, которую
 * нашёл pmm_init_from_efi (total_frames). Используется И для "уже
 * присутствующих" записей (см. комментарий ниже), И — что оказалось
 * важнее — для СВЕЖЕВЫДЕЛЕННЫХ pmm_alloc_frame(): раньше код проверял
 * только pa == (phys_addr_t)-1 (полный отказ выделить), но НЕ проверял
 * что успешно "выделенный" pa вообще похож на разумный адрес внутри
 * реальной памяти — если тут баг где-то в pmm-аллокаторе (или просто
 * физическая память кончилась не тем способом, что даёт чистый -1),
 * kmemset() на такой pa падает тем же самым #GP, но ещё ДО того как
 * запись вообще попадает в таблицу — то есть первая версия проверки
 * (только на "уже присутствует") эту ветку вообще не защищала. */
static inline bool pa_in_ram(phys_addr_t pa) {
    return (pa >> PAGE_SHIFT) < g_pmm.total_frames;
}

static pte_t *vmm_walk_table(pte_t *pml4, virt_addr_t va, bool create) {
    size_t idx_p4 = PML4_IDX(va);
    size_t idx_p3 = PDPT_IDX(va);
    size_t idx_p2 = PD_IDX(va);
    size_t idx_p1 = PT_IDX(va);

    /* ВАЖНО: PTE_U здесь, на ПРОМЕЖУТОЧНЫХ уровнях (PML4E/PDPTE/PDE) —
     * не опционально. У x86-64 бит User/Supervisor проверяется на КАЖДОМ
     * уровне трансляции адреса, а не только в конечном PTE: если хотя бы
     * один уровень выше не имеет PTE_U, весь путь трактуется как
     * supervisor-only и ring3-доступ по нему получит page fault,
     * независимо от того, что выставлено в листовом PTE у vmm_map_user().
     * Раньше здесь стояло PTE_P | PTE_W без PTE_U — то есть ЛЮБАЯ user
     * страница, для которой требовалось впервые создать промежуточные
     * таблицы (что происходит для каждого нового процесса/региона
     * адресного пространства, ранее не тронутого), реально оставалась
     * недоступной из ring3. Здесь это не ослабляет защиту ядра: PTE_U
     * на промежуточном уровне лишь РАЗРЕШАЕТ user-доступ, если он ЕЩЁ
     * разрешён и на конечном PTE — chisto kernel-only маппинги
     * (vmm_map(), а не vmm_map_user()) листовой PTE_U не ставят вообще,
     * так что для них ничего не меняется. */
    if (!(pml4[idx_p4] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PML4E) вернул мусор, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        pml4[idx_p4] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
    /* Защита от "мусорной" записи: если под каким-то индексом лежит
     * непроинициализированная физическая память вида 0xFFFFFFFF...
     * (бывает при нехватке физической памяти на pmm_alloc_frame или при
     * первом же обращении к ранее нетронутой ветке адресного
     * пространства), бит Present (bit0) у неё СЛУЧАЙНО тоже окажется 1 —
     * код ниже принял бы её за валидную таблицу и разыменовал
     * phys_access() от бессмысленного адреса, получив неканонический
     * указатель и #GP без единой диагностической строки в логе. Вместо
     * слепого доверия проверяем, что извлечённый физический адрес
     * реально лежит в границах памяти, которую нашёл pmm_init_from_efi. */
    phys_addr_t p3_pa = pml4[idx_p4] & PTE_ADDR_MASK;
    if (!pa_in_ram(p3_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PML4E указывает за пределы RAM, pa", (uint64_t)p3_pa);
        return NULL;
    }
    pte_t *p3t = (pte_t *)phys_access(p3_pa);
    if (!(p3t[idx_p3] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PDPTE) вернул мусор, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        p3t[idx_p3] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
    phys_addr_t p2_pa = p3t[idx_p3] & PTE_ADDR_MASK;
    if (!pa_in_ram(p2_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PDPTE указывает за пределы RAM, pa", (uint64_t)p2_pa);
        return NULL;
    }
    pte_t *p2t = (pte_t *)phys_access(p2_pa);
    if (!(p2t[idx_p2] & PTE_P)) {
        if (!create) return NULL;
        phys_addr_t pa = pmm_alloc_frame();
        if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) {
            DBG_VAL("VMM", "vmm_walk_table: pmm_alloc_frame(PDE) вернул мусор, pa", (uint64_t)pa);
            return NULL;
        }
        kmemset(phys_access(pa), 0, PAGE_SIZE);
        p2t[idx_p2] = pte_make(pa, PTE_P | PTE_W | PTE_U);
    }
    phys_addr_t p1_pa = p2t[idx_p2] & PTE_ADDR_MASK;
    if (!pa_in_ram(p1_pa)) {
        DBG_VAL("VMM", "vmm_walk_table: PDE указывает за пределы RAM, pa", (uint64_t)p1_pa);
        return NULL;
    }
    pte_t *p1t = (pte_t *)phys_access(p1_pa);
    return &p1t[idx_p1];
}

/* Подавляем warning об неиспользуемых функциях, они могут понадобиться позже */
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
#define KERNEL_MAP_BYTES (8ULL * 1024 * 1024)

/* Вспомогательная функция: маппит 2 MiB huge page в PD (PTE_PS=1) */
static int vmm_map_2mb(virt_addr_t va, phys_addr_t pa, uint64_t flags) {
    /* va и pa должны быть выровнены на 2 MiB */
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

    /* Huge page: PTE_PS (бит 7) = 1, адрес выровнен на 2 MiB */
    p2t[idx_p2] = (pa & 0x000FFFFFFFE00000ULL) | flags | (1ULL << 7) /* PTE_PS */;
    return 0;
}

#define HUGE_PAGE_SIZE (2ULL << 20)  /* 2 MiB */
#define HUGE_PAGE_SHIFT 21

int vmm_init(void) {
    spinlock_init(&g_vmm.lock);
    g_vmm.paging_active = false;
    phys_addr_t pml4_pa = pmm_alloc_frame();
    if (pml4_pa == (phys_addr_t)-1) return -1;
    g_vmm.kernel_pml4 = (pte_t *)phys_access(pml4_pa);
    kmemset(g_vmm.kernel_pml4, 0, PAGE_SIZE);
    g_kernel_pml4_phys = pml4_pa;

    /* Direct map: 4 GiB через 2 MiB huge pages (2048 итераций вместо 1M) */
    uint64_t dm_flags = PTE_P | PTE_W | PTE_G | PTE_NX;
    for (size_t i = 0; i < (4ULL << 30) >> HUGE_PAGE_SHIFT; i++) {
        phys_addr_t pa = (phys_addr_t)i << HUGE_PAGE_SHIFT;
        int r = vmm_map_2mb(DIRECT_MAP_OFFSET + pa, pa, dm_flags);
        if (r) return r;
    }

    /* Ядро: 8 MiB через 2 MiB huge pages (4 итерации) */
    uint64_t kflags = PTE_P | PTE_W | PTE_G;
    for (size_t off = 0; off < KERNEL_MAP_BYTES; off += HUGE_PAGE_SIZE) {
        int r = vmm_map_2mb(KERNEL_VIRT_BASE + off, KERNEL_PHYS_BASE + off, kflags);
        if (r) return r;
    }
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

/* vmm_virt_to_phys — настоящий перевод kva -> pa через обход таблиц
 * страниц, а НЕ через "kva - DIRECT_MAP_OFFSET".
 *
 * ВАЖНО: последнее верно ТОЛЬКО для памяти, попавшей в исходные
 * статические 4 ГиБ direct-map, установленные один раз в vmm_init()
 * строго 1:1 (pa -> DIRECT_MAP_OFFSET+pa). Как только куча (kmalloc)
 * растёт за эти 4 ГиБ через km_expand(), это перестаёт быть верным:
 * km_expand берёт физический фрейм через pmm_alloc_frames() (может
 * оказаться где угодно в физической памяти), а виртуальный адрес
 * просто монотонно продолжает g_km.heap_end — соответствия "смещение
 * = разница" для такой памяти в общем случае НЕТ. Именно на этом
 * ошибочном допущении раньше был построен sc_get_framebuffer() в
 * Syscall.c: back buffer (kmalloc'нутый, а к моменту его выделения
 * куча УЖЕ выросла за 4 ГиБ) переводился в "pa" неверно, из-за чего
 * userspace-маппинг framebuffer'а указывал на случайную чужую
 * физическую страницу — писать в неё можно было без page fault (это
 * просто другая валидная RAM-страница), но эффекта на экране это не
 * давало, потому что fb_flip() копирует совсем другую физическую
 * страницу (ту, что реально стоит в PTE у back-buffer'а).
 *
 * Возвращает (phys_addr_t)-1, если va не замаплен вообще. */
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

/* ==========================================================================
KERNEL HEAP
========================================================================== */
#define KM_MIN_SIZE 16
#define KM_MAX_SIZE (4096 * 16)
#define KM_SIZE_CLASSES 12
#define KM_CLASS_SIZE(i) (KM_MIN_SIZE << (i))

/*
 * ВАЖНО: next/prev — это указатели ТОЛЬКО для двусвязного списка свободных
 * блоков данного size-class'а (валидны, только пока block->free == true).
 * phys_prev — отдельный указатель на блок, который располагается ФИЗИЧЕСКИ
 * непосредственно перед текущим в куче (NULL, если это самый первый блок
 * кучи). Он поддерживается всегда, независимо от free/allocated, и нужен
 * чтобы при kfree() можно было корректно найти физического соседа слева
 * для слияния (boundary-merge), не путая его со случайным мусором,
 * оставшимся в next/prev от предыдущего пребывания блока в фри-листе.
 */
typedef struct km_block {
    size_t size;
    bool free;
    struct km_block *next;       /* free-list link (валиден только если free) */
    struct km_block *prev;       /* free-list link (валиден только если free) */
    struct km_block *phys_prev;  /* физически предыдущий блок в куче, либо NULL */
} km_block_t;
typedef struct {
    km_block_t *free_lists[KM_SIZE_CLASSES];
    spinlock_t lock;
    virt_addr_t heap_end;
    size_t heap_used;
    km_block_t *last_block;      /* физически последний блок в куче (NULL если куча пуста) */
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
    
    dbg_puts("[KM] km_find_best: req=");
    dbg_hex64(req);
    dbg_puts(" class=");
    dbg_hex64(c);
    dbg_puts("\n");
    
    km_block_t *best = NULL;
    size_t best_diff = (size_t)-1;
    for (size_t i = c; i < KM_SIZE_CLASSES; i++) {
        km_block_t *b = g_km.free_lists[i];
        while (b) {
            /* Проверка валидности указателя */
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

        /* Блок, который раньше шёл физически сразу после best (если был),
         * теперь должен ссылаться на rem, а не на best — best стал короче. */
        km_block_t *old_next = (km_block_t *)((uint8_t *)best + sizeof(km_block_t) + orig_size);
        if ((uint8_t *)old_next < (uint8_t *)g_km.heap_end) {
            old_next->phys_prev = rem;
        } else {
            /* best был последним блоком кучи — теперь им стал rem */
            g_km.last_block = rem;
        }

        size_t r_class = km_size_class(rem->size);
        rem->next = g_km.free_lists[r_class];
        if (g_km.free_lists[r_class]) g_km.free_lists[r_class]->prev = rem;
        g_km.free_lists[r_class] = rem;

        best->size = req;
    }
    best->free = false;
    /* next/prev были указателями free-list'а и сейчас бессмысленны —
     * обнуляем, чтобы не оставлять "грязных" значений в занятом блоке. */
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

/*
 * Корректно отвязывает блок b от его текущего free-list, независимо от того,
 * в голове он, в середине или в хвосте списка. ДОЛЖЕН вызываться, пока
 * b->size ещё не изменён (класс размера вычисляется из текущего b->size).
 * После вызова next/prev блока b сброшены — он больше нигде не зарегистрирован.
 */
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
    
    km_block_t *blk = (km_block_t *)(uintptr_t)va;
    blk->size = (pages << PAGE_SHIFT) - sizeof(km_block_t);
    blk->free = true;
    blk->next = NULL;
    blk->prev = NULL;
    /* Новый блок всегда располагается ровно там, где раньше заканчивалась
     * куча, поэтому его физический предшественник — это блок, который был
     * последним до расширения (если куча уже не пуста). */
    blk->phys_prev = g_km.last_block;
    g_km.last_block = blk;
    km_insert_free(blk);
    g_km.heap_end = va + (pages << PAGE_SHIFT);
    return 0;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = ROUND_UP(size, 16);
    
    /* Проверка что g_km инициализирована */
    if (!g_km.heap_end) {
        dbg_puts("[KM] kmalloc: heap_end=0\n");
        return NULL;
    }
    
    dbg_puts("[KM] kmalloc: size=");
    dbg_hex64(size);
    dbg_puts(" heap_end=");
    dbg_hex64((uint64_t)(uintptr_t)g_km.heap_end);
    dbg_puts(" &g_km=");
    dbg_hex64((uint64_t)(uintptr_t)&g_km);
    dbg_puts("\n");
    
    spinlock_acquire(&g_km.lock);
    
    dbg_puts("[KM] kmalloc: spinlock acquired\n");
    
    km_block_t *b = km_find_best(size);
    
    dbg_puts("[KM] kmalloc: km_find_best=");
    dbg_hex64((uint64_t)(uintptr_t)b);
    dbg_puts("\n");
    
    if (!b) {
        dbg_puts("[KM] kmalloc: expanding\n");
        if (km_expand(size) != 0) {
            dbg_puts("[KM] kmalloc: expand FAILED\n");
            spinlock_release(&g_km.lock);
            return NULL;
        }
        dbg_puts("[KM] kmalloc: expand OK, retry\n");
        b = km_find_best(size);
    }
    spinlock_release(&g_km.lock);
    
    if (!b) {
        dbg_puts("[KM] kmalloc: still NULL\n");
        return NULL;
    }
    
    g_km.heap_used += b->size + sizeof(km_block_t);
    void *result = (void *)((uint8_t *)b + sizeof(km_block_t));
    dbg_puts("[KM] kmalloc: return=");
    dbg_hex64((uint64_t)(uintptr_t)result);
    dbg_puts("\n");
    return result;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    km_block_t *b = (km_block_t *)((uint8_t *)ptr - sizeof(km_block_t));

    /* ВРЕМЕННЫЙ дамп — убрать после отладки. Печатаем ВСЁ содержимое
     * заголовка блока ДО того, как что-либо с ним делаем — если тут
     * мусор (не 0/1 в free, огромный/нулевой size, дикие адреса в
     * next/prev/phys_prev), значит заголовок был испорчен ДО вызова
     * kfree (переполнение буфера где-то в самой структуре, например в
     * elf64_spawn_arg_t) — и падать будем на первой же операции с этим
     * мусором, независимо от того, насколько "правильно" защищён сам
     * kfree/km_list_unlink. */
    DBG_VAL("KM", "kfree: ptr", (uint64_t)(uintptr_t)ptr);
    DBG_VAL("KM", "kfree: block hdr addr", (uint64_t)(uintptr_t)b);
    DBG_VAL("KM", "kfree: b->size", (uint64_t)b->size);
    DBG_VAL("KM", "kfree: b->free", (uint64_t)b->free);
    DBG_VAL("KM", "kfree: b->next", (uint64_t)(uintptr_t)b->next);
    DBG_VAL("KM", "kfree: b->prev", (uint64_t)(uintptr_t)b->prev);
    DBG_VAL("KM", "kfree: b->phys_prev", (uint64_t)(uintptr_t)b->phys_prev);
    DBG_VAL("KM", "kfree: g_km.heap_end", (uint64_t)g_km.heap_end);
    DBG_VAL("KM", "kfree: g_km.last_block", (uint64_t)(uintptr_t)g_km.last_block);

    if (b->free) return; /* защита от double-free */

    spinlock_acquire(&g_km.lock);
    b->free = true;
    g_km.heap_used -= b->size + sizeof(km_block_t);

    /* --- Слияние с физически СЛЕДУЮЩИМ блоком (если он свободен) --- */
    DBG_MSG("KM", "kfree: checkpoint before next-merge");
    km_block_t *next = (km_block_t *)((uint8_t *)b + sizeof(km_block_t) + b->size);
    if ((uint8_t *)next < (uint8_t *)g_km.heap_end && next->free) {
        DBG_MSG("KM", "kfree: checkpoint next-merge taken");
        km_list_unlink(next);           /* корректно убираем next из ЛЮБОЙ позиции в его free-list */
        b->size += sizeof(km_block_t) + next->size;

        km_block_t *after_next = (km_block_t *)((uint8_t *)next + sizeof(km_block_t) + next->size);
        if ((uint8_t *)after_next < (uint8_t *)g_km.heap_end) {
            after_next->phys_prev = b;   /* был next, теперь физпредшественник — b */
        } else {
            g_km.last_block = b;         /* next был последним блоком кучи */
        }
        DBG_MSG("KM", "kfree: checkpoint next-merge done");
    }

    /* --- Слияние с физически ПРЕДЫДУЩИМ блоком (если он свободен) ---
     * Используем НАСТОЯЩИЙ физический указатель phys_prev, а не next/prev
     * (которые относятся только к free-list и могут быть "грязными"). */
    DBG_MSG("KM", "kfree: checkpoint before prev-merge");
    km_block_t *prevb = b->phys_prev;
    if (prevb && prevb->free) {
        DBG_MSG("KM", "kfree: checkpoint prev-merge taken");
        km_list_unlink(prevb);
        prevb->size += sizeof(km_block_t) + b->size;

        km_block_t *after_b = (km_block_t *)((uint8_t *)b + sizeof(km_block_t) + b->size);
        if ((uint8_t *)after_b < (uint8_t *)g_km.heap_end) {
            after_b->phys_prev = prevb;
        } else {
            g_km.last_block = prevb;
        }
        b = prevb; /* объединённый блок теперь представлен prevb */
        DBG_MSG("KM", "kfree: checkpoint prev-merge done");
    }

    DBG_MSG("KM", "kfree: checkpoint before km_insert_free");
    km_insert_free(b);
    DBG_MSG("KM", "kfree: checkpoint done, releasing lock");
    spinlock_release(&g_km.lock);
}

int kmalloc_init(void) {
    kmemset(&g_km, 0, sizeof(g_km));
    spinlock_init(&g_km.lock);
    g_km.heap_end = DIRECT_MAP_OFFSET + (4ULL << 30);
    g_km.heap_used = 0;
    return 0;
}

/* ==========================================================================
ИНИЦИАЛИЗАЦИЯ И ЭКСПОРТ
========================================================================== */
int mem_control_init(const void *efi_map, uint64_t map_size, uint64_t desc_size) {
    int r;
    DBG_MSG("KR", "12a pmm_init");
    r = pmm_init_from_efi((const efi_memory_desc_t *)efi_map, (size_t)map_size, (size_t)desc_size);
    if (r != 0) return -1;
    DBG_MSG("KR", "12b vmm_init");
    r = vmm_init();
    if (r != 0) return -2;
    DBG_MSG("KR", "12c kmalloc_init");
    r = kmalloc_init();
    if (r != 0) return -3;
    DBG_MSG("KR", "12d vmm_activate");
    vmm_activate();
    DBG_MSG("KR", "12e mem done");
    return 0;
}

/* =========================================================================
 * PER-PROCESS ADDRESS SPACES
 *
 * ИСПРАВЛЕНО: раньше ЛЮБОЙ вызов vmm_map_user() (и elf64_load() сегментов
 * ELF/стека, и sc_mmap/sc_shm_at/sc_get_framebuffer из Syscall.c) всегда
 * писал в ОДНУ И ТУ ЖЕ, единственную на всё ядро g_vmm.kernel_pml4. Это
 * значит, что у ВСЕХ userspace-процессов был буквально ОДИН общий набор
 * виртуальных адресов — включая стек (он всегда садится по фиксированному
 * USER_STACK_TOP из ELF64.c) и код (non-PIE ET_EXEC всегда грузится по
 * одному и тому же vaddr из файла). Пока одновременно жил только один
 * процесс — проблем не было. Как только запускались двое одновременно
 * (даже случайно, например повторный спавн, пока первый экземпляр ещё не
 * дошёл до sys_exit) — elf64_load() второго процесса ОБНУЛЯЛ и
 * переиспользовал те же самые физические страницы под ТЕ ЖЕ виртуальные
 * адреса, что использовал первый, — стирая его стек/код прямо у него
 * из-под ног. Результат — #PF с RIP=0 (адрес возврата на стеке
 * оказывается зануленным чужим elf64_load) в самый неожиданный момент.
 *
 * Фикс: у каждого userspace-процесса теперь СВОЯ верхнеуровневая таблица
 * (PML4) — vmm_create_user_pml4(). Нижняя половина (индексы 0..255,
 * user-адреса) в ней всегда пустая и заполняется индивидуально при
 * загрузке ELF (vmm_map_user_in). Верхняя половина (256..511, kernel:
 * direct map, код ядра, куча) — общие для ВСЕХ адресных пространств
 * указатели на ОДНИ И ТЕ ЖЕ физические таблицы, что и в g_vmm.kernel_pml4
 * (копируется один раз при создании, дальше ядро одинаково видно из
 * любого CR3). Собственно переключение CR3 при переключении задач —
 * в Scheduler.c::context_switch().
 * ========================================================================= */

/* vmm_kernel_pml4_phys — физический адрес "исходной" PML4 ядра (та, что
 * загружена в CR3 для kernel-потоков без своего pml4_phys, т.е. когда
 * task_t.pml4_phys == 0). Используется Scheduler.c при переключении CR3. */
phys_addr_t vmm_kernel_pml4_phys(void) { return g_kernel_pml4_phys; }

/* vmm_create_user_pml4 — новая PML4 для отдельного процесса: нижняя
 * половина (user, 0..255) — чистый ноль, верхняя (kernel, 256..511) —
 * копия соответствующих записей g_vmm.kernel_pml4 (те же физические
 * PDPT/PD/PT, не дублируются — копируется только сама верхнеуровневая
 * таблица, 512 записей по 8 байт). Возвращает физический адрес новой
 * PML4 или (phys_addr_t)-1 при нехватке памяти. */
phys_addr_t vmm_create_user_pml4(void) {
    phys_addr_t pa = pmm_alloc_frame();
    if (pa == (phys_addr_t)-1 || !pa_in_ram(pa)) return (phys_addr_t)-1;
    pte_t *pml4 = (pte_t *)phys_access(pa);
    kmemset(pml4, 0, PAGE_SIZE);
    for (size_t i = 256; i < 512; i++) pml4[i] = g_vmm.kernel_pml4[i];
    return pa;
}

/* vmm_free_user_pml4 — освобождает верхнеуровневую таблицу процесса.
 * ВАЖНО (известное упрощение, как и везде в этом проекте — см.
 * elf64_free(), который тоже не убирает записи в page tables при
 * освобождении физических страниц сегментов): здесь освобождается
 * ТОЛЬКО сама PML4-страница, а НЕ вложенные PDPT/PD/PT, которые
 * vmm_walk_table() создавала под нижнюю половину (user-адреса) этого
 * процесса — они остаются висеть как утечка (по несколько страниц на
 * процесс). Полная рекурсивная очистка требует прохода по всем
 * презентным записям на всех уровнях — не делаем сейчас, т.к. процессы
 * и так пока не переживают перезапуск ядра, а этот путь и раньше (для
 * общей PML4) утечки такого рода не закрывал. */
void vmm_free_user_pml4(phys_addr_t pml4_phys) {
    if (pml4_phys && pml4_phys != (phys_addr_t)-1) pmm_free_frames(pml4_phys, 1);
}

/* vmm_map_user_in — как vmm_map_user() ниже, но в ЯВНО указанную PML4
 * (для ELF64.c: сегменты/стек нового процесса грузятся ДО того, как он
 * стал "текущей" задачей — автоопределение через sched_current_pml4_phys()
 * тут даст неверный (чужой, текущий на момент спавна) адрес). */
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
    /* invlpg имеет смысл, только если pml4_phys сейчас реально в CR3;
     * при заполнении ЕЩЁ не запущенного процесса это не так, но лишний
     * invlpg здесь безвреден. */
    if (g_vmm.paging_active) cpu_invlpg(va);
    spinlock_release(&g_vmm.lock);
    return 0;
}

/* vmm_map_user — как раньше по сигнатуре (все старые вызовы в Syscall.c —
 * sc_mmap/sc_shm_at/sc_get_framebuffer — не тронуты), но теперь маппит
 * в PML4 ТЕКУЩЕЙ задачи (sched_current_pml4_phys()), а не всегда в
 * общую g_vmm.kernel_pml4 — иначе после появления отдельных PML4 у
 * процессов эти маппинги стали бы невидимы вызвавшему процессу (он
 * работает уже под своим CR3, а не под kernel_pml4). Если текущей
 * задачи ещё нет или это kernel-поток без своего адресного пространства
 * (pml4_phys == 0) — используем kernel_pml4, как и раньше. */
int vmm_map_user(virt_addr_t va, phys_addr_t pa, bool rw, bool exec) {
    uint64_t cur = sched_current_pml4_phys();
    phys_addr_t target = cur ? (phys_addr_t)cur : g_kernel_pml4_phys;
    return vmm_map_user_in(target, va, pa, rw, exec);
}

#ifdef __cplusplus
}
#endif