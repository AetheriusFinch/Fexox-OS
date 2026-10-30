/* Fexox OS, EULA GPLv2 */

#include "VFS.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames_range(uint64_t count, uint64_t align,
                                           phys_addr_t min_phys,
                                           phys_addr_t max_phys);
extern void pmm_free_frames(phys_addr_t pa, uint64_t count);

 
 
 

#ifndef NVME_DIRECT_MAP_OFFSET
#define NVME_DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#endif
#define NVME_DIRECT_MAP_SIZE   0x100000000ULL
#define NVME_DMA_MIN_PHYS      0x100000ULL
#define NVME_DMA_MAX_PHYS      0x100000000ULL
#define NVME_PAGE_SIZE         4096u

#define NVME_MAX_CONTROLLERS   4
#define NVME_MAX_NAMESPACES    8
#define NVME_MAX_IO_QUEUES     4u
#define NVME_ADMIN_QUEUE_DEPTH 16u
#define NVME_IO_QUEUE_DEPTH    64u       
#define NVME_DMA_SLOTS         8u        
#ifndef NVME_MAX_XFER_PAGES
#define NVME_MAX_XFER_PAGES    64u       
#endif
#ifndef NVME_PRP_LIST_PAGES
#define NVME_PRP_LIST_PAGES    1u        
#endif

#ifndef NVME_IRQ_VECTOR_BASE
#define NVME_IRQ_VECTOR_BASE   0x50u
#endif
#ifndef NVME_IRQ_VECTOR_COUNT
#define NVME_IRQ_VECTOR_COUNT  16u       

#endif
#ifndef NVME_HAVE_TICKS
#define NVME_HAVE_TICKS        1         
#endif

#define NVME_POLL_LIMIT        50000000u
#ifndef NVME_ADMIN_TIMEOUT_MS
#define NVME_ADMIN_TIMEOUT_MS  5000u
#endif
#ifndef NVME_IO_TIMEOUT_MS
#define NVME_IO_TIMEOUT_MS     8000u
#endif
#define NVME_MAX_RETRIES       3u

_Static_assert(NVME_IO_QUEUE_DEPTH <= 64u, "SQ/CQ must fit in one page");
_Static_assert(NVME_IRQ_VECTOR_COUNT >= 1u && NVME_IRQ_VECTOR_COUNT <= 32u,
               "vector bitmap is 32 bits");
_Static_assert(NVME_MAX_XFER_PAGES + 1u <= NVME_PRP_LIST_PAGES * 511u,
               "PRP list too small for the maximum transfer");

 
 
 

#define NVME_REG_CAP           0x0000
#define NVME_REG_VS            0x0008
#define NVME_REG_INTMS         0x000C
#define NVME_REG_INTMC         0x0010
#define NVME_REG_CC            0x0014
#define NVME_REG_CSTS          0x001C
#define NVME_REG_AQA           0x0024
#define NVME_REG_ASQ           0x0028
#define NVME_REG_ACQ           0x0030
#define NVME_REG_DOORBELL      0x1000

#define NVME_CC_EN             (1u << 0)
#define NVME_CC_SHN_NORMAL     (1u << 14)
#define NVME_CC_IOSQES         (6u << 16)
#define NVME_CC_IOCQES         (4u << 20)
#define NVME_CSTS_RDY          (1u << 0)
#define NVME_CSTS_CFS          (1u << 1)
#define NVME_CSTS_SHST_MASK    (3u << 2)
#define NVME_CSTS_SHST_DONE    (2u << 2)

#define NVME_ADMIN_DELETE_SQ   0x00
#define NVME_ADMIN_CREATE_SQ   0x01
#define NVME_ADMIN_GET_LOG     0x02
#define NVME_ADMIN_DELETE_CQ   0x04
#define NVME_ADMIN_CREATE_CQ   0x05
#define NVME_ADMIN_IDENTIFY    0x06
#define NVME_ADMIN_SET_FEAT    0x09
#define NVME_ADMIN_ASYNC_EVENT 0x0C

#define NVME_NVM_FLUSH         0x00
#define NVME_NVM_WRITE         0x01
#define NVME_NVM_READ          0x02
#define NVME_NVM_WRITE_ZEROES  0x08
#define NVME_NVM_DSM           0x09

#define NVME_FEAT_NUM_QUEUES   0x07
#define NVME_FEAT_ASYNC_EVENT  0x0B

#define NVME_IDENTIFY_NAMESPACE  0
#define NVME_IDENTIFY_CONTROLLER 1
#define NVME_IDENTIFY_ACTIVE_NS  2

#define NVME_ONCS_DSM          (1u << 2)
#define NVME_ONCS_WRITE_ZEROES (1u << 3)

 
#define NVME_SW_FLAG           0x8000u
#define NVME_SW_TIMEOUT        0x8001u
#define NVME_SW_ABORT          0x8002u
#define NVME_SW_NOTREADY       0x8003u
#define NVME_SW_NOMEM          0x8004u
#define NVME_SW_BADBUF         0x8005u

 
 
 

typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint32_t reserved[2];
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw[6];                      
} nvme_sqe_t;

typedef struct {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_cqe_t;

_Static_assert(sizeof(nvme_sqe_t) == 64, "NVMe submission entry must be 64 bytes");
_Static_assert(sizeof(nvme_cqe_t) == 16, "NVMe completion entry must be 16 bytes");

typedef struct { volatile uint32_t v; } nvme_lock_t;

enum { NVME_REQ_FREE = 0, NVME_REQ_INFLIGHT = 1, NVME_REQ_DONE = 2 };

typedef struct {
    volatile uint8_t state;
    uint8_t is_aer;
    uint8_t abandoned;                    
    uint8_t pad;
    uint16_t status;                      
    uint16_t pad2;
    uint32_t result;
} nvme_request_t;

typedef struct {
    nvme_lock_t lock;
    nvme_sqe_t *sq;
    nvme_cqe_t *cq;
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint16_t qid;
    uint16_t depth;
    uint16_t iv;                          
    uint8_t vector;                       
    uint8_t cq_phase;
    uint16_t sq_tail;
    uint16_t sq_head;
    uint16_t cq_head;
    uint16_t next_cid;
    uint16_t inflight;
    volatile uint8_t active;
    uint64_t irq_seen;
    uint64_t completions;
    uint64_t spurious;
    nvme_request_t req[NVME_IO_QUEUE_DEPTH];
} nvme_queue_t;

typedef struct {
    uint64_t *prp_list;                   
    uint64_t prp_phys;
    uint8_t *bounce;                      
    uint64_t bounce_phys;
    uint32_t bounce_bytes;
    volatile uint32_t in_use;
} nvme_dma_slot_t;

typedef enum {
    NVME_STATE_OFF = 0,
    NVME_STATE_INIT,
    NVME_STATE_READY,
    NVME_STATE_RESETTING,
    NVME_STATE_FAILED
} nvme_state_t;

typedef enum {
    NVME_IRQ_POLL = 0,
    NVME_IRQ_MSI,
    NVME_IRQ_MSIX
} nvme_irq_mode_t;

typedef struct {
    uint8_t bus, dev, fn;
    uint32_t index;
    uint64_t mmio_phys;
    uint64_t cap;
    uint32_t doorbell_stride;
    uint32_t version;
    volatile uint32_t state;
    volatile uint32_t recovering;

     
    nvme_irq_mode_t irq_mode;
    volatile uint32_t irq_ok;             
    uint8_t msi_cap, msix_cap;
    uint32_t n_iv;
    uint8_t iv_vector[NVME_MAX_IO_QUEUES + 1u];
    uint32_t vec_mask;                    
    uint32_t poll_rescues;

     
    uint16_t admin_depth, io_depth;
    nvme_queue_t admin;
    nvme_queue_t io[NVME_MAX_IO_QUEUES];
    uint32_t n_io_alloc;                  
    uint32_t n_io;                        
    volatile uint32_t io_rr;

     
    uint8_t *ident;
    uint64_t ident_phys;
    uint8_t *logbuf;
    uint64_t log_phys;
    nvme_dma_slot_t slots[NVME_DMA_SLOTS];
    uint32_t nslots;
    uint32_t max_xfer_bytes;

     
    uint32_t nn;
    uint16_t oncs;
    bool vwc;
    uint8_t aerl;
    char model[41];
    char serial[21];
    char fw[9];

     
    volatile uint32_t aer_event;
    volatile uint32_t aer_pending;
    volatile uint32_t aer_servicing;

     
    uint64_t resets;
    uint64_t timeouts;
    uint64_t io_errors;
    uint64_t io_cmds;
} nvme_controller_t;

typedef struct {
    nvme_controller_t *controller;
    uint32_t nsid;
    blkdev_t blkdev;
} nvme_namespace_t;

typedef struct {
    bool translate;                       
    uint64_t base;                        
} nvme_dma_map_t;

static nvme_controller_t g_nvme_controllers[NVME_MAX_CONTROLLERS];
static nvme_namespace_t g_nvme_namespaces[NVME_MAX_NAMESPACES];
static int g_nvme_controller_count;
static int g_nvme_namespace_count;
static blkdev_ops_t g_nvme_blkdev_ops;
static volatile uint64_t g_nvme_interrupt_count;
static bool g_nvme_irq_registered;
static uint32_t g_nvme_vec_used;
static uint64_t (*g_nvme_virt_to_phys)(const void *virt);

typedef void (*nvme_isr_handler_t)(uint64_t vector, uint64_t error,
                                   void *frame);
extern int interrupt_register_handler(uint16_t vector,
                                      nvme_isr_handler_t handler);
extern int interrupt_unregister_handler(uint16_t vector);
extern uint32_t interrupt_get_apic_id(void);
#if NVME_HAVE_TICKS
extern uint64_t interrupt_get_ticks(void);
extern uint32_t interrupt_get_timer_hz(void);
#endif

static void nvme_controller_teardown(nvme_controller_t *ctrl);
static bool nvme_controller_recover(nvme_controller_t *ctrl);

 
 
 

#ifndef NVME_ARCH_HOOKS
static inline uint64_t nvme_irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void nvme_irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" ::: "memory");
}

static inline bool nvme_irq_enabled(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(flags) :: "memory");
    return (flags & (1ULL << 9)) != 0;
}

 

static inline void nvme_idle_wait(volatile uint8_t *state) {
    __asm__ volatile ("cli" ::: "memory");
    if (*state != NVME_REQ_DONE)
        __asm__ volatile ("sti\n\thlt" ::: "memory");
    __asm__ volatile ("sti" ::: "memory");
}
#endif

static inline void nvme_relax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

