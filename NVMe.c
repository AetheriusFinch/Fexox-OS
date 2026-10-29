/* Fexox OS Project, EULA GPLv2 */
 






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

#define NVME_DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#define NVME_DMA_MIN_PHYS      0x100000ULL
#define NVME_DMA_MAX_PHYS      0x100000000ULL
#define NVME_PAGE_SIZE         4096u
#define NVME_QUEUE_DEPTH       16u
#define NVME_MAX_CONTROLLERS   4
#define NVME_MAX_NAMESPACES    8
#define NVME_POLL_LIMIT        50000000u
#define NVME_IRQ_VECTOR        0x50u

#define NVME_REG_CAP           0x0000
#define NVME_REG_VS            0x0008
#define NVME_REG_CC            0x0014
#define NVME_REG_CSTS          0x001C
#define NVME_REG_AQA           0x0024
#define NVME_REG_ASQ           0x0028
#define NVME_REG_ACQ           0x0030
#define NVME_REG_DOORBELL      0x1000

#define NVME_CC_EN             (1u << 0)
#define NVME_CSTS_RDY          (1u << 0)
#define NVME_CSTS_CFS          (1u << 1)

#define NVME_ADMIN_CREATE_SQ   0x01
#define NVME_ADMIN_IDENTIFY    0x06
#define NVME_ADMIN_CREATE_CQ   0x05
#define NVME_NVM_FLUSH          0x00
#define NVME_NVM_WRITE          0x01
#define NVME_NVM_READ           0x02

#define NVME_IDENTIFY_NAMESPACE 0
#define NVME_IDENTIFY_CONTROLLER 1
#define NVME_IDENTIFY_ACTIVE_NS 2

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

typedef struct {
    nvme_sqe_t *sq;
    nvme_cqe_t *cq;
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint16_t qid;
    uint16_t depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t next_cid;
    uint8_t cq_phase;
} nvme_queue_t;

typedef struct {
    uint64_t mmio_phys;
    uint64_t cap;
    uint32_t doorbell_stride;
    nvme_queue_t admin;
    nvme_queue_t io;
    uint8_t *identify_page;
    uint64_t identify_phys;
    uint8_t *data_page;
    uint64_t data_phys;
    bool msi_enabled;
    bool enabled;
    bool ready;
} nvme_controller_t;

typedef struct {
    nvme_controller_t *controller;
    uint32_t nsid;
    blkdev_t blkdev;
} nvme_namespace_t;

static nvme_controller_t g_nvme_controllers[NVME_MAX_CONTROLLERS];
static nvme_namespace_t g_nvme_namespaces[NVME_MAX_NAMESPACES];
static int g_nvme_controller_count;
static int g_nvme_namespace_count;
static blkdev_ops_t g_nvme_blkdev_ops;
static volatile uint64_t g_nvme_interrupt_count;
static bool g_nvme_irq_registered;

typedef void (*nvme_isr_handler_t)(uint64_t vector, uint64_t error,
                                   void *frame);
extern int interrupt_register_handler(uint16_t vector,
                                      nvme_isr_handler_t handler);
extern uint32_t interrupt_get_apic_id(void);