static inline uint64_t nvme_lock(nvme_lock_t *lock) {
    uint64_t flags = nvme_irq_save();
    while (__atomic_exchange_n(&lock->v, 1u, __ATOMIC_ACQUIRE))
        nvme_relax();
    return flags;
}

static inline void nvme_unlock(nvme_lock_t *lock, uint64_t flags) {
    __atomic_store_n(&lock->v, 0u, __ATOMIC_RELEASE);
    nvme_irq_restore(flags);
}

#if NVME_HAVE_TICKS
static inline uint32_t nvme_hz(void) { return interrupt_get_timer_hz(); }
static inline uint64_t nvme_ticks(void) { return interrupt_get_ticks(); }
#else
static inline uint32_t nvme_hz(void) { return 0; }
static inline uint64_t nvme_ticks(void) { return 0; }
#endif

typedef struct {
    uint64_t start;
    uint64_t limit_ticks;
    uint32_t spins;
} nvme_deadline_t;

static void nvme_deadline_start(nvme_deadline_t *d, uint32_t ms) {
    uint32_t hz = nvme_hz();
    d->start = nvme_ticks();
    d->limit_ticks = hz ? ((uint64_t)ms * hz + 999u) / 1000u + 1u : 0;
    d->spins = 0;
}

 

static bool nvme_deadline_expired(nvme_deadline_t *d) {
    if (++d->spins > NVME_POLL_LIMIT)
        return true;
    if (d->limit_ticks && nvme_ticks() - d->start > d->limit_ticks)
        return true;
    return false;
}

static inline void nvme_copy(void *dst, const void *src, uint64_t bytes) {
    __asm__ volatile ("rep movsb"
                      : "+D"(dst), "+S"(src), "+c"(bytes) :: "memory");
}

static inline void nvme_zero(void *dst, uint64_t bytes) {
    __asm__ volatile ("rep stosb"
                      : "+D"(dst), "+c"(bytes) : "a"(0) : "memory");
}

static inline int nvme_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline void nvme_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

 
 
 

static inline uint32_t nvme_pci_read32(uint8_t bus, uint8_t dev,
                                       uint8_t fn, uint8_t reg) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                     | (reg & 0xFCu);
    __asm__ volatile ("outl %0, %1" :: "a"(address), "Nd"((uint16_t)0xCF8));
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"((uint16_t)0xCFC));
    return value;
}