static void nvme_interrupt_handler(uint64_t vector, uint64_t error,
                                  void *frame) {
    (void)vector;
    (void)error;
    (void)frame;
    __atomic_fetch_add(&g_nvme_interrupt_count, 1, __ATOMIC_RELAXED);
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

static bool nvme_enable_msi(uint8_t bus, uint8_t dev, uint8_t fn) {
    if (!g_nvme_irq_registered)
        return false;

    uint8_t cap = nvme_pci_read8(bus, dev, fn, 0x34) & 0xFCu;
    uint8_t msi_cap = 0;
    uint8_t msix_cap = 0;
    for (uint32_t steps = 0; cap >= 0x40u && cap <= 0xFCu && steps < 48u;
         steps++) {
        uint8_t id = nvme_pci_read8(bus, dev, fn, cap);
        if (id == 0x05u)
            msi_cap = cap;
        else if (id == 0x11u)
            msix_cap = cap;
        uint8_t next = nvme_pci_read8(bus, dev, fn, cap + 1u) & 0xFCu;
        if (next == cap)
            break;
        cap = next;
    }

    if (!msi_cap) {
        DBG_MSG("NVME", "PCI MSI unavailable; using polling");
        return false;
    }

     

    if (msix_cap) {
        uint16_t msix_control = nvme_pci_read16(bus, dev, fn, msix_cap + 2u);
        if (msix_control & 0x8000u) {
            nvme_pci_write16(bus, dev, fn, msix_cap + 2u,
                             msix_control & ~0x8000u);
        }
    }

    uint16_t control = nvme_pci_read16(bus, dev, fn, msi_cap + 2u);
    bool address_64 = (control & (1u << 7)) != 0;
    bool per_vector_mask = (control & (1u << 8)) != 0;
    if (per_vector_mask && msi_cap > (address_64 ? 0xECu : 0xF0u)) {
        DBG_MSG("NVME", "malformed PCI MSI capability; using polling");
        return false;
    }
    nvme_pci_write16(bus, dev, fn, msi_cap + 2u, control & ~1u);

    uint32_t address = 0xFEE00000u
        | ((interrupt_get_apic_id() & 0xFFu) << 12);
    nvme_pci_write32(bus, dev, fn, msi_cap + 4u, address);
    if (address_64) {
        nvme_pci_write32(bus, dev, fn, msi_cap + 8u, 0);
        nvme_pci_write16(bus, dev, fn, msi_cap + 12u, NVME_IRQ_VECTOR);
        if (per_vector_mask)
            nvme_pci_write32(bus, dev, fn, msi_cap + 16u, 0);
    } else {
        nvme_pci_write16(bus, dev, fn, msi_cap + 8u, NVME_IRQ_VECTOR);
        if (per_vector_mask)
            nvme_pci_write32(bus, dev, fn, msi_cap + 12u, 0);
    }

    control &= (uint16_t)~((7u << 4) | 1u);
    control |= 1u;
    nvme_pci_write16(bus, dev, fn, msi_cap + 2u, control);

    if (!(nvme_pci_read16(bus, dev, fn, msi_cap + 2u) & 1u)) {
        DBG_MSG("NVME", "PCI MSI enable failed; using polling");
        return false;
    }
    return true;
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

static void *nvme_dma_alloc(uint64_t bytes, uint64_t *phys_out) {
    uint64_t pages = (bytes + NVME_PAGE_SIZE - 1) / NVME_PAGE_SIZE;
    phys_addr_t phys = pmm_alloc_frames_range(pages, 1, NVME_DMA_MIN_PHYS,
                                               NVME_DMA_MAX_PHYS);
    if (phys == (phys_addr_t)-1)
        return NULL;

    uint8_t *virt = (uint8_t *)(uintptr_t)
        (NVME_DIRECT_MAP_OFFSET + phys);
    for (uint64_t i = 0; i < pages * NVME_PAGE_SIZE; i++)
        virt[i] = 0;
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

static inline void nvme_copy(void *dst, const void *src, uint64_t bytes) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (bytes--)
        *d++ = *s++;
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

static inline uint32_t nvme_doorbell_offset(const nvme_controller_t *ctrl,
                                           uint16_t qid, bool completion) {
    uint32_t index = (uint32_t)qid * 2u + (completion ? 1u : 0u);
    return NVME_REG_DOORBELL + index * ctrl->doorbell_stride;
}

static bool nvme_wait_ready(nvme_controller_t *ctrl, bool ready) {
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; spin++) {
        uint32_t status = nvme_mmio_read32(ctrl, NVME_REG_CSTS);
        if (status & NVME_CSTS_CFS)
            return false;
        if (!!(status & NVME_CSTS_RDY) == ready)
            return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static bool nvme_submit(nvme_controller_t *ctrl, nvme_queue_t *queue,
                        const nvme_sqe_t *request) {
    if (!ctrl->ready)
        return false;

    nvme_sqe_t *slot = &queue->sq[queue->sq_tail];
    *slot = *request;
    slot->cid = queue->next_cid++;
    uint16_t cid = slot->cid;

    __asm__ volatile ("mfence" ::: "memory");
    queue->sq_tail = (uint16_t)((queue->sq_tail + 1u) % queue->depth);
    nvme_mmio_write32(ctrl, nvme_doorbell_offset(ctrl, queue->qid, false),
                      queue->sq_tail);

    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; spin++) {
        nvme_cqe_t *completion = &queue->cq[queue->cq_head];
        __asm__ volatile ("lfence" ::: "memory");
        if ((completion->status & 1u) == queue->cq_phase) {
            bool ok = completion->cid == cid
                   && (completion->status >> 1) == 0;
            queue->cq_head = (uint16_t)((queue->cq_head + 1u) % queue->depth);
            if (queue->cq_head == 0)
                queue->cq_phase ^= 1u;
            __asm__ volatile ("mfence" ::: "memory");
            nvme_mmio_write32(ctrl,
                nvme_doorbell_offset(ctrl, queue->qid, true), queue->cq_head);
            return ok;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    ctrl->ready = false;
    DBG_VAL("NVME", "command timed out, queue", queue->qid);
    return false;
}

static bool nvme_admin_command(nvme_controller_t *ctrl, uint8_t opcode,
                               uint32_t nsid, uint64_t data_phys,
                               uint32_t cdw10, uint32_t cdw11) {
    nvme_sqe_t request = {0};
    request.opcode = opcode;
    request.nsid = nsid;
    request.prp1 = data_phys;
    request.cdw[0] = cdw10;
    request.cdw[1] = cdw11;
    return nvme_submit(ctrl, &ctrl->admin, &request);
}

static bool nvme_queue_init(nvme_queue_t *queue, uint16_t qid) {
    queue->depth = NVME_QUEUE_DEPTH;
    queue->qid = qid;
    queue->cq_phase = 1;
    queue->sq = (nvme_sqe_t *)nvme_dma_alloc(NVME_PAGE_SIZE,
                                              &queue->sq_phys);
    queue->cq = (nvme_cqe_t *)nvme_dma_alloc(NVME_PAGE_SIZE,
                                              &queue->cq_phys);
    return queue->sq && queue->cq;
}

static void nvme_controller_release(nvme_controller_t *ctrl) {
    if (ctrl->enabled) {
        nvme_mmio_write32(ctrl, NVME_REG_CC, 0);
        if (!nvme_wait_ready(ctrl, false))
            return;
        ctrl->enabled = false;
        ctrl->ready = false;
    }

    nvme_dma_free(ctrl->admin.sq, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->admin.cq, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->io.sq, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->io.cq, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->identify_page, NVME_PAGE_SIZE);
    nvme_dma_free(ctrl->data_page, NVME_PAGE_SIZE);
    ctrl->admin.sq = NULL;
    ctrl->admin.cq = NULL;
    ctrl->io.sq = NULL;
    ctrl->io.cq = NULL;
    ctrl->identify_page = NULL;
    ctrl->data_page = NULL;
}

static bool nvme_controller_init(nvme_controller_t *ctrl, uint64_t mmio_phys) {
    ctrl->mmio_phys = mmio_phys;
    ctrl->cap = nvme_mmio_read64(ctrl, NVME_REG_CAP);

    uint32_t max_entries = (uint32_t)(ctrl->cap & 0xFFFFu) + 1u;
    uint32_t mps_min = (uint32_t)((ctrl->cap >> 48) & 0xFu);
    uint32_t dstrd = (uint32_t)((ctrl->cap >> 32) & 0xFu);
    if (max_entries < NVME_QUEUE_DEPTH || mps_min != 0
        || !(ctrl->cap & 1u) || dstrd > 15u) {
        DBG_MSG("NVME", "unsupported CAP (queue depth, page size, or NVM)");
        return false;
    }
    ctrl->doorbell_stride = 4u << dstrd;

    uint32_t version = nvme_mmio_read32(ctrl, NVME_REG_VS);
    DBG_VAL("NVME", "controller version", version);
    DBG_VAL("NVME", "controller CAP", ctrl->cap);

    uint32_t cc = nvme_mmio_read32(ctrl, NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_mmio_write32(ctrl, NVME_REG_CC, cc & ~NVME_CC_EN);
        if (!nvme_wait_ready(ctrl, false)) {
            DBG_MSG("NVME", "controller did not stop");
            return false;
        }
    }

    if (!nvme_queue_init(&ctrl->admin, 0)
        || !nvme_queue_init(&ctrl->io, 1)) {
        DBG_MSG("NVME", "queue DMA allocation failed");
        nvme_controller_release(ctrl);
        return false;
    }

    nvme_mmio_write32(ctrl, NVME_REG_AQA,
                      ((NVME_QUEUE_DEPTH - 1u) << 16)
                    | (NVME_QUEUE_DEPTH - 1u));
    nvme_mmio_write64(ctrl, NVME_REG_ASQ, ctrl->admin.sq_phys);
    nvme_mmio_write64(ctrl, NVME_REG_ACQ, ctrl->admin.cq_phys);

     
    nvme_mmio_write32(ctrl, NVME_REG_CC,
                      NVME_CC_EN | (6u << 16) | (4u << 20));
    ctrl->enabled = true;
    if (!nvme_wait_ready(ctrl, true)) {
        DBG_MSG("NVME", "controller did not become ready");
        nvme_controller_release(ctrl);
        return false;
    }
    ctrl->ready = true;

    if (!nvme_admin_command(ctrl, NVME_ADMIN_CREATE_CQ, 0,
            ctrl->io.cq_phys,
            1u | ((NVME_QUEUE_DEPTH - 1u) << 16),
            1u | (ctrl->msi_enabled ? 1u << 1 : 0u)
               | (ctrl->msi_enabled ? NVME_IRQ_VECTOR << 16 : 0u))) {
        DBG_MSG("NVME", "Create I/O Completion Queue failed");
        nvme_controller_release(ctrl);
        return false;
    }
    if (!nvme_admin_command(ctrl, NVME_ADMIN_CREATE_SQ, 0,
            ctrl->io.sq_phys,
            1u | ((NVME_QUEUE_DEPTH - 1u) << 16),
            1u | (1u << 16))) {
        DBG_MSG("NVME", "Create I/O Submission Queue failed");
        nvme_controller_release(ctrl);
        return false;
    }

    ctrl->identify_page = (uint8_t *)nvme_dma_alloc(
        NVME_PAGE_SIZE, &ctrl->identify_phys);
    ctrl->data_page = (uint8_t *)nvme_dma_alloc(
        NVME_PAGE_SIZE, &ctrl->data_phys);
    if (!ctrl->identify_page || !ctrl->data_page) {
        DBG_MSG("NVME", "data DMA allocation failed");
        nvme_controller_release(ctrl);
        return false;
    }

    if (!nvme_admin_command(ctrl, NVME_ADMIN_IDENTIFY, 0,
            ctrl->identify_phys, NVME_IDENTIFY_CONTROLLER, 0)) {
        DBG_MSG("NVME", "Identify Controller failed");
        nvme_controller_release(ctrl);
        return false;
    }

    return true;
}

static bool nvme_namespace_add(nvme_controller_t *ctrl, uint32_t nsid) {
    if (g_nvme_namespace_count >= NVME_MAX_NAMESPACES)
        return false;

    for (uint32_t i = 0; i < NVME_PAGE_SIZE; i++)
        ctrl->identify_page[i] = 0;
    if (!nvme_admin_command(ctrl, NVME_ADMIN_IDENTIFY, nsid,
            ctrl->identify_phys, NVME_IDENTIFY_NAMESPACE, 0)) {
        DBG_VAL("NVME", "Identify Namespace failed, NSID", nsid);
        return false;
    }

    uint64_t sector_count = *(const uint64_t *)ctrl->identify_page;
    uint8_t format_index = ctrl->identify_page[26] & 0x0Fu;
    uint32_t format_offset = 128u + (uint32_t)format_index * 4u;
    uint16_t metadata_size =
        (uint16_t)ctrl->identify_page[format_offset]
        | ((uint16_t)ctrl->identify_page[format_offset + 1] << 8);
    uint8_t lbads = ctrl->identify_page[format_offset + 2];
    if (!sector_count || metadata_size != 0 || lbads < 9 || lbads > 12) {
        DBG_VAL("NVME", "unsupported namespace LBA format, NSID", nsid);
        return false;
    }

    nvme_namespace_t *ns = &g_nvme_namespaces[g_nvme_namespace_count];
    ns->controller = ctrl;
    ns->nsid = nsid;
    ns->blkdev.sector_size = 1u << lbads;
    ns->blkdev.sector_count = sector_count;
    ns->blkdev.priv = ns;
    ns->blkdev.readonly = false;

    char name[32] = "nvme0n1";
    uint32_t controller_number = (uint32_t)g_nvme_controller_count;
    uint32_t namespace_number = nsid;
    uint32_t digits[10];
    uint32_t digit_count = 0;
    do {
        digits[digit_count++] = controller_number % 10u;
        controller_number /= 10u;
    } while (controller_number && digit_count < 10u);
    uint32_t pos = 4u;
    while (digit_count)
        name[pos++] = (char)('0' + digits[--digit_count]);
    name[pos++] = 'n';
    digit_count = 0;
    do {
        digits[digit_count++] = namespace_number % 10u;
        namespace_number /= 10u;
    } while (namespace_number && digit_count < 10u);
    while (digit_count)
        name[pos++] = (char)('0' + digits[--digit_count]);
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

static int nvme_rw(blkdev_t *dev, uint64_t lba, uint32_t count,
                   void *buffer, bool write) {
    nvme_namespace_t *ns = (nvme_namespace_t *)dev->priv;
    nvme_controller_t *ctrl = ns->controller;
    uint32_t sector_size = dev->sector_size;

    if (count == 0)
        return VFS_OK;
    if (lba > dev->sector_count || count > dev->sector_count - lba)
        return VFS_ERR_OVERFLOW;
    if (!buffer || !ctrl->ready)
        return VFS_ERR_IO;

    uint32_t max_sectors = NVME_PAGE_SIZE / sector_size;
    uint8_t *user_buffer = (uint8_t *)buffer;
    while (count) {
        uint32_t chunk = count > max_sectors ? max_sectors : count;
        uint32_t bytes = chunk * sector_size;
        if (write)
            nvme_copy(ctrl->data_page, user_buffer, bytes);
        else
            for (uint32_t i = 0; i < NVME_PAGE_SIZE; i++)
                ctrl->data_page[i] = 0;

        nvme_sqe_t request = {0};
        request.opcode = write ? NVME_NVM_WRITE : NVME_NVM_READ;
        request.nsid = ns->nsid;
        request.prp1 = ctrl->data_phys;
        request.cdw[0] = (uint32_t)lba;
        request.cdw[1] = (uint32_t)(lba >> 32);
        request.cdw[2] = chunk - 1u;
        if (!nvme_submit(ctrl, &ctrl->io, &request))
            return VFS_ERR_IO;

        if (write) {
            nvme_sqe_t flush = {0};
            flush.opcode = NVME_NVM_FLUSH;
            flush.nsid = ns->nsid;
            if (!nvme_submit(ctrl, &ctrl->io, &flush))
                return VFS_ERR_IO;
        } else {
            __asm__ volatile ("lfence" ::: "memory");
            nvme_copy(user_buffer, ctrl->data_page, bytes);
        }

        lba += chunk;
        count -= chunk;
        user_buffer += bytes;
    }
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

static bool nvme_probe_controller(uint8_t bus, uint8_t dev, uint8_t fn,
                                  uint32_t pci_id) {
    uint32_t bar0 = nvme_pci_read32(bus, dev, fn, 0x10);
    if ((bar0 & 1u) != 0) {
        DBG_MSG("NVME", "BAR0 is I/O space; skipping controller");
        return false;
    }

    uint32_t bar_type = (bar0 >> 1) & 3u;
    if (bar_type != 0u && bar_type != 2u) {
        DBG_MSG("NVME", "unsupported BAR0 memory type");
        return false;
    }

    uint64_t mmio_phys = bar0 & ~0xFULL;
    if (bar_type == 2u) {
        uint32_t bar1 = nvme_pci_read32(bus, dev, fn, 0x14);
        mmio_phys |= (uint64_t)bar1 << 32;
    }
    if (!mmio_phys || mmio_phys >= NVME_DMA_MAX_PHYS
        || g_nvme_controller_count >= NVME_MAX_CONTROLLERS) {
        if (mmio_phys >= NVME_DMA_MAX_PHYS)
            DBG_MSG("NVME", "BAR0 is outside the kernel direct map");
        return false;
    }

    DBG_VAL("NVME", "PCI controller ID", pci_id);
    DBG_VAL("NVME", "BAR0 physical", mmio_phys);

    uint32_t command = nvme_pci_read32(bus, dev, fn, 0x04);
    nvme_pci_write32(bus, dev, fn, 0x04, command | 0x06u);

    nvme_controller_t *ctrl =
        &g_nvme_controllers[g_nvme_controller_count];
    ctrl->msi_enabled = nvme_enable_msi(bus, dev, fn);
    if (!nvme_controller_init(ctrl, mmio_phys))
        return false;

    uint32_t active_ns[1024];
    for (uint32_t i = 0; i < NVME_PAGE_SIZE; i++)
        ctrl->identify_page[i] = 0;
    if (!nvme_admin_command(ctrl, NVME_ADMIN_IDENTIFY, 0,
            ctrl->identify_phys, NVME_IDENTIFY_ACTIVE_NS, 0)) {
        DBG_MSG("NVME", "Identify Active Namespace List failed");
        nvme_controller_release(ctrl);
        return false;
    }
    for (uint32_t i = 0; i < 1024; i++) {
        const uint8_t *p = &ctrl->identify_page[i * 4u];
        active_ns[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    uint32_t added = 0;
    for (uint32_t i = 0; i < 1024
         && g_nvme_namespace_count < NVME_MAX_NAMESPACES; i++) {
        if (!active_ns[i])
            break;
        if (nvme_namespace_add(ctrl, active_ns[i]))
            added++;
    }

    if (!added) {
        nvme_controller_release(ctrl);
        return false;
    }
    g_nvme_controller_count++;
    return true;
}

int nvme_init(void) {
    DBG_MSG("NVME", "=== nvme_init ===");
    g_nvme_irq_registered =
        interrupt_register_handler(NVME_IRQ_VECTOR,
                                   nvme_interrupt_handler) == 0;
    if (!g_nvme_irq_registered) {
        DBG_MSG("NVME", "NVMe IRQ handler registration failed; using polling");
    }
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