static inline void nvme_pci_write32(uint8_t bus, uint8_t dev,
                                    uint8_t fn, uint8_t reg, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                     | (reg & 0xFCu);
    __asm__ volatile ("outl %0, %1" :: "a"(address), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outl %0, %1" :: "a"(value), "Nd"((uint16_t)0xCFC));
}

static inline uint16_t nvme_pci_read16(uint8_t bus, uint8_t dev,
                                       uint8_t fn, uint8_t reg) {
    uint32_t value = nvme_pci_read32(bus, dev, fn, reg);
    return (uint16_t)(value >> ((reg & 2u) * 8u));
}

static inline uint8_t nvme_pci_read8(uint8_t bus, uint8_t dev,
                                     uint8_t fn, uint8_t reg) {
    uint32_t value = nvme_pci_read32(bus, dev, fn, reg);
    return (uint8_t)(value >> ((reg & 3u) * 8u));
}

static inline void nvme_pci_write16(uint8_t bus, uint8_t dev, uint8_t fn,
                                    uint8_t reg, uint16_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                     | (reg & 0xFCu);
    uint16_t port = (uint16_t)(0xCFCu + (reg & 2u));
    __asm__ volatile ("outl %0, %1" :: "a"(address), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

static bool nvme_pci_bar_phys(uint8_t bus, uint8_t dev, uint8_t fn,
                              uint8_t bir, uint64_t *out) {
    if (bir > 5u)
        return false;
    uint32_t bar = nvme_pci_read32(bus, dev, fn, (uint8_t)(0x10u + bir * 4u));
    if (bar & 1u)
        return false;
    uint32_t type = (bar >> 1) & 3u;
    uint64_t phys = (uint64_t)bar & ~0xFULL;
    if (type == 2u) {
        if (bir >= 5u)
            return false;
        phys |= (uint64_t)nvme_pci_read32(bus, dev, fn,
                    (uint8_t)(0x14u + bir * 4u)) << 32;
    } else if (type != 0u) {
        return false;
    }
    if (!phys)
        return false;
    *out = phys;
    return true;
}

static void nvme_pci_find_caps(nvme_controller_t *ctrl) {
    ctrl->msi_cap = 0;
    ctrl->msix_cap = 0;
    uint16_t status = nvme_pci_read16(ctrl->bus, ctrl->dev, ctrl->fn, 0x06);
    if (!(status & (1u << 4)))
        return;
    uint8_t cap = nvme_pci_read8(ctrl->bus, ctrl->dev, ctrl->fn, 0x34) & 0xFCu;
    for (uint32_t steps = 0; cap >= 0x40u && cap <= 0xFCu && steps < 48u;
         steps++) {
        uint8_t id = nvme_pci_read8(ctrl->bus, ctrl->dev, ctrl->fn, cap);
        if (id == 0x05u)
            ctrl->msi_cap = cap;
        else if (id == 0x11u)
            ctrl->msix_cap = cap;
        uint8_t next = nvme_pci_read8(ctrl->bus, ctrl->dev, ctrl->fn,
                                      (uint8_t)(cap + 1u)) & 0xFCu;
        if (next == cap)
            break;
        cap = next;
    }
}

 
 
 

static inline uint32_t nvme_mmio_read32(const nvme_controller_t *ctrl,
                                        uint32_t offset) {
    __asm__ volatile ("mfence" ::: "memory");
    return *(volatile uint32_t *)(uintptr_t)
        (NVME_DIRECT_MAP_OFFSET + ctrl->mmio_phys + offset);
}

static inline uint64_t nvme_mmio_read64(const nvme_controller_t *ctrl,
                                        uint32_t offset) {
    __asm__ volatile ("mfence" ::: "memory");
    return *(volatile uint64_t *)(uintptr_t)
        (NVME_DIRECT_MAP_OFFSET + ctrl->mmio_phys + offset);
}

static inline void nvme_mmio_write32(const nvme_controller_t *ctrl,
                                     uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)
        (NVME_DIRECT_MAP_OFFSET + ctrl->mmio_phys + offset) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline void nvme_mmio_write64(const nvme_controller_t *ctrl,
                                     uint32_t offset, uint64_t value) {
    *(volatile uint64_t *)(uintptr_t)
        (NVME_DIRECT_MAP_OFFSET + ctrl->mmio_phys + offset) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline void nvme_phys_write32(uint64_t phys, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)(NVME_DIRECT_MAP_OFFSET + phys) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline uint32_t nvme_doorbell_offset(const nvme_controller_t *ctrl,
                                           uint16_t qid, bool completion) {
    uint32_t index = (uint32_t)qid * 2u + (completion ? 1u : 0u);
    return NVME_REG_DOORBELL + index * ctrl->doorbell_stride;
}

static uint32_t nvme_ctrl_timeout_ms(const nvme_controller_t *ctrl) {
    uint32_t to = (uint32_t)((ctrl->cap >> 24) & 0xFFu) * 500u;
    return to < 2000u ? 2000u : to;
}

 

static bool nvme_wait_ready(nvme_controller_t *ctrl, bool ready) {
    nvme_deadline_t dl;
    nvme_deadline_start(&dl, nvme_ctrl_timeout_ms(ctrl) + 500u);
    for (;;) {
        uint32_t status = nvme_mmio_read32(ctrl, NVME_REG_CSTS);
        if (ready && (status & NVME_CSTS_CFS))
            return false;
        if (!!(status & NVME_CSTS_RDY) == ready)
            return true;
        if (nvme_deadline_expired(&dl))
            return false;
        nvme_relax();
    }
}

 
 
 

static void *nvme_dma_alloc(uint64_t bytes, uint64_t *phys_out) {
    uint64_t pages = (bytes + NVME_PAGE_SIZE - 1) / NVME_PAGE_SIZE;
    phys_addr_t phys = pmm_alloc_frames_range(pages, 1, NVME_DMA_MIN_PHYS,
                                               NVME_DMA_MAX_PHYS);
    if (phys == (phys_addr_t)-1)
        return NULL;

    uint8_t *virt = (uint8_t *)(uintptr_t)(NVME_DIRECT_MAP_OFFSET + phys);
    nvme_zero(virt, pages * NVME_PAGE_SIZE);
    *phys_out = phys;
    return virt;
}

static void nvme_dma_free(void *virt, uint64_t bytes) {
    if (!virt)
        return;
    uint64_t pages = (bytes + NVME_PAGE_SIZE - 1) / NVME_PAGE_SIZE;
    uint64_t phys = (uint64_t)(uintptr_t)virt - NVME_DIRECT_MAP_OFFSET;
    pmm_free_frames(phys, pages);
}

 
 
 

static int nvme_vec_alloc(void) {
    for (uint32_t i = 0; i < NVME_IRQ_VECTOR_COUNT; i++) {
        if (!(g_nvme_vec_used & (1u << i))) {
            g_nvme_vec_used |= 1u << i;
            return (int)(NVME_IRQ_VECTOR_BASE + i);
        }
    }
    return -1;
}

static void nvme_vec_release_mask(uint32_t mask) {
    g_nvme_vec_used &= ~mask;
}

static uint32_t nvme_ctrl_irq_total(const nvme_controller_t *ctrl) {
    uint64_t total = ctrl->admin.irq_seen;
    for (uint32_t i = 0; i < NVME_MAX_IO_QUEUES; i++)
        total += ctrl->io[i].irq_seen;
    return (uint32_t)total;
}

 
 
 

enum { NVME_POST_OK = 0, NVME_POST_BUSY = 1, NVME_POST_DOWN = 2 };

 
static int nvme_post_locked(nvme_controller_t *ctrl, nvme_queue_t *q,
                            const nvme_sqe_t *sqe, bool aer,
                            uint16_t *cid_out) {
    if (!q->active)
        return NVME_POST_DOWN;

    uint16_t next = (uint16_t)((q->sq_tail + 1u) % q->depth);
    if (next == q->sq_head)
        return NVME_POST_BUSY;

    uint16_t cid = 0xFFFFu;
    for (uint32_t i = 0; i < q->depth; i++) {
        uint16_t c = (uint16_t)((q->next_cid + i) % q->depth);
        if (q->req[c].state == NVME_REQ_FREE) {
            cid = c;
            break;
        }
    }
    if (cid == 0xFFFFu)
        return NVME_POST_BUSY;
    q->next_cid = (uint16_t)((cid + 1u) % q->depth);

    nvme_request_t *r = &q->req[cid];
    r->status = 0;
    r->result = 0;
    r->abandoned = 0;
    r->is_aer = aer ? 1u : 0u;
    __atomic_store_n(&r->state, (uint8_t)NVME_REQ_INFLIGHT, __ATOMIC_RELEASE);

    nvme_sqe_t *slot = &q->sq[q->sq_tail];
    *slot = *sqe;
    slot->cid = cid;
    __asm__ volatile ("mfence" ::: "memory");
    q->sq_tail = next;
    q->inflight++;
    nvme_mmio_write32(ctrl, nvme_doorbell_offset(ctrl, q->qid, false),
                      q->sq_tail);
    *cid_out = cid;
    return NVME_POST_OK;
}

 
static void nvme_aer_complete(nvme_controller_t *ctrl, nvme_queue_t *q,
                              uint16_t cid) {
    nvme_request_t *r = &q->req[cid];
    uint16_t status = r->status;
    uint32_t result = r->result;
    r->is_aer = 0;
    __atomic_store_n(&r->state, (uint8_t)NVME_REQ_FREE, __ATOMIC_RELEASE);

    if (status != 0)
        return;                           
    ctrl->aer_event = result;
    __atomic_store_n(&ctrl->aer_pending, 1u, __ATOMIC_RELEASE);

    nvme_sqe_t sqe;
    nvme_zero(&sqe, sizeof(sqe));
    sqe.opcode = NVME_ADMIN_ASYNC_EVENT;
    uint16_t new_cid;
    nvme_post_locked(ctrl, q, &sqe, true, &new_cid);
}

static inline bool nvme_cq_peek(const nvme_queue_t *q) {
    const volatile nvme_cqe_t *cqe = &q->cq[q->cq_head];
    return (cqe->status & 1u) == q->cq_phase;
}

 
static uint32_t nvme_queue_reap(nvme_controller_t *ctrl, nvme_queue_t *q) {
    uint32_t reaped = 0;
    uint64_t flags = nvme_lock(&q->lock);
    if (q->active) {
        for (;;) {
            volatile nvme_cqe_t *cqe = &q->cq[q->cq_head];
            uint16_t status = cqe->status;
            if ((status & 1u) != q->cq_phase)
                break;
            __asm__ volatile ("lfence" ::: "memory");
            uint16_t cid = cqe->cid;
            uint32_t result = cqe->result;
            uint16_t sq_head = cqe->sq_head;
            if (sq_head < q->depth)
                q->sq_head = sq_head;

            if (cid < q->depth
                && q->req[cid].state == NVME_REQ_INFLIGHT) {
                nvme_request_t *r = &q->req[cid];
                r->status = (uint16_t)(status >> 1);
                r->result = result;
                if (q->inflight)
                    q->inflight--;
                if (r->is_aer) {
                    nvme_aer_complete(ctrl, q, cid);
                } else if (r->abandoned) {
                    r->abandoned = 0;
                    __atomic_store_n(&r->state, (uint8_t)NVME_REQ_FREE,
                                     __ATOMIC_RELEASE);
                } else {
                    __atomic_store_n(&r->state, (uint8_t)NVME_REQ_DONE,
                                     __ATOMIC_RELEASE);
                }
            } else {
                q->spurious++;
            }

            q->cq_head = (uint16_t)((q->cq_head + 1u) % q->depth);
            if (q->cq_head == 0)
                q->cq_phase ^= 1u;
            reaped++;
        }
        if (reaped) {
            q->completions += reaped;
            nvme_mmio_write32(ctrl,
                nvme_doorbell_offset(ctrl, q->qid, true), q->cq_head);
        }
    }
    nvme_unlock(&q->lock, flags);
    return reaped;
}

 
static void nvme_interrupt_handler(uint64_t vector, uint64_t error,
                                   void *frame) {
    (void)error;
    (void)frame;
    __atomic_fetch_add(&g_nvme_interrupt_count, 1, __ATOMIC_RELAXED);

    for (int c = 0; c < NVME_MAX_CONTROLLERS; c++) {
        nvme_controller_t *ctrl = &g_nvme_controllers[c];
        if (ctrl->irq_mode == NVME_IRQ_POLL)
            continue;
        if (ctrl->admin.active && ctrl->admin.vector == vector) {
            ctrl->admin.irq_seen++;
            nvme_queue_reap(ctrl, &ctrl->admin);
        }
        for (uint32_t i = 0; i < NVME_MAX_IO_QUEUES; i++) {
            nvme_queue_t *q = &ctrl->io[i];
            if (q->active && q->vector == vector) {
                q->irq_seen++;
                nvme_queue_reap(ctrl, q);
            }
        }
    }
}

 



static uint32_t nvme_wait_cid(nvme_controller_t *ctrl, nvme_queue_t *q,
                              uint16_t cid, uint32_t timeout_ms,
                              uint32_t *result) {
    nvme_request_t *r = &q->req[cid];
    nvme_deadline_t dl;
    nvme_deadline_start(&dl, timeout_ms);

    while (__atomic_load_n(&r->state, __ATOMIC_ACQUIRE) != NVME_REQ_DONE) {
        bool sleepy = ctrl->irq_mode != NVME_IRQ_POLL && ctrl->irq_ok
                   && nvme_hz() != 0 && nvme_irq_enabled();
        if (sleepy) {
            nvme_idle_wait(&r->state);
            if (r->state != NVME_REQ_DONE && nvme_cq_peek(q)) {
                 
                if (nvme_queue_reap(ctrl, q)) {
                    ctrl->poll_rescues++;
                    if (ctrl->poll_rescues >= 16u
                        && nvme_ctrl_irq_total(ctrl) < ctrl->poll_rescues) {
                        ctrl->irq_ok = 0;
                        DBG_MSG("NVME", "interrupts are not delivered; polling");
                    }
                }
            }
        } else {
            if (nvme_cq_peek(q))
                nvme_queue_reap(ctrl, q);
            else
                nvme_relax();
        }

        if (r->state != NVME_REQ_DONE && nvme_deadline_expired(&dl)) {
            bool abandon = false;
            uint64_t flags = nvme_lock(&q->lock);
            if (r->state == NVME_REQ_INFLIGHT) {
                r->abandoned = 1;
                abandon = true;
            }
            nvme_unlock(&q->lock, flags);
            if (abandon) {
                ctrl->timeouts++;
                return NVME_SW_TIMEOUT;
            }
        }
    }

    uint32_t status = r->status;
    if (result)
        *result = r->result;
    __atomic_store_n(&r->state, (uint8_t)NVME_REQ_FREE, __ATOMIC_RELEASE);
    return status;
}

 
static uint32_t nvme_submit_async(nvme_controller_t *ctrl, nvme_queue_t *q,
                                  const nvme_sqe_t *sqe, uint32_t timeout_ms,
                                  uint16_t *cid_out) {
    nvme_deadline_t dl;
    nvme_deadline_start(&dl, timeout_ms);
    for (;;) {
        uint64_t flags = nvme_lock(&q->lock);
        int rc = nvme_post_locked(ctrl, q, sqe, false, cid_out);
        nvme_unlock(&q->lock, flags);
        if (rc == NVME_POST_OK)
            return 0;
        if (rc == NVME_POST_DOWN)
            return NVME_SW_NOTREADY;
        nvme_queue_reap(ctrl, q);
        if (nvme_deadline_expired(&dl))
            return NVME_SW_TIMEOUT;
        nvme_relax();
    }
}

static uint32_t nvme_cmd_sync(nvme_controller_t *ctrl, nvme_queue_t *q,
                              const nvme_sqe_t *sqe, uint32_t timeout_ms,
                              uint32_t *result) {
    uint16_t cid;
    uint32_t st = nvme_submit_async(ctrl, q, sqe, timeout_ms, &cid);
    if (st)
        return st;
    return nvme_wait_cid(ctrl, q, cid, timeout_ms, result);
}

static bool nvme_admin_cmd(nvme_controller_t *ctrl, uint8_t opcode,
                           uint32_t nsid, uint64_t prp1,
                           uint32_t cdw10, uint32_t cdw11,
                           uint32_t *result) {
    nvme_sqe_t sqe;
    nvme_zero(&sqe, sizeof(sqe));
    sqe.opcode = opcode;
    sqe.nsid = nsid;
    sqe.prp1 = prp1;
    sqe.cdw[0] = cdw10;
    sqe.cdw[1] = cdw11;
    uint32_t st = nvme_cmd_sync(ctrl, &ctrl->admin, &sqe,
                                NVME_ADMIN_TIMEOUT_MS, result);
    if (st == NVME_SW_TIMEOUT && ctrl->state == NVME_STATE_READY)
        nvme_controller_recover(ctrl);
    if (st) {
        DBG_VAL("NVME", "admin command failed, opcode", opcode);
        DBG_VAL("NVME", "admin command status", st);
    }
    return st == 0;
}

 
 
 

static void nvme_irq_teardown(nvme_controller_t *ctrl) {
    if (ctrl->irq_mode == NVME_IRQ_MSIX && ctrl->msix_cap) {
        uint16_t mc = nvme_pci_read16(ctrl->bus, ctrl->dev, ctrl->fn,
                                      (uint8_t)(ctrl->msix_cap + 2u));
        nvme_pci_write16(ctrl->bus, ctrl->dev, ctrl->fn,
                         (uint8_t)(ctrl->msix_cap + 2u),
                         (uint16_t)(mc & ~0x8000u));
    } else if (ctrl->irq_mode == NVME_IRQ_MSI && ctrl->msi_cap) {
        uint16_t mc = nvme_pci_read16(ctrl->bus, ctrl->dev, ctrl->fn,
                                      (uint8_t)(ctrl->msi_cap + 2u));
        nvme_pci_write16(ctrl->bus, ctrl->dev, ctrl->fn,
                         (uint8_t)(ctrl->msi_cap + 2u),
                         (uint16_t)(mc & ~1u));
    }
    nvme_vec_release_mask(ctrl->vec_mask);
    ctrl->vec_mask = 0;
    ctrl->irq_mode = NVME_IRQ_POLL;
    ctrl->n_iv = 0;
}

static bool nvme_setup_msix(nvme_controller_t *ctrl) {
    uint8_t cap = ctrl->msix_cap;
    uint8_t b = ctrl->bus, d = ctrl->dev, f = ctrl->fn;

    uint16_t mc = nvme_pci_read16(b, d, f, (uint8_t)(cap + 2u));
    uint32_t table_size = (uint32_t)(mc & 0x7FFu) + 1u;
    uint32_t table_reg = nvme_pci_read32(b, d, f, (uint8_t)(cap + 4u));
    uint8_t bir = (uint8_t)(table_reg & 7u);
    uint32_t table_off = table_reg & ~7u;

    uint64_t bar_phys;
    if (bir == 0)
        bar_phys = ctrl->mmio_phys;
    else if (!nvme_pci_bar_phys(b, d, f, bir, &bar_phys))
        return false;
    uint64_t table_phys = bar_phys + table_off;
    if (table_phys + (uint64_t)table_size * 16u > NVME_DIRECT_MAP_SIZE) {
        DBG_MSG("NVME", "MSI-X table outside the direct map");
        return false;
    }

    uint32_t want = table_size < NVME_MAX_IO_QUEUES + 1u
                  ? table_size : NVME_MAX_IO_QUEUES + 1u;
    uint32_t got = 0;
    uint32_t mask = 0;
    uint8_t vectors[NVME_MAX_IO_QUEUES + 1u];
    for (uint32_t i = 0; i < want; i++) {
        int v = nvme_vec_alloc();
        if (v < 0)
            break;
        vectors[got++] = (uint8_t)v;
        mask |= 1u << ((uint32_t)v - NVME_IRQ_VECTOR_BASE);
    }
    if (!got)
        return false;

     
    if (ctrl->msi_cap) {
        uint16_t c = nvme_pci_read16(b, d, f, (uint8_t)(ctrl->msi_cap + 2u));
        if (c & 1u)
            nvme_pci_write16(b, d, f, (uint8_t)(ctrl->msi_cap + 2u),
                             (uint16_t)(c & ~1u));
    }
    nvme_pci_write16(b, d, f, (uint8_t)(cap + 2u),
                     (uint16_t)((mc & ~0x8000u) | 0x4000u));

    uint32_t address = 0xFEE00000u
        | ((interrupt_get_apic_id() & 0xFFu) << 12);
    for (uint32_t i = 0; i < want; i++) {
        uint64_t entry = table_phys + (uint64_t)i * 16u;
        uint8_t v = vectors[i < got ? i : i % got];
        ctrl->iv_vector[i] = v;
        nvme_phys_write32(entry + 12, 1u);           
        nvme_phys_write32(entry + 0, address);
        nvme_phys_write32(entry + 4, 0);
        nvme_phys_write32(entry + 8, v);
        nvme_phys_write32(entry + 12, 0);            
    }

    nvme_pci_write16(b, d, f, (uint8_t)(cap + 2u),
                     (uint16_t)((mc | 0x8000u) & ~0x4000u));
    uint16_t check = nvme_pci_read16(b, d, f, (uint8_t)(cap + 2u));
    if (!(check & 0x8000u)) {
        nvme_vec_release_mask(mask);
        DBG_MSG("NVME", "PCI MSI-X enable failed");
        return false;
    }

    ctrl->vec_mask = mask;
    ctrl->n_iv = want;
    ctrl->irq_mode = NVME_IRQ_MSIX;
    return true;
}

static bool nvme_setup_msi(nvme_controller_t *ctrl) {
    uint8_t cap = ctrl->msi_cap;
    uint8_t b = ctrl->bus, d = ctrl->dev, f = ctrl->fn;

    uint16_t control = nvme_pci_read16(b, d, f, (uint8_t)(cap + 2u));
    bool address_64 = (control & (1u << 7)) != 0;
    bool per_vector_mask = (control & (1u << 8)) != 0;
    if (per_vector_mask && cap > (address_64 ? 0xECu : 0xF0u)) {
        DBG_MSG("NVME", "malformed PCI MSI capability");
        return false;
    }

    int vec = nvme_vec_alloc();
    if (vec < 0)
        return false;

    if (ctrl->msix_cap) {
        uint16_t mc = nvme_pci_read16(b, d, f, (uint8_t)(ctrl->msix_cap + 2u));
        if (mc & 0x8000u)
            nvme_pci_write16(b, d, f, (uint8_t)(ctrl->msix_cap + 2u),
                             (uint16_t)(mc & ~0x8000u));
    }
    nvme_pci_write16(b, d, f, (uint8_t)(cap + 2u), (uint16_t)(control & ~1u));

    uint32_t address = 0xFEE00000u
        | ((interrupt_get_apic_id() & 0xFFu) << 12);
    nvme_pci_write32(b, d, f, (uint8_t)(cap + 4u), address);
    if (address_64) {
        nvme_pci_write32(b, d, f, (uint8_t)(cap + 8u), 0);
        nvme_pci_write16(b, d, f, (uint8_t)(cap + 12u), (uint16_t)vec);
        if (per_vector_mask)
            nvme_pci_write32(b, d, f, (uint8_t)(cap + 16u), 0);
    } else {
        nvme_pci_write16(b, d, f, (uint8_t)(cap + 8u), (uint16_t)vec);
        if (per_vector_mask)
            nvme_pci_write32(b, d, f, (uint8_t)(cap + 12u), 0);
    }

    control &= (uint16_t)~((7u << 4) | 1u);       
    control |= 1u;
    nvme_pci_write16(b, d, f, (uint8_t)(cap + 2u), control);
    if (!(nvme_pci_read16(b, d, f, (uint8_t)(cap + 2u)) & 1u)) {
        nvme_vec_release_mask(1u << ((uint32_t)vec - NVME_IRQ_VECTOR_BASE));
        DBG_MSG("NVME", "PCI MSI enable failed");
        return false;
    }

    ctrl->iv_vector[0] = (uint8_t)vec;
    ctrl->vec_mask = 1u << ((uint32_t)vec - NVME_IRQ_VECTOR_BASE);
    ctrl->n_iv = 1;
    ctrl->irq_mode = NVME_IRQ_MSI;
    return true;
}

static void nvme_irq_setup(nvme_controller_t *ctrl) {
    ctrl->irq_mode = NVME_IRQ_POLL;
    ctrl->n_iv = 0;
    ctrl->irq_ok = 1;
    nvme_pci_find_caps(ctrl);

    if (g_nvme_irq_registered) {
        if (ctrl->msix_cap && nvme_setup_msix(ctrl)) {
            DBG_VAL("NVME", "using MSI-X, vectors", ctrl->n_iv);
        } else if (ctrl->msi_cap && nvme_setup_msi(ctrl)) {
            DBG_MSG("NVME", "using MSI");
        } else {
            DBG_MSG("NVME", "no MSI/MSI-X; using polling");
        }
    } else {
        DBG_MSG("NVME", "IRQ handlers not registered; using polling");
    }

     
    uint16_t cmd = nvme_pci_read16(ctrl->bus, ctrl->dev, ctrl->fn, 0x04);
    nvme_pci_write16(ctrl->bus, ctrl->dev, ctrl->fn, 0x04,
                     (uint16_t)(cmd | 0x0406u));      
}

static uint16_t nvme_queue_iv(const nvme_controller_t *ctrl, uint16_t qid) {
    if (ctrl->irq_mode != NVME_IRQ_MSIX || ctrl->n_iv < 2u || qid == 0)
        return 0;
    return (uint16_t)(1u + (uint32_t)(qid - 1u) % (ctrl->n_iv - 1u));
}

 
 
 

static bool nvme_queue_alloc(nvme_queue_t *q, uint16_t qid, uint16_t depth) {
    q->qid = qid;
    q->depth = depth;
    q->sq = (nvme_sqe_t *)nvme_dma_alloc(NVME_PAGE_SIZE, &q->sq_phys);
    q->cq = (nvme_cqe_t *)nvme_dma_alloc(NVME_PAGE_SIZE, &q->cq_phys);
    return q->sq && q->cq;
}

static void nvme_queue_free(nvme_queue_t *q) {
    nvme_dma_free(q->sq, NVME_PAGE_SIZE);
    nvme_dma_free(q->cq, NVME_PAGE_SIZE);
    q->sq = NULL;
    q->cq = NULL;
}

 
static void nvme_queue_reset_ring(nvme_queue_t *q) {
    q->sq_tail = 0;
    q->sq_head = 0;
    q->cq_head = 0;
    q->cq_phase = 1;
    q->inflight = 0;
    q->next_cid = 0;
    nvme_zero(q->sq, NVME_PAGE_SIZE);
    nvme_zero(q->cq, NVME_PAGE_SIZE);
}

 
static void nvme_queue_abort_requests(nvme_queue_t *q) {
    uint64_t flags = nvme_lock(&q->lock);
    for (uint32_t i = 0; i < NVME_IO_QUEUE_DEPTH; i++) {
        nvme_request_t *r = &q->req[i];
        if (r->state != NVME_REQ_INFLIGHT)
            continue;
        if (r->is_aer || r->abandoned) {
            r->is_aer = 0;
            r->abandoned = 0;
            __atomic_store_n(&r->state, (uint8_t)NVME_REQ_FREE,
                             __ATOMIC_RELEASE);
        } else {
            r->status = NVME_SW_ABORT;
            __atomic_store_n(&r->state, (uint8_t)NVME_REQ_DONE,
                             __ATOMIC_RELEASE);
        }
    }
    q->inflight = 0;
    nvme_unlock(&q->lock, flags);
}

static void nvme_queue_deactivate(nvme_queue_t *q) {
    uint64_t flags = nvme_lock(&q->lock);
    q->active = 0;
    nvme_unlock(&q->lock, flags);
}

static bool nvme_slots_alloc(nvme_controller_t *ctrl) {
    ctrl->nslots = 0;
    for (uint32_t i = 0; i < NVME_DMA_SLOTS; i++) {
        nvme_dma_slot_t *s = &ctrl->slots[i];
        s->prp_list = (uint64_t *)nvme_dma_alloc(
            NVME_PRP_LIST_PAGES * NVME_PAGE_SIZE, &s->prp_phys);
        if (!s->prp_list)
            break;
        s->bounce = NULL;
        s->bounce_bytes = 0;
        s->in_use = 0;
        ctrl->nslots++;
    }
    return ctrl->nslots != 0;
}

static void nvme_controller_free_memory(nvme_controller_t *ctrl) {
    nvme_queue_free(&ctrl->admin);
    for (uint32_t i = 0; i < NVME_MAX_IO_QUEUES; i++)
        nvme_queue_free(&ctrl->io[i]);
    nvme_dma_free(ctrl->ident, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->logbuf, NVME_PAGE_SIZE);
    ctrl->ident = NULL;
    ctrl->logbuf = NULL;
    for (uint32_t i = 0; i < ctrl->nslots; i++) {
        nvme_dma_free(ctrl->slots[i].prp_list,
                      NVME_PRP_LIST_PAGES * NVME_PAGE_SIZE);
        nvme_dma_free(ctrl->slots[i].bounce, ctrl->slots[i].bounce_bytes);
        ctrl->slots[i].prp_list = NULL;
        ctrl->slots[i].bounce = NULL;
        ctrl->slots[i].bounce_bytes = 0;
    }
    ctrl->nslots = 0;
}

static bool nvme_controller_alloc(nvme_controller_t *ctrl) {
    uint32_t mqes = (uint32_t)(ctrl->cap & 0xFFFFu) + 1u;
    ctrl->admin_depth = (uint16_t)(mqes < NVME_ADMIN_QUEUE_DEPTH
                                   ? mqes : NVME_ADMIN_QUEUE_DEPTH);
    ctrl->io_depth = (uint16_t)(mqes < NVME_IO_QUEUE_DEPTH
                                ? mqes : NVME_IO_QUEUE_DEPTH);

    uint32_t want_io = 1;
    if (ctrl->irq_mode == NVME_IRQ_MSIX && ctrl->n_iv > 2u)
        want_io = ctrl->n_iv - 1u;
    if (want_io > NVME_MAX_IO_QUEUES)
        want_io = NVME_MAX_IO_QUEUES;

    if (!nvme_queue_alloc(&ctrl->admin, 0, ctrl->admin_depth))
        return false;
    ctrl->n_io_alloc = 0;
    for (uint32_t i = 0; i < want_io; i++) {
        if (!nvme_queue_alloc(&ctrl->io[i], (uint16_t)(i + 1u),
                              ctrl->io_depth))
            break;
        ctrl->n_io_alloc++;
    }
    if (!ctrl->n_io_alloc)
        return false;

    ctrl->ident = (uint8_t *)nvme_dma_alloc(NVME_PAGE_SIZE, &ctrl->ident_phys);
    ctrl->logbuf = (uint8_t *)nvme_dma_alloc(NVME_PAGE_SIZE, &ctrl->log_phys);
    if (!ctrl->ident || !ctrl->logbuf)
        return false;
    return nvme_slots_alloc(ctrl);
}

 
 
 

static bool nvme_aer_start(nvme_controller_t *ctrl) {
    nvme_sqe_t sqe;
    nvme_zero(&sqe, sizeof(sqe));
    sqe.opcode = NVME_ADMIN_ASYNC_EVENT;
    uint16_t cid;
    uint64_t flags = nvme_lock(&ctrl->admin.lock);
    int rc = nvme_post_locked(ctrl, &ctrl->admin, &sqe, true, &cid);
    nvme_unlock(&ctrl->admin.lock, flags);
    return rc == NVME_POST_OK;
}

 
static bool nvme_hw_start(nvme_controller_t *ctrl) {
    nvme_queue_reset_ring(&ctrl->admin);
    for (uint32_t i = 0; i < ctrl->n_io_alloc; i++) {
        nvme_queue_reset_ring(&ctrl->io[i]);
        ctrl->io[i].active = 0;
    }
    ctrl->n_io = 0;
    ctrl->aer_pending = 0;

    ctrl->admin.iv = 0;
    ctrl->admin.vector = ctrl->irq_mode != NVME_IRQ_POLL ? ctrl->iv_vector[0] : 0;

    nvme_mmio_write32(ctrl, NVME_REG_AQA,
                      ((uint32_t)(ctrl->admin_depth - 1u) << 16)
                    | (uint32_t)(ctrl->admin_depth - 1u));
    nvme_mmio_write64(ctrl, NVME_REG_ASQ, ctrl->admin.sq_phys);
    nvme_mmio_write64(ctrl, NVME_REG_ACQ, ctrl->admin.cq_phys);

     
    if (ctrl->irq_mode == NVME_IRQ_MSI)
        nvme_mmio_write32(ctrl, NVME_REG_INTMC, 0xFFFFFFFFu);
    else if (ctrl->irq_mode == NVME_IRQ_POLL)
        nvme_mmio_write32(ctrl, NVME_REG_INTMS, 0xFFFFFFFFu);

    nvme_mmio_write32(ctrl, NVME_REG_CC,
                      NVME_CC_EN | NVME_CC_IOSQES | NVME_CC_IOCQES);
    if (!nvme_wait_ready(ctrl, true)) {
        DBG_MSG("NVME", "controller did not become ready");
        return false;
    }
    ctrl->admin.active = 1;

    uint32_t want = ctrl->n_io_alloc;
    uint32_t result = 0;
    if (!nvme_admin_cmd(ctrl, NVME_ADMIN_SET_FEAT, 0, 0,
                        NVME_FEAT_NUM_QUEUES,
                        ((want - 1u) << 16) | (want - 1u), &result)) {
        DBG_MSG("NVME", "Set Features (Number of Queues) failed");
        return false;
    }
    uint32_t nsq = (result & 0xFFFFu) + 1u;
    uint32_t ncq = (result >> 16) + 1u;
    uint32_t n = want;
    if (nsq < n) n = nsq;
    if (ncq < n) n = ncq;

    bool irq = ctrl->irq_mode != NVME_IRQ_POLL;
    for (uint32_t i = 0; i < n; i++) {
        nvme_queue_t *q = &ctrl->io[i];
        q->iv = nvme_queue_iv(ctrl, q->qid);
        q->vector = irq ? ctrl->iv_vector[q->iv] : 0;

        uint32_t cq_flags = 1u | (irq ? (1u << 1) : 0u)
                          | ((uint32_t)q->iv << 16);
        if (!nvme_admin_cmd(ctrl, NVME_ADMIN_CREATE_CQ, 0, q->cq_phys,
                (uint32_t)q->qid | ((uint32_t)(q->depth - 1u) << 16),
                cq_flags, NULL)) {
            DBG_VAL("NVME", "Create I/O CQ failed, qid", q->qid);
            break;
        }
        if (!nvme_admin_cmd(ctrl, NVME_ADMIN_CREATE_SQ, 0, q->sq_phys,
                (uint32_t)q->qid | ((uint32_t)(q->depth - 1u) << 16),
                1u | ((uint32_t)q->qid << 16), NULL)) {
            DBG_VAL("NVME", "Create I/O SQ failed, qid", q->qid);
            nvme_admin_cmd(ctrl, NVME_ADMIN_DELETE_CQ, 0, 0, q->qid, 0, NULL);
            break;
        }
        q->active = 1;
        ctrl->n_io++;
    }
    if (!ctrl->n_io)
        return false;

     
    nvme_admin_cmd(ctrl, NVME_ADMIN_SET_FEAT, 0, 0,
                   NVME_FEAT_ASYNC_EVENT, 0xFFu, NULL);
    nvme_aer_start(ctrl);
    return true;
}

static bool nvme_controller_recover(nvme_controller_t *ctrl) {
    if (ctrl->state == NVME_STATE_OFF || ctrl->state == NVME_STATE_INIT)
        return false;

    if (__atomic_exchange_n(&ctrl->recovering, 1u, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&ctrl->recovering, __ATOMIC_ACQUIRE))
            nvme_relax();
        return ctrl->state == NVME_STATE_READY;
    }

    DBG_MSG("NVME", "resetting controller");
    ctrl->resets++;
    ctrl->state = NVME_STATE_RESETTING;

     
    nvme_queue_deactivate(&ctrl->admin);
    for (uint32_t i = 0; i < ctrl->n_io_alloc; i++)
        nvme_queue_deactivate(&ctrl->io[i]);

    nvme_mmio_write32(ctrl, NVME_REG_CC, 0);
    bool ok = nvme_wait_ready(ctrl, false);
    if (!ok)
        DBG_MSG("NVME", "controller did not stop during reset");

    nvme_queue_abort_requests(&ctrl->admin);
    for (uint32_t i = 0; i < ctrl->n_io_alloc; i++)
        nvme_queue_abort_requests(&ctrl->io[i]);

    if (ok)
        ok = nvme_hw_start(ctrl);
    ctrl->state = ok ? NVME_STATE_READY : NVME_STATE_FAILED;
    if (!ok)
        DBG_MSG("NVME", "controller recovery failed");
    __atomic_store_n(&ctrl->recovering, 0u, __ATOMIC_RELEASE);
    return ok;
}

static void nvme_controller_teardown(nvme_controller_t *ctrl) {
    nvme_queue_deactivate(&ctrl->admin);
    for (uint32_t i = 0; i < NVME_MAX_IO_QUEUES; i++)
        nvme_queue_deactivate(&ctrl->io[i]);

    bool stopped = true;
    if (nvme_mmio_read32(ctrl, NVME_REG_CC) & NVME_CC_EN) {
        nvme_mmio_write32(ctrl, NVME_REG_CC, 0);
        stopped = nvme_wait_ready(ctrl, false);
    }
    nvme_irq_teardown(ctrl);
    ctrl->state = NVME_STATE_OFF;
    if (stopped)
        nvme_controller_free_memory(ctrl);   
}

static void nvme_controller_shutdown(nvme_controller_t *ctrl) {
    if (ctrl->state != NVME_STATE_READY)
        return;
    ctrl->state = NVME_STATE_RESETTING;

    nvme_sqe_t flush;
    nvme_zero(&flush, sizeof(flush));
    flush.opcode = NVME_NVM_FLUSH;
    flush.nsid = 0xFFFFFFFFu;
    nvme_cmd_sync(ctrl, &ctrl->io[0], &flush, NVME_IO_TIMEOUT_MS, NULL);

    for (uint32_t i = 0; i < ctrl->n_io; i++) {
        nvme_admin_cmd(ctrl, NVME_ADMIN_DELETE_SQ, 0, 0,
                       ctrl->io[i].qid, 0, NULL);
        nvme_admin_cmd(ctrl, NVME_ADMIN_DELETE_CQ, 0, 0,
                       ctrl->io[i].qid, 0, NULL);
    }

    uint32_t cc = nvme_mmio_read32(ctrl, NVME_REG_CC);
    nvme_mmio_write32(ctrl, NVME_REG_CC, cc | NVME_CC_SHN_NORMAL);
    nvme_deadline_t dl;
    nvme_deadline_start(&dl, nvme_ctrl_timeout_ms(ctrl));
    while ((nvme_mmio_read32(ctrl, NVME_REG_CSTS) & NVME_CSTS_SHST_MASK)
           != NVME_CSTS_SHST_DONE) {
        if (nvme_deadline_expired(&dl)) {
            DBG_MSG("NVME", "shutdown notification timed out");
            break;
        }
        nvme_relax();
    }
    nvme_controller_teardown(ctrl);
}

 
 
 

static bool nvme_map_page_phys(const nvme_dma_map_t *m, uint32_t index,
                               uint64_t *out) {
    if (index == 0) {
        if (!m->translate) {
            *out = m->base;
            return true;
        }
        if (!g_nvme_virt_to_phys)
            return false;
        uint64_t p = g_nvme_virt_to_phys((const void *)(uintptr_t)m->base);
        if (p == (uint64_t)-1 || (p & 0xFFFu) != (m->base & 0xFFFu))
            return false;
        *out = p;
        return true;
    }
    uint64_t page = (m->base & ~0xFFFULL) + (uint64_t)index * NVME_PAGE_SIZE;
    if (!m->translate) {
        *out = page;
        return true;
    }
    if (!g_nvme_virt_to_phys)
        return false;
    uint64_t p = g_nvme_virt_to_phys((const void *)(uintptr_t)page);
    if (p == (uint64_t)-1 || (p & 0xFFFu) != 0)
        return false;
    *out = p;
    return true;
}

 





static bool nvme_build_prps(const nvme_dma_slot_t *slot,
                            const nvme_dma_map_t *m, uint32_t bytes,
                            uint64_t *prp1, uint64_t *prp2) {
    uint32_t offset = (uint32_t)(m->base & 0xFFFu);
    uint32_t pages = (offset + bytes + NVME_PAGE_SIZE - 1u) / NVME_PAGE_SIZE;

    uint64_t p0;
    if (!nvme_map_page_phys(m, 0, &p0))
        return false;
    *prp1 = p0;
    *prp2 = 0;
    if (pages <= 1u)
        return true;
    if (pages == 2u)
        return nvme_map_page_phys(m, 1, prp2);

    uint32_t entries = pages - 1u;
    if (entries > NVME_PRP_LIST_PAGES * 511u)
        return false;

    uint32_t next_page = 1;
    uint32_t list_page = 0;
    while (entries) {
        uint64_t *list = slot->prp_list + (uint64_t)list_page * 512u;
        uint32_t take = entries > 512u ? 511u : entries;
        for (uint32_t i = 0; i < take; i++) {
            uint64_t p;
            if (!nvme_map_page_phys(m, next_page++, &p))
                return false;
            list[i] = p;
        }
        entries -= take;
        if (entries) {
            list_page++;
            list[511] = slot->prp_phys + (uint64_t)list_page * NVME_PAGE_SIZE;
        }
    }
    *prp2 = slot->prp_phys;
    return true;
}

 


void nvme_set_virt_to_phys(uint64_t (*fn)(const void *virt)) {
    g_nvme_virt_to_phys = fn;
}

static bool nvme_try_map_buffer(const void *buf, uint32_t bytes,
                                nvme_dma_map_t *m) {
    uint64_t va = (uint64_t)(uintptr_t)buf;
    if (va & 3u)
        return false;                     
    if (va >= NVME_DIRECT_MAP_OFFSET
        && va - NVME_DIRECT_MAP_OFFSET + bytes <= NVME_DIRECT_MAP_SIZE) {
        m->translate = false;
        m->base = va - NVME_DIRECT_MAP_OFFSET;
        return true;
    }
    if (g_nvme_virt_to_phys) {
        m->translate = true;
        m->base = va;
        return true;
    }
    return false;
}

static nvme_dma_slot_t *nvme_slot_acquire(nvme_controller_t *ctrl) {
    for (uint32_t i = 0; i < ctrl->nslots; i++) {
        nvme_dma_slot_t *s = &ctrl->slots[i];
        if (!__atomic_exchange_n(&s->in_use, 1u, __ATOMIC_ACQUIRE))
            return s;
    }
    return NULL;
}

static void nvme_slot_release(nvme_dma_slot_t *slot) {
    __atomic_store_n(&slot->in_use, 0u, __ATOMIC_RELEASE);
}

static bool nvme_slot_ensure_bounce(nvme_controller_t *ctrl,
                                    nvme_dma_slot_t *slot) {
    if (slot->bounce)
        return true;
    uint32_t pages = ctrl->max_xfer_bytes / NVME_PAGE_SIZE;
    while (pages) {
        uint64_t phys;
        void *mem = nvme_dma_alloc((uint64_t)pages * NVME_PAGE_SIZE, &phys);
        if (mem) {
            slot->bounce = (uint8_t *)mem;
            slot->bounce_phys = phys;
            slot->bounce_bytes = pages * NVME_PAGE_SIZE;
            return true;
        }
        pages >>= 1;                      
    }
    return false;
}

 
 
 

typedef struct {
    nvme_dma_slot_t *slot;
    nvme_queue_t *queue;
    uint16_t cid;
    uint32_t bytes;
    uint8_t *user;
    bool bounced;
} nvme_chunk_t;

static nvme_queue_t *nvme_pick_io_queue(nvme_controller_t *ctrl) {
    uint32_t n = ctrl->n_io ? ctrl->n_io : 1u;
    uint32_t idx = __atomic_fetch_add(&ctrl->io_rr, 1u, __ATOMIC_RELAXED) % n;
    return &ctrl->io[idx];
}

static uint32_t nvme_issue_chunk(nvme_namespace_t *ns, nvme_chunk_t *ck,
                                 uint64_t lba, uint32_t *sectors,
                                 uint8_t *user, bool write) {
    nvme_controller_t *ctrl = ns->controller;
    uint32_t ss = ns->blkdev.sector_size;
    uint32_t n = *sectors;
    uint32_t max_sectors = ctrl->max_xfer_bytes / ss;
    if (n > max_sectors)
        n = max_sectors;
    uint32_t bytes = n * ss;

    nvme_dma_map_t map;
    bool bounced = false;
    if (!nvme_try_map_buffer(user, bytes, &map)) {
        if (!nvme_slot_ensure_bounce(ctrl, ck->slot))
            return NVME_SW_NOMEM;
        if (bytes > ck->slot->bounce_bytes) {
            n = ck->slot->bounce_bytes / ss;
            bytes = n * ss;
        }
        map.translate = false;
        map.base = ck->slot->bounce_phys;
        bounced = true;
        if (write)
            nvme_copy(ck->slot->bounce, user, bytes);
    }

    nvme_sqe_t sqe;
    nvme_zero(&sqe, sizeof(sqe));
    sqe.opcode = write ? NVME_NVM_WRITE : NVME_NVM_READ;
    sqe.nsid = ns->nsid;
    if (!nvme_build_prps(ck->slot, &map, bytes, &sqe.prp1, &sqe.prp2))
        return NVME_SW_BADBUF;
    sqe.cdw[0] = (uint32_t)lba;
    sqe.cdw[1] = (uint32_t)(lba >> 32);
    sqe.cdw[2] = n - 1u;

    ck->queue = nvme_pick_io_queue(ctrl);
    uint32_t st = nvme_submit_async(ctrl, ck->queue, &sqe,
                                    NVME_IO_TIMEOUT_MS, &ck->cid);
    if (st)
        return st;
    ck->bytes = bytes;
    ck->user = user;
    ck->bounced = bounced;
    ctrl->io_cmds++;
    *sectors = n;
    return 0;
}

 

static uint32_t nvme_rw_once(nvme_namespace_t *ns, uint64_t lba,
                             uint32_t count, uint8_t *user, bool write) {
    nvme_controller_t *ctrl = ns->controller;
    uint32_t ss = ns->blkdev.sector_size;
    nvme_chunk_t win[NVME_DMA_SLOTS];
    uint32_t head = 0, active = 0;
    uint32_t error = 0;
    bool reset_done = false;

    while (count || active) {
        while (count && !error && active < ctrl->nslots) {
            if (ctrl->state != NVME_STATE_READY) {
                error = NVME_SW_NOTREADY;
                break;
            }
            nvme_dma_slot_t *slot = nvme_slot_acquire(ctrl);
            if (!slot)
                break;
            nvme_chunk_t *ck = &win[(head + active) % NVME_DMA_SLOTS];
            ck->slot = slot;
            uint32_t n = count;
            uint32_t st = nvme_issue_chunk(ns, ck, lba, &n, user, write);
            if (st) {
                nvme_slot_release(slot);
                error = st;
                break;
            }
            lba += n;
            count -= n;
            user += (uint64_t)n * ss;
            active++;
        }

        if (!active) {
            if (error || !count)
                break;
            if (ctrl->state == NVME_STATE_FAILED) {
                error = NVME_SW_NOTREADY;
                break;
            }
            nvme_relax();                 
            continue;
        }

        nvme_chunk_t *ck = &win[head];
        uint32_t st = nvme_wait_cid(ctrl, ck->queue, ck->cid,
                                    NVME_IO_TIMEOUT_MS, NULL);
        bool leak_slot = false;
        if (st == NVME_SW_TIMEOUT) {
             
            if (!reset_done) {
                reset_done = true;
                nvme_controller_recover(ctrl);
            }
            leak_slot = ctrl->state != NVME_STATE_READY;
        }
        if (!st && !write && ck->bounced)
            nvme_copy(ck->user, ck->slot->bounce, ck->bytes);
        if (st && !error)
            error = st;
        if (!leak_slot)
            nvme_slot_release(ck->slot);
        head = (head + 1u) % NVME_DMA_SLOTS;
        active--;
    }
    return error;
}

static bool nvme_status_retryable(uint32_t st) {
    if (st == NVME_SW_TIMEOUT || st == NVME_SW_ABORT
        || st == NVME_SW_NOTREADY)
        return true;
    if (st & NVME_SW_FLAG)
        return false;
    if (st & 0x4000u)                     
        return false;
    uint32_t sct = (st >> 8) & 7u;
    return sct == 0 || sct == 3;          
}

static void nvme_service_events(nvme_controller_t *ctrl) {
     
    if (ctrl->admin.active && nvme_cq_peek(&ctrl->admin))
        nvme_queue_reap(ctrl, &ctrl->admin);
    if (!__atomic_load_n(&ctrl->aer_pending, __ATOMIC_ACQUIRE))
        return;
    if (__atomic_exchange_n(&ctrl->aer_servicing, 1u, __ATOMIC_ACQUIRE))
        return;
    if (__atomic_exchange_n(&ctrl->aer_pending, 0u, __ATOMIC_ACQ_REL)) {
        uint32_t ev = ctrl->aer_event;
        uint32_t type = ev & 7u;
        uint32_t info = (ev >> 8) & 0xFFu;
        uint32_t lid = (ev >> 16) & 0xFFu;
        DBG_VAL("NVME", "async event type", type);
        DBG_VAL("NVME", "async event info", info);
        if (lid == 0x01u || lid == 0x02u || lid == 0x03u) {
             
            if (nvme_admin_cmd(ctrl, NVME_ADMIN_GET_LOG, 0xFFFFFFFFu,
                               ctrl->log_phys, lid | (127u << 16), 0, NULL)
                && lid == 0x02u && ctrl->logbuf[0]) {
                DBG_VAL("NVME", "SMART critical warning", ctrl->logbuf[0]);
            }
        }
    }
    __atomic_store_n(&ctrl->aer_servicing, 0u, __ATOMIC_RELEASE);
}

static int nvme_ns_flush(nvme_namespace_t *ns) {
    nvme_controller_t *ctrl = ns->controller;
    nvme_sqe_t sqe;
    nvme_zero(&sqe, sizeof(sqe));
    sqe.opcode = NVME_NVM_FLUSH;
    sqe.nsid = ns->nsid;
    uint32_t st = nvme_cmd_sync(ctrl, nvme_pick_io_queue(ctrl), &sqe,
                                NVME_IO_TIMEOUT_MS, NULL);
    if (st == NVME_SW_TIMEOUT)
        nvme_controller_recover(ctrl);
    return st ? VFS_ERR_IO : VFS_OK;
}

static int nvme_rw(blkdev_t *dev, uint64_t lba, uint32_t count,
                   void *buffer, bool write) {
    nvme_namespace_t *ns = (nvme_namespace_t *)dev->priv;
    nvme_controller_t *ctrl = ns->controller;

    if (count == 0)
        return VFS_OK;
    if (lba > dev->sector_count || count > dev->sector_count - lba)
        return VFS_ERR_OVERFLOW;
    if (!buffer)
        return VFS_ERR_IO;
    if (ctrl->state == NVME_STATE_OFF || ctrl->state == NVME_STATE_INIT)
        return VFS_ERR_IO;

    nvme_service_events(ctrl);

    uint32_t st = 0;
    for (uint32_t attempt = 0; attempt < NVME_MAX_RETRIES; attempt++) {
        if (ctrl->state != NVME_STATE_READY && !nvme_controller_recover(ctrl)) {
            st = NVME_SW_NOTREADY;
            break;
        }
        st = nvme_rw_once(ns, lba, count, (uint8_t *)buffer, write);
        if (!st || !nvme_status_retryable(st))
            break;
        DBG_VAL("NVME", "I/O failed, retrying, status", st);
    }
    if (st) {
        ctrl->io_errors++;
        DBG_VAL("NVME", "I/O failed, status", st);
        return VFS_ERR_IO;
    }

     
    if (write && ctrl->vwc)
        return nvme_ns_flush(ns);
    return VFS_OK;
}

static int nvme_blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count,
                            void *buffer) {
    return nvme_rw(dev, lba, count, buffer, false);
}

static int nvme_blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count,
                             const void *buffer) {
    if (dev->readonly)
        return VFS_ERR_ROFS;
    return nvme_rw(dev, lba, count, (void *)buffer, true);
}

static void nvme_blkdev_destroy(blkdev_t *dev) {
    (void)dev;
}

static blkdev_ops_t g_nvme_blkdev_ops = {
    .read_sectors = nvme_blkdev_read,
    .write_sectors = nvme_blkdev_write,
    .destroy = nvme_blkdev_destroy,
};

static nvme_namespace_t *nvme_ns_from_dev(blkdev_t *dev) {
    if (!dev || dev->ops != &g_nvme_blkdev_ops)
        return NULL;
    return (nvme_namespace_t *)dev->priv;
}

 

int nvme_flush_drive(blkdev_t *dev) {
    nvme_namespace_t *ns = nvme_ns_from_dev(dev);
    if (!ns || ns->controller->state != NVME_STATE_READY)
        return VFS_ERR_IO;
    return nvme_ns_flush(ns);
}

 
int nvme_trim_drive(blkdev_t *dev, uint64_t lba, uint64_t count) {
    nvme_namespace_t *ns = nvme_ns_from_dev(dev);
    if (!ns)
        return VFS_ERR_IO;
    nvme_controller_t *ctrl = ns->controller;
    if (ctrl->state != NVME_STATE_READY || !(ctrl->oncs & NVME_ONCS_DSM))
        return VFS_ERR_IO;
    if (lba > dev->sector_count || count > dev->sector_count - lba)
        return VFS_ERR_OVERFLOW;

    nvme_dma_slot_t *slot;
    while (!(slot = nvme_slot_acquire(ctrl))) {
        if (ctrl->state != NVME_STATE_READY)
            return VFS_ERR_IO;
        nvme_relax();
    }
    int rc = VFS_OK;
    if (!nvme_slot_ensure_bounce(ctrl, slot)) {
        nvme_slot_release(slot);
        return VFS_ERR_IO;
    }
    while (count && rc == VFS_OK) {
        uint32_t n = count > 65536u ? 65536u : (uint32_t)count;
        uint32_t *range = (uint32_t *)slot->bounce;
        range[0] = 0;                     
        range[1] = n;                     
        range[2] = (uint32_t)lba;
        range[3] = (uint32_t)(lba >> 32);

        nvme_sqe_t sqe;
        nvme_zero(&sqe, sizeof(sqe));
        sqe.opcode = NVME_NVM_DSM;
        sqe.nsid = ns->nsid;
        sqe.prp1 = slot->bounce_phys;
        sqe.cdw[0] = 0;                   
        sqe.cdw[1] = 1u << 2;             
        uint32_t st = nvme_cmd_sync(ctrl, nvme_pick_io_queue(ctrl), &sqe,
                                    NVME_IO_TIMEOUT_MS, NULL);
        if (st == NVME_SW_TIMEOUT) {
            nvme_controller_recover(ctrl);
            if (ctrl->state != NVME_STATE_READY)
                return VFS_ERR_IO;        
        }
        if (st)
            rc = VFS_ERR_IO;
        lba += n;
        count -= n;
    }
    nvme_slot_release(slot);
    return rc;
}

int nvme_write_zeroes_drive(blkdev_t *dev, uint64_t lba, uint64_t count) {
    nvme_namespace_t *ns = nvme_ns_from_dev(dev);
    if (!ns)
        return VFS_ERR_IO;
    nvme_controller_t *ctrl = ns->controller;
    if (ctrl->state != NVME_STATE_READY
        || !(ctrl->oncs & NVME_ONCS_WRITE_ZEROES))
        return VFS_ERR_IO;
    if (dev->readonly)
        return VFS_ERR_ROFS;
    if (lba > dev->sector_count || count > dev->sector_count - lba)
        return VFS_ERR_OVERFLOW;

    while (count) {
        uint32_t n = count > 2048u ? 2048u : (uint32_t)count;
        nvme_sqe_t sqe;
        nvme_zero(&sqe, sizeof(sqe));
        sqe.opcode = NVME_NVM_WRITE_ZEROES;
        sqe.nsid = ns->nsid;
        sqe.cdw[0] = (uint32_t)lba;
        sqe.cdw[1] = (uint32_t)(lba >> 32);
        sqe.cdw[2] = n - 1u;
        uint32_t st = nvme_cmd_sync(ctrl, nvme_pick_io_queue(ctrl), &sqe,
                                    NVME_IO_TIMEOUT_MS, NULL);
        if (st == NVME_SW_TIMEOUT)
            nvme_controller_recover(ctrl);
        if (st)
            return VFS_ERR_IO;
        lba += n;
        count -= n;
    }
    return VFS_OK;
}

 
 
 

static void nvme_copy_ident_str(char *dst, const uint8_t *src, uint32_t len) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; i++)
        dst[n++] = (src[i] >= 0x20u && src[i] < 0x7Fu) ? (char)src[i] : ' ';
    while (n && dst[n - 1] == ' ')
        n--;
    dst[n] = '\0';
}

static bool nvme_identify_controller(nvme_controller_t *ctrl) {
    nvme_zero(ctrl->ident, NVME_PAGE_SIZE);
    if (!nvme_admin_cmd(ctrl, NVME_ADMIN_IDENTIFY, 0, ctrl->ident_phys,
                        NVME_IDENTIFY_CONTROLLER, 0, NULL)) {
        DBG_MSG("NVME", "Identify Controller failed");
        return false;
    }
    const uint8_t *id = ctrl->ident;

    nvme_copy_ident_str(ctrl->serial, id + 4, 20);
    nvme_copy_ident_str(ctrl->model, id + 24, 40);
    nvme_copy_ident_str(ctrl->fw, id + 64, 8);

    uint8_t mdts = id[77];
    uint32_t pages = NVME_MAX_XFER_PAGES;
    if (mdts && mdts < 16u && (1u << mdts) < pages)
        pages = 1u << mdts;
    ctrl->max_xfer_bytes = pages * NVME_PAGE_SIZE;

    ctrl->aerl = id[259];
    ctrl->nn = (uint32_t)id[516] | ((uint32_t)id[517] << 8)
             | ((uint32_t)id[518] << 16) | ((uint32_t)id[519] << 24);
    ctrl->oncs = (uint16_t)(id[520] | (id[521] << 8));
    ctrl->vwc = (id[525] & 1u) != 0;

     
    uint8_t sqes = id[512], cqes = id[513];
    if ((sqes & 0xFu) > 6u || (sqes >> 4) < 6u
        || (cqes & 0xFu) > 4u || (cqes >> 4) < 4u) {
        DBG_MSG("NVME", "controller does not support 64/16 byte entries");
        return false;
    }

    dbg_puts("[NVME] model: ");
    dbg_puts(ctrl->model);
    dbg_puts("  serial: ");
    dbg_puts(ctrl->serial);
    dbg_puts("  fw: ");
    dbg_puts(ctrl->fw);
    dbg_puts("\n");
    DBG_VAL("NVME", "max transfer bytes", ctrl->max_xfer_bytes);
    DBG_VAL("NVME", "volatile write cache", ctrl->vwc ? 1 : 0);
    DBG_VAL("NVME", "I/O queues", ctrl->n_io);
    return true;
}

static uint32_t nvme_put_u32(char *dst, uint32_t pos, uint32_t value) {
    char tmp[10];
    uint32_t n = 0;
    do {
        tmp[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < sizeof(tmp));
    while (n)
        dst[pos++] = tmp[--n];
    return pos;
}

static bool nvme_namespace_add(nvme_controller_t *ctrl, uint32_t nsid) {
    if (g_nvme_namespace_count >= NVME_MAX_NAMESPACES)
        return false;

    nvme_zero(ctrl->ident, NVME_PAGE_SIZE);
    if (!nvme_admin_cmd(ctrl, NVME_ADMIN_IDENTIFY, nsid, ctrl->ident_phys,
                        NVME_IDENTIFY_NAMESPACE, 0, NULL)) {
        DBG_VAL("NVME", "Identify Namespace failed, NSID", nsid);
        return false;
    }

    uint64_t sector_count = 0;
    for (uint32_t i = 0; i < 8; i++)
        sector_count |= (uint64_t)ctrl->ident[i] << (8u * i);
    uint8_t format_index = ctrl->ident[26] & 0x0Fu;
    uint32_t format_offset = 128u + (uint32_t)format_index * 4u;
    uint16_t metadata_size =
        (uint16_t)ctrl->ident[format_offset]
        | ((uint16_t)ctrl->ident[format_offset + 1] << 8);
    uint8_t lbads = ctrl->ident[format_offset + 2];
    if (!sector_count || metadata_size != 0 || lbads < 9 || lbads > 12) {
        DBG_VAL("NVME", "unsupported namespace LBA format, NSID", nsid);
        return false;
    }

    nvme_namespace_t *ns = &g_nvme_namespaces[g_nvme_namespace_count];
    nvme_zero(ns, sizeof(*ns));
    ns->controller = ctrl;
    ns->nsid = nsid;
    ns->blkdev.sector_size = 1u << lbads;
    ns->blkdev.sector_count = sector_count;
    ns->blkdev.priv = ns;
    ns->blkdev.readonly = false;

    char name[40];
    uint32_t pos = 0;
    name[pos++] = 'n'; name[pos++] = 'v'; name[pos++] = 'm'; name[pos++] = 'e';
    pos = nvme_put_u32(name, pos, ctrl->index);
    name[pos++] = 'n';
    pos = nvme_put_u32(name, pos, nsid);
    name[pos] = '\0';
    nvme_str_copy(ns->blkdev.name, name, (int)sizeof(ns->blkdev.name));

    ns->blkdev.ops = &g_nvme_blkdev_ops;
    int registered = blkdev_register(&ns->blkdev);
    if (registered < 0) {
        DBG_VAL("NVME", "blkdev_register failed", (uint64_t)(int64_t)registered);
        return false;
    }

    g_nvme_namespace_count++;
    dbg_puts("[NVME] registered ");
    dbg_puts(ns->blkdev.name);
    dbg_puts("\n");
    DBG_VAL("NVME", "namespace sectors", sector_count);
    DBG_VAL("NVME", "namespace sector size", ns->blkdev.sector_size);
    return true;
}

static bool nvme_probe_controller(uint8_t bus, uint8_t dev, uint8_t fn,
                                  uint32_t pci_id) {
    if (g_nvme_controller_count >= NVME_MAX_CONTROLLERS)
        return false;

    uint64_t mmio_phys;
    if (!nvme_pci_bar_phys(bus, dev, fn, 0, &mmio_phys)) {
        DBG_MSG("NVME", "BAR0 is not a usable memory BAR; skipping controller");
        return false;
    }
    if (mmio_phys >= NVME_DMA_MAX_PHYS) {
        DBG_MSG("NVME", "BAR0 is outside the kernel direct map");
        return false;
    }

    DBG_VAL("NVME", "PCI controller ID", pci_id);
    DBG_VAL("NVME", "BAR0 physical", mmio_phys);

    nvme_controller_t *ctrl = &g_nvme_controllers[g_nvme_controller_count];
    nvme_zero(ctrl, sizeof(*ctrl));
    ctrl->bus = bus;
    ctrl->dev = dev;
    ctrl->fn = fn;
    ctrl->index = (uint32_t)g_nvme_controller_count;
    ctrl->mmio_phys = mmio_phys;
    ctrl->state = NVME_STATE_INIT;

     
    uint16_t cmd = nvme_pci_read16(bus, dev, fn, 0x04);
    nvme_pci_write16(bus, dev, fn, 0x04, (uint16_t)(cmd | 0x06u));

    ctrl->cap = nvme_mmio_read64(ctrl, NVME_REG_CAP);
    uint32_t mps_min = (uint32_t)((ctrl->cap >> 48) & 0xFu);
    uint32_t dstrd = (uint32_t)((ctrl->cap >> 32) & 0xFu);
    bool nvm_cmd_set = (ctrl->cap >> 37) & 1u;
    if (mps_min != 0 || !nvm_cmd_set || (ctrl->cap & 0xFFFFu) < 1u) {
        DBG_MSG("NVME", "unsupported CAP (page size, NVM command set, MQES)");
        ctrl->state = NVME_STATE_OFF;
        return false;
    }
    ctrl->doorbell_stride = 4u << dstrd;
    ctrl->version = nvme_mmio_read32(ctrl, NVME_REG_VS);
    DBG_VAL("NVME", "controller version", ctrl->version);
    DBG_VAL("NVME", "controller CAP", ctrl->cap);

    uint32_t cc = nvme_mmio_read32(ctrl, NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_mmio_write32(ctrl, NVME_REG_CC, cc & ~NVME_CC_EN);
        if (!nvme_wait_ready(ctrl, false)) {
            DBG_MSG("NVME", "controller did not stop");
            ctrl->state = NVME_STATE_OFF;
            return false;
        }
    }

    nvme_irq_setup(ctrl);
    if (!nvme_controller_alloc(ctrl)) {
        DBG_MSG("NVME", "DMA allocation failed");
        nvme_controller_teardown(ctrl);
        return false;
    }
     
    ctrl->max_xfer_bytes = NVME_PAGE_SIZE;

    if (!nvme_hw_start(ctrl) || !nvme_identify_controller(ctrl)) {
        nvme_controller_teardown(ctrl);
        return false;
    }

     
    uint32_t active_ns[NVME_MAX_NAMESPACES];
    uint32_t active_count = 0;
    nvme_zero(ctrl->ident, NVME_PAGE_SIZE);
    if (nvme_admin_cmd(ctrl, NVME_ADMIN_IDENTIFY, 0, ctrl->ident_phys,
                       NVME_IDENTIFY_ACTIVE_NS, 0, NULL)) {
        for (uint32_t i = 0; i < 1024 && active_count < NVME_MAX_NAMESPACES;
             i++) {
            const uint8_t *p = &ctrl->ident[i * 4u];
            uint32_t id = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            if (!id)
                break;
            active_ns[active_count++] = id;
        }
    } else {
         
        DBG_MSG("NVME", "no Active Namespace List; probing NSID 1..N");
        uint32_t limit = ctrl->nn < NVME_MAX_NAMESPACES
                       ? ctrl->nn : NVME_MAX_NAMESPACES;
        for (uint32_t i = 1; i <= limit; i++)
            active_ns[active_count++] = i;
    }

    uint32_t added = 0;
    for (uint32_t i = 0; i < active_count; i++) {
        if (nvme_namespace_add(ctrl, active_ns[i]))
            added++;
    }
    if (!added) {
        nvme_controller_teardown(ctrl);
        return false;
    }

    ctrl->state = NVME_STATE_READY;
    g_nvme_controller_count++;
    return true;
}

 
 
 

int nvme_init(void) {
    DBG_MSG("NVME", "=== nvme_init ===");

    g_nvme_irq_registered = false;
    uint32_t registered = 0;
    for (uint32_t i = 0; i < NVME_IRQ_VECTOR_COUNT; i++) {
        if (interrupt_register_handler((uint16_t)(NVME_IRQ_VECTOR_BASE + i),
                                       nvme_interrupt_handler) == 0)
            registered++;
        else
            g_nvme_vec_used |= 1u << i;       
    }
    g_nvme_irq_registered = registered != 0;
    if (!g_nvme_irq_registered)
        DBG_MSG("NVME", "IRQ handler registration failed; using polling");

    int found = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            bool multifunction = false;
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = nvme_pci_read32((uint8_t)bus, dev, fn, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    if (fn == 0)
                        break;
                    continue;
                }

                uint32_t class_reg =
                    nvme_pci_read32((uint8_t)bus, dev, fn, 0x08);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                uint8_t header_type =
                    (uint8_t)(nvme_pci_read32((uint8_t)bus, dev, fn, 0x0C)
                              >> 16);
                if (fn == 0)
                    multifunction = (header_type & 0x80u) != 0;

                if (class_code == 0x01 && subclass == 0x08
                    && prog_if == 0x02
                    && nvme_probe_controller((uint8_t)bus, dev, fn, id))
                    found++;

                if (fn == 0 && !multifunction)
                    break;
            }
        }
    }

    if (!found) {
        DBG_MSG("NVME", "no usable NVMe controller or namespace found");
        return VFS_ERR_NXDEV;
    }
    DBG_VAL("NVME", "registered namespaces", (uint64_t)g_nvme_namespace_count);
    DBG_MSG("NVME", "=== nvme_init done ===");
    return VFS_OK;
}

blkdev_t *nvme_find_drive(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_nvme_namespace_count; i++) {
        if (nvme_str_cmp(g_nvme_namespaces[i].blkdev.name, name) == 0)
            return &g_nvme_namespaces[i].blkdev;
    }
    return NULL;
}

 
void nvme_shutdown(void) {
    for (int i = 0; i < g_nvme_controller_count; i++)
        nvme_controller_shutdown(&g_nvme_controllers[i]);
    for (uint32_t i = 0; i < NVME_IRQ_VECTOR_COUNT; i++)
        interrupt_unregister_handler((uint16_t)(NVME_IRQ_VECTOR_BASE + i));
    g_nvme_irq_registered = false;
}

uint64_t nvme_get_interrupt_count(void) {
    return __atomic_load_n(&g_nvme_interrupt_count, __ATOMIC_RELAXED);
}

void nvme_dump_info(void) {
    for (int i = 0; i < g_nvme_controller_count; i++) {
        nvme_controller_t *c = &g_nvme_controllers[i];
        DBG_VAL("NVME", "controller", (uint64_t)i);
        DBG_VAL("NVME", "  state", c->state);
        DBG_VAL("NVME", "  irq mode (0 poll, 1 MSI, 2 MSI-X)", c->irq_mode);
        DBG_VAL("NVME", "  I/O queues", c->n_io);
        DBG_VAL("NVME", "  commands", c->io_cmds);
        DBG_VAL("NVME", "  interrupts", nvme_ctrl_irq_total(c));
        DBG_VAL("NVME", "  poll rescues", c->poll_rescues);
        DBG_VAL("NVME", "  timeouts", c->timeouts);
        DBG_VAL("NVME", "  resets", c->resets);
        DBG_VAL("NVME", "  I/O errors", c->io_errors);
    }
}
