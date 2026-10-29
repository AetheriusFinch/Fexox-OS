/* Fexox OS Project, EULA GPLv2 */
 


#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void usb_delay_ms(uint32_t ms);
typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames_range(uint64_t count, uint64_t align,
                                           uint64_t min_phys, uint64_t max_phys);
extern void usb_register_hcd(usb_hcd_t *hcd);

#define XHCI_DIRECT_MAP 0xFFFF880000000000ULL
#define XHCI_DMA_MIN    0x100000ULL
#define XHCI_DMA_MAX    0x100000000ULL
#define XHCI_PAGE_SIZE  4096u
#define XHCI_MAX_CONTROLLERS 4
#define XHCI_MAX_SLOTS 32
#define XHCI_RING_TRBS 256
#define XHCI_COMMAND_LIMIT 10000000u
#define XHCI_TRANSFER_LIMIT 20000000u
#define XHCI_PORTSC_CCS  (1u << 0)
#define XHCI_PORTSC_PED  (1u << 1)
#define XHCI_PORTSC_PR   (1u << 4)
#define XHCI_PORTSC_PLS_MASK (0xFu << 5)
#define XHCI_PORTSC_PP   (1u << 9)
#define XHCI_PORTSC_CSC  (1u << 17)
#define XHCI_PORTSC_PRC  (1u << 21)
#define XHCI_PORTSC_WPR  (1u << 31)
#define XHCI_PORTSC_WRC  (1u << 19)
#define XHCI_PORTSC_CHANGE (0x7Fu << 17)
#define XHCI_TRB_CYCLE   (1u << 0)
#define XHCI_TRB_TOGGLE  (1u << 1)
#define XHCI_TRB_CHAIN   (1u << 4)
#define XHCI_TRB_IOC     (1u << 5)
#define XHCI_TRB_IDT     (1u << 6)
#define XHCI_TRB_TYPE(n) ((uint32_t)(n) << 10)
#define XHCI_TRB_SLOT(n) ((uint32_t)(n) << 24)
#define XHCI_TRB_EP(n)   ((uint32_t)(n) << 16)
#define XHCI_TRB_DIR_IN  (1u << 16)
#define XHCI_TRB_LEN_MASK 0x1FFFFu
#define XHCI_CC_SUCCESS 1u
#define XHCI_CC_SHORT_PACKET 13u
#define XHCI_CMD_ENABLE_SLOT 9u
#define XHCI_CMD_ADDRESS_DEVICE 11u
#define XHCI_CMD_CONFIGURE_ENDPOINT 12u
#define XHCI_CMD_STOP_ENDPOINT 15u
#define XHCI_CMD_RESET_ENDPOINT 14u
#define XHCI_EVENT_TRANSFER 32u
#define XHCI_EVENT_COMMAND 33u
#define XHCI_SETUP_STAGE 2u
#define XHCI_DATA_STAGE 3u
#define XHCI_STATUS_STAGE 4u
#define XHCI_NORMAL 1u
#define XHCI_LINK 6u

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed, aligned(16))) xhci_trb_t;

typedef struct {
    uint64_t base;
    uint32_t size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct {
    xhci_trb_t *trbs;
    uint64_t phys;
    uint16_t enqueue;
    uint8_t cycle;
} xhci_ring_t;

typedef struct {
    bool used;
    uint8_t address;
    uint8_t root_port;
    uint8_t speed_id;
    uint16_t ep0_packet;
    uint32_t *input_context;
    uint64_t input_context_phys;
    uint32_t *output_context;
    uint64_t output_context_phys;
    xhci_ring_t *endpoint_rings[32];
    uint16_t endpoint_packet[32];
} xhci_slot_t;

typedef struct {
    uint64_t mmio_phys;
    uint64_t cap_length;
    uint64_t op_phys;
    uint64_t runtime_phys;
    uint64_t doorbell_phys;
    uint32_t context_words;
    uint32_t max_slots;
    uint8_t num_ports;
    uint8_t pci_bus, pci_dev, pci_fn;
    uint32_t pci_command;
    uint64_t *dcbaa;
    uint64_t dcbaa_phys;
    uint64_t *scratchpad_array;
    uint64_t scratchpad_array_phys;
    xhci_ring_t command;
    xhci_trb_t *events;
    uint64_t events_phys;
    xhci_erst_entry_t *erst;
    uint64_t erst_phys;
    uint16_t event_dequeue;
    uint8_t event_cycle;
    uint8_t *scratch;
    uint64_t scratch_phys;
    xhci_slot_t slots[XHCI_MAX_SLOTS + 1];
    uint8_t address_slot[128];
    uint64_t scratchpad_phys;
    uint32_t scratchpad_count;
    usb_hcd_t hcd;
} xhci_controller_t;

static xhci_controller_t g_xhci[XHCI_MAX_CONTROLLERS];
static int g_xhci_count;

static inline uint64_t xhci_irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void xhci_irq_restore(uint64_t flags) {
    if (flags & 0x200u)
        __asm__ volatile ("sti" ::: "memory");
}

static inline uint32_t xhci_pci_read32(uint8_t bus, uint8_t dev,
                                      uint8_t fn, uint8_t reg) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                     | (reg & 0xFCu);
    __asm__ volatile ("outl %0, %1" :: "a"(address), "Nd"((uint16_t)0xCF8));
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"((uint16_t)0xCFC));
    return value;
}

static inline uint16_t xhci_pci_read16(uint8_t bus, uint8_t dev,
                                      uint8_t fn, uint8_t reg) {
    uint32_t value = xhci_pci_read32(bus, dev, fn, reg);
    return (uint16_t)(value >> ((reg & 2u) * 8u));
}

static inline void xhci_pci_write16(uint8_t bus, uint8_t dev, uint8_t fn,
                                    uint8_t reg, uint16_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16)
                     | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                     | (reg & 0xFCu);
    __asm__ volatile ("outl %0, %1" :: "a"(address), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outw %0, %1"
                      :: "a"(value), "Nd"((uint16_t)(0xCFCu + (reg & 2u))));
}

static inline uint32_t xhci_mmio_read32(const xhci_controller_t *ctrl,
                                        uint64_t offset) {
    __asm__ volatile ("mfence" ::: "memory");
    return *(volatile uint32_t *)(uintptr_t)
        (XHCI_DIRECT_MAP + ctrl->mmio_phys + offset);
}

static inline void xhci_mmio_write32(const xhci_controller_t *ctrl,
                                     uint64_t offset, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)
        (XHCI_DIRECT_MAP + ctrl->mmio_phys + offset) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline void xhci_mmio_write64(const xhci_controller_t *ctrl,
                                     uint64_t offset, uint64_t value) {
    *(volatile uint64_t *)(uintptr_t)
        (XHCI_DIRECT_MAP + ctrl->mmio_phys + offset) = value;
    __asm__ volatile ("mfence" ::: "memory");
}

static void *xhci_dma_alloc(uint64_t bytes, uint64_t align_pages,
                            uint64_t *phys_out) {
    uint64_t pages = (bytes + XHCI_PAGE_SIZE - 1u) / XHCI_PAGE_SIZE;
    uint64_t phys = pmm_alloc_frames_range(pages, align_pages, XHCI_DMA_MIN,
                                            XHCI_DMA_MAX);
    if (phys == (uint64_t)-1)
        return NULL;
    void *virt = (void *)(uintptr_t)(XHCI_DIRECT_MAP + phys);
    memset(virt, 0, (size_t)(pages * XHCI_PAGE_SIZE));
    if (phys_out)
        *phys_out = phys;
    return virt;
}

static inline uint32_t xhci_op_read32(const xhci_controller_t *ctrl,
                                      uint32_t offset) {
    return xhci_mmio_read32(ctrl, ctrl->op_phys - ctrl->mmio_phys + offset);
}

static inline void xhci_op_write32(const xhci_controller_t *ctrl,
                                   uint32_t offset, uint32_t value) {
    xhci_mmio_write32(ctrl, ctrl->op_phys - ctrl->mmio_phys + offset, value);
}

static inline void xhci_doorbell_write(const xhci_controller_t *ctrl,
                                       uint32_t offset, uint32_t value) {
    xhci_mmio_write32(ctrl, ctrl->doorbell_phys - ctrl->mmio_phys + offset,
                      value);
}

static inline uint32_t xhci_port_read(const xhci_controller_t *ctrl,
                                      uint8_t port) {
    return xhci_op_read32(ctrl, 0x400u + (uint32_t)port * 0x10u);
}

static inline void xhci_port_write(const xhci_controller_t *ctrl,
                                   uint8_t port, uint32_t value) {
    xhci_op_write32(ctrl, 0x400u + (uint32_t)port * 0x10u, value);
}

static bool xhci_wait_bits(const xhci_controller_t *ctrl, uint32_t offset,
                           uint32_t mask, bool set, uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        bool value = (xhci_op_read32(ctrl, offset) & mask) != 0;
        if (value == set)
            return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static xhci_ring_t *xhci_ring_create(uint16_t link_type) {
    uint64_t phys = 0;
    xhci_ring_t *ring = (xhci_ring_t *)kmalloc(sizeof(*ring));
    if (!ring)
        return NULL;
    memset(ring, 0, sizeof(*ring));
    ring->trbs = (xhci_trb_t *)xhci_dma_alloc(XHCI_PAGE_SIZE, 1, &phys);
    if (!ring->trbs) {
        kfree(ring);
        return NULL;
    }
    ring->phys = phys;
    ring->cycle = 1;
    xhci_trb_t *link = &ring->trbs[XHCI_RING_TRBS - 1u];
    link->parameter = phys;
    link->control = XHCI_TRB_TYPE(link_type) | XHCI_TRB_TOGGLE
                  | XHCI_TRB_CYCLE;
    return ring;
}

static void xhci_ring_push(xhci_ring_t *ring, xhci_trb_t *trb) {
    if (ring->enqueue == XHCI_RING_TRBS - 1u) {
        ring->trbs[ring->enqueue].control =
            (ring->trbs[ring->enqueue].control & ~XHCI_TRB_CYCLE) | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1u;
    }
    trb->control = (trb->control & ~XHCI_TRB_CYCLE) | ring->cycle;
    ring->trbs[ring->enqueue] = *trb;
    __asm__ volatile ("mfence" ::: "memory");
    ring->enqueue++;
}

static xhci_trb_t *xhci_next_event(xhci_controller_t *ctrl) {
    xhci_trb_t *event = &ctrl->events[ctrl->event_dequeue];
    __asm__ volatile ("lfence" ::: "memory");
    if ((event->control & XHCI_TRB_CYCLE) != ctrl->event_cycle)
        return NULL;
    return event;
}

static void xhci_consume_event(xhci_controller_t *ctrl) {
    ctrl->event_dequeue++;
    if (ctrl->event_dequeue == XHCI_RING_TRBS) {
        ctrl->event_dequeue = 0;
        ctrl->event_cycle ^= 1u;
    }
    uint64_t erdp = ctrl->events_phys
                  + (uint64_t)ctrl->event_dequeue * sizeof(xhci_trb_t);
    xhci_mmio_write64(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x38u, erdp | (1u << 3));
}

static bool xhci_wait_command(xhci_controller_t *ctrl, uint8_t *out_slot) {
    xhci_ring_t *ring = &ctrl->command;
    uint16_t command_index = ring->enqueue == 0
        ? XHCI_RING_TRBS - 2u : (uint16_t)(ring->enqueue - 1u);
    uint64_t command_phys = ring->phys
                          + (uint64_t)command_index * sizeof(xhci_trb_t);
    xhci_doorbell_write(ctrl, 0, 0);

    for (uint32_t i = 0; i < XHCI_COMMAND_LIMIT; i++) {
        xhci_trb_t *event = xhci_next_event(ctrl);
        if (event) {
            uint32_t type = (event->control >> 10) & 0x3Fu;
            if (type == XHCI_EVENT_COMMAND) {
                uint8_t code = (uint8_t)(event->status >> 24);
                bool same = (event->parameter == command_phys);
                if (out_slot)
                    *out_slot = (uint8_t)(event->control >> 24);
                xhci_consume_event(ctrl);
                return same && code == XHCI_CC_SUCCESS;
            }
            xhci_consume_event(ctrl);
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    DBG_MSG("XHCI", "command completion timeout");
    return false;
}

static bool xhci_command(xhci_controller_t *ctrl, uint64_t parameter,
                         uint32_t status, uint32_t control,
                         uint8_t *out_slot) {
    xhci_trb_t trb;
    trb.parameter = parameter;
    trb.status = status;
    trb.control = control;
    uint16_t index = ctrl->command.enqueue;
    xhci_ring_push(&ctrl->command, &trb);
    (void)index;
    return xhci_wait_command(ctrl, out_slot);
}

static inline uint32_t *xhci_context(xhci_controller_t *ctrl,
                                     uint32_t *base, uint32_t index) {
    return base + index * ctrl->context_words;
}

static bool xhci_wait_transfer(xhci_controller_t *ctrl, uint8_t slot_id,
                               uint8_t endpoint_id, uint32_t *residual) {
    for (uint32_t i = 0; i < XHCI_TRANSFER_LIMIT; i++) {
        xhci_trb_t *event = xhci_next_event(ctrl);
        if (event) {
            uint32_t type = (event->control >> 10) & 0x3Fu;
            uint8_t event_slot = (uint8_t)(event->control >> 24);
            uint8_t event_ep = (uint8_t)((event->control >> 16) & 0x1Fu);
            uint8_t code = (uint8_t)(event->status >> 24);
            uint32_t remaining = event->status & 0xFFFFFFu;
            xhci_consume_event(ctrl);
            if (type != XHCI_EVENT_TRANSFER || event_slot != slot_id
                || event_ep != endpoint_id)
                continue;
            if (residual)
                *residual = remaining;
            return code == XHCI_CC_SUCCESS || code == XHCI_CC_SHORT_PACKET;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    DBG_VAL("XHCI", "transfer timeout, slot", slot_id);
    return false;
}

static uint8_t xhci_endpoint_id(uint8_t ep_addr) {
    uint8_t ep_num = ep_addr & 0x0Fu;
    if (ep_num == 0)
        return 1;
    return (uint8_t)(ep_num * 2u + ((ep_addr & 0x80u) ? 1u : 0u));
}

static xhci_slot_t *xhci_find_slot(xhci_controller_t *ctrl, uint8_t address,
                                   uint8_t *slot_id) {
    if (address >= 128)
        return NULL;
    uint8_t id = ctrl->address_slot[address];
    if (!id || id > ctrl->max_slots || !ctrl->slots[id].used)
        return NULL;
    if (slot_id)
        *slot_id = id;
    return &ctrl->slots[id];
}

static bool xhci_address_device(xhci_controller_t *ctrl, uint8_t port,
                                uint8_t speed_id, uint16_t ep0_packet,
                                uint8_t *slot_id_out) {
    uint8_t slot_id = 0;
    if (!xhci_command(ctrl, 0, 0, XHCI_TRB_TYPE(XHCI_CMD_ENABLE_SLOT),
                      &slot_id)
        || !slot_id || slot_id > ctrl->max_slots) {
        DBG_MSG("XHCI", "Enable Slot failed");
        return false;
    }

    xhci_slot_t *slot = &ctrl->slots[slot_id];
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->root_port = port;
    slot->speed_id = speed_id;
    slot->ep0_packet = ep0_packet;
    slot->input_context = (uint32_t *)xhci_dma_alloc(
        XHCI_PAGE_SIZE, 1, &slot->input_context_phys);
    slot->output_context = (uint32_t *)xhci_dma_alloc(
        XHCI_PAGE_SIZE, 1, &slot->output_context_phys);
    slot->endpoint_rings[1] = xhci_ring_create(XHCI_LINK);
    if (!slot->input_context || !slot->output_context
        || !slot->endpoint_rings[1]) {
        DBG_MSG("XHCI", "device context allocation failed");
        return false;
    }
    slot->endpoint_packet[1] = ep0_packet;
    ctrl->dcbaa[slot_id] = slot->output_context_phys;

    uint32_t *slot_ctx = xhci_context(ctrl, slot->input_context, 1);
    slot_ctx[0] = ((uint32_t)speed_id << 20) | (1u << 27);
    slot_ctx[1] = ((uint32_t)(port + 1u) << 16);
    uint32_t *ep0_ctx = xhci_context(ctrl, slot->input_context, 2);
    ep0_ctx[1] = (3u << 1) | (4u << 3)
               | ((uint32_t)ep0_packet << 16);
    ep0_ctx[2] = (uint32_t)slot->endpoint_rings[1]->phys
               | slot->endpoint_rings[1]->cycle;
    ep0_ctx[3] = (uint32_t)(slot->endpoint_rings[1]->phys >> 32);
    ep0_ctx[4] = 8u;
    uint32_t *control_ctx = slot->input_context;
    control_ctx[1] = (1u << 0) | (1u << 1);

    if (!xhci_command(ctrl, slot->input_context_phys, 0,
            XHCI_TRB_TYPE(XHCI_CMD_ADDRESS_DEVICE) | XHCI_TRB_SLOT(slot_id),
            NULL)) {
        DBG_MSG("XHCI", "Address Device failed");
        return false;
    }

    ctrl->address_slot[0] = slot_id;
    *slot_id_out = slot_id;
    return true;
}

static bool xhci_ensure_endpoint(xhci_controller_t *ctrl, uint8_t slot_id,
                                 uint8_t ep_addr, uint16_t max_packet) {
    xhci_slot_t *slot = &ctrl->slots[slot_id];
    uint8_t endpoint_id = xhci_endpoint_id(ep_addr);
    if (endpoint_id >= 32 || !max_packet)
        return false;
    if (slot->endpoint_rings[endpoint_id])
        return true;

    xhci_ring_t *ring = xhci_ring_create(XHCI_LINK);
    if (!ring)
        return false;

    memset(slot->input_context, 0, XHCI_PAGE_SIZE);
    uint32_t *slot_ctx = xhci_context(ctrl, slot->input_context, 1);
    uint32_t *output_slot = xhci_context(ctrl, slot->output_context, 0);
    memcpy(slot_ctx, output_slot,
           ctrl->context_words * sizeof(uint32_t));
    uint32_t context_entries = output_slot[0] >> 27;
    if (endpoint_id > context_entries)
        context_entries = endpoint_id;
    slot_ctx[0] = (output_slot[0] & 0x07FFFFFFu)
                | (context_entries << 27);

    uint32_t *ep_ctx = xhci_context(ctrl, slot->input_context,
                                    (uint32_t)endpoint_id + 1u);
    bool in = (ep_addr & 0x80u) != 0;
    uint8_t ep_num = ep_addr & 0x0Fu;
    ep_ctx[1] = (3u << 1) | ((uint32_t)(in ? 6u : 2u) << 3)
              | ((uint32_t)max_packet << 16);
    ep_ctx[2] = (uint32_t)ring->phys | ring->cycle;
    ep_ctx[3] = (uint32_t)(ring->phys >> 32);
    ep_ctx[4] = max_packet;
    slot->input_context[1] = (1u << 0) | (1u << endpoint_id);

    if (!xhci_command(ctrl, slot->input_context_phys, 0,
            XHCI_TRB_TYPE(XHCI_CMD_CONFIGURE_ENDPOINT)
                | XHCI_TRB_SLOT(slot_id), NULL)) {
        DBG_VAL("XHCI", "Configure Endpoint failed, endpoint", endpoint_id);
        return false;
    }
    slot->endpoint_rings[endpoint_id] = ring;
    slot->endpoint_packet[endpoint_id] = max_packet;
    (void)ep_num;
    return true;
}

static int xhci_control_transfer(usb_hcd_t *hcd, uint8_t dev_addr,
                                 uint8_t speed, uint8_t max_packet0,
                                 const usb_setup_pkt_t *setup, void *data,
                                 uint16_t len) {
    uint64_t flags = xhci_irq_save();
    (void)speed;
    (void)max_packet0;
    xhci_controller_t *ctrl = (xhci_controller_t *)hcd->priv;
    if (!setup || len > XHCI_PAGE_SIZE || (len && !data)) {
        xhci_irq_restore(flags);
        return -1;
    }

    uint8_t slot_id = 0;
    xhci_slot_t *slot = xhci_find_slot(ctrl, dev_addr, &slot_id);
    if (!slot || !slot->endpoint_rings[1]) {
        xhci_irq_restore(flags);
        return -1;
    }

    if (setup->bRequest == 5
        && (setup->bmRequestType & 0x7Fu) == 0
        && setup->wValue < 128) {
        uint8_t new_address = (uint8_t)setup->wValue;
        if (ctrl->address_slot[dev_addr] == slot_id)
            ctrl->address_slot[dev_addr] = 0;
        slot->address = new_address;
        ctrl->address_slot[new_address] = slot_id;
        xhci_irq_restore(flags);
        return 0;
    }

    xhci_ring_t *ring = slot->endpoint_rings[1];
    uint8_t *bytes = (uint8_t *)data;
    if (len && !(setup->bmRequestType & 0x80u))
        memcpy(ctrl->scratch, bytes, len);

    uint64_t setup_value = (uint64_t)setup->bmRequestType
                         | ((uint64_t)setup->bRequest << 8)
                         | ((uint64_t)setup->wValue << 16)
                         | ((uint64_t)setup->wIndex << 32)
                         | ((uint64_t)len << 48);
    uint32_t transfer_type = len == 0 ? 0u
        : ((setup->bmRequestType & 0x80u) ? 3u : 2u);
    xhci_trb_t trb = {0};
    trb.parameter = setup_value;
    trb.status = 0;
    trb.control = XHCI_TRB_TYPE(XHCI_SETUP_STAGE) | XHCI_TRB_IDT
                | XHCI_TRB_CHAIN | (transfer_type << 16);
    xhci_ring_push(ring, &trb);

    if (len) {
        trb.parameter = ctrl->scratch_phys;
        trb.status = len;
        trb.control = XHCI_TRB_TYPE(XHCI_DATA_STAGE) | XHCI_TRB_CHAIN
                    | ((setup->bmRequestType & 0x80u) ? XHCI_TRB_DIR_IN : 0u);
        xhci_ring_push(ring, &trb);
    }

    trb.parameter = 0;
    trb.status = 0;
    trb.control = XHCI_TRB_TYPE(XHCI_STATUS_STAGE) | XHCI_TRB_IOC
                | ((len == 0 || !(setup->bmRequestType & 0x80u))
                   ? XHCI_TRB_DIR_IN : 0u);
    xhci_ring_push(ring, &trb);
    xhci_doorbell_write(ctrl, (uint32_t)slot_id * 4u,
                        1u);

    uint32_t residual = 0;
    if (!xhci_wait_transfer(ctrl, slot_id, 1, &residual)) {
        xhci_irq_restore(flags);
        return -1;
    }
    if (len && (setup->bmRequestType & 0x80u)) {
        uint32_t transferred = residual <= len ? len - residual : 0;
        memcpy(bytes, ctrl->scratch, transferred);
    }
    xhci_irq_restore(flags);
    return 0;
}

static int xhci_bulk_transfer(usb_hcd_t *hcd, uint8_t dev_addr,
                              uint8_t speed, uint8_t ep_addr,
                              uint16_t max_packet, void *data,
                              uint32_t len, uint8_t *toggle) {
    uint64_t flags = xhci_irq_save();
    (void)speed;
    if (toggle)
        *toggle = 0;
    if (!len) {
        xhci_irq_restore(flags);
        return 0;
    }
    if (!data || len > 65536u) {
        xhci_irq_restore(flags);
        return -1;
    }

    xhci_controller_t *ctrl = (xhci_controller_t *)hcd->priv;
    uint8_t slot_id = 0;
    xhci_slot_t *slot = xhci_find_slot(ctrl, dev_addr, &slot_id);
    if (!slot || !xhci_ensure_endpoint(ctrl, slot_id, ep_addr, max_packet)) {
        xhci_irq_restore(flags);
        return -1;
    }

    bool in = (ep_addr & 0x80u) != 0;
    xhci_ring_t *ring = slot->endpoint_rings[xhci_endpoint_id(ep_addr)];
    uint8_t *bytes = (uint8_t *)data;
    if (!in)
        memcpy(ctrl->scratch, bytes, len);

    xhci_trb_t trb = {0};
    trb.parameter = ctrl->scratch_phys;
    trb.status = len & XHCI_TRB_LEN_MASK;
    trb.control = XHCI_TRB_TYPE(XHCI_NORMAL) | XHCI_TRB_IOC
                | (in ? XHCI_TRB_DIR_IN : 0u);
    xhci_ring_push(ring, &trb);
    uint8_t endpoint_id = xhci_endpoint_id(ep_addr);
    xhci_doorbell_write(ctrl, (uint32_t)slot_id * 4u, endpoint_id);

    uint32_t residual = 0;
    if (!xhci_wait_transfer(ctrl, slot_id, endpoint_id, &residual)) {
        xhci_irq_restore(flags);
        return -1;
    }
    if (in) {
        uint32_t transferred = residual <= len ? len - residual : 0;
        memcpy(bytes, ctrl->scratch, transferred);
    }
    xhci_irq_restore(flags);
    return 0;
}

static bool xhci_port_connected(usb_hcd_t *hcd, uint8_t port,
                                bool *out_connected) {
    xhci_controller_t *ctrl = (xhci_controller_t *)hcd->priv;
    if (port >= ctrl->num_ports || !out_connected)
        return false;
    *out_connected = (xhci_port_read(ctrl, port) & XHCI_PORTSC_CCS) != 0;
    return true;
}

static bool xhci_port_reset(usb_hcd_t *hcd, uint8_t port,
                            uint8_t *out_speed) {
    uint64_t flags = xhci_irq_save();
    xhci_controller_t *ctrl = (xhci_controller_t *)hcd->priv;
    if (port >= ctrl->num_ports || !out_speed) {
        xhci_irq_restore(flags);
        return false;
    }

    uint32_t portsc = xhci_port_read(ctrl, port);
    if (!(portsc & XHCI_PORTSC_CCS)) {
        xhci_irq_restore(flags);
        return false;
    }
    uint8_t speed_id = (uint8_t)((portsc >> 10) & 0xFu);
    if (speed_id == 0) {
        xhci_irq_restore(flags);
        return false;
    }

    uint32_t clear_changes = portsc & XHCI_PORTSC_CHANGE;
    portsc &= ~(XHCI_PORTSC_CHANGE | XHCI_PORTSC_PED
                | XHCI_PORTSC_PR | XHCI_PORTSC_WPR);
    if (clear_changes)
        xhci_port_write(ctrl, port, portsc | clear_changes);

    if (!(portsc & XHCI_PORTSC_PP)) {
        portsc &= ~XHCI_PORTSC_CHANGE;
        xhci_port_write(ctrl, port, portsc | XHCI_PORTSC_PP);
    }

    if (speed_id >= 4u)
        xhci_port_write(ctrl, port, (portsc & ~XHCI_PORTSC_CHANGE)
                                  | XHCI_PORTSC_WPR);
    else
        xhci_port_write(ctrl, port, (portsc & ~XHCI_PORTSC_CHANGE)
                                  | XHCI_PORTSC_PR);
    usb_delay_ms(50);

    bool reset_done = false;
    for (uint32_t i = 0; i < 5000u; i++) {
        uint32_t current = xhci_port_read(ctrl, port);
        if (current & XHCI_PORTSC_PRC || current & XHCI_PORTSC_WRC) {
            reset_done = true;
            xhci_port_write(ctrl, port,
                (current & ~(XHCI_PORTSC_CHANGE | XHCI_PORTSC_PR
                             | XHCI_PORTSC_WPR))
                | (current & (XHCI_PORTSC_PRC | XHCI_PORTSC_WRC)));
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    if (!reset_done) {
        DBG_VAL("XHCI", "root port reset timeout", port + 1u);
        xhci_irq_restore(flags);
        return false;
    }

    portsc = xhci_port_read(ctrl, port);
    if (!(portsc & XHCI_PORTSC_CCS) || !(portsc & XHCI_PORTSC_PED)) {
        xhci_irq_restore(flags);
        return false;
    }

    switch (speed_id) {
        case 1: *out_speed = USB_SPEED_FULL; break;
        case 2: *out_speed = USB_SPEED_LOW; break;
        case 3: *out_speed = USB_SPEED_HIGH; break;
        case 4: *out_speed = USB_SPEED_SUPER; break;
        default:
            if (speed_id > 4u)
                *out_speed = USB_SPEED_SUPER;
            else {
                DBG_VAL("XHCI", "unsupported port speed ID", speed_id);
                xhci_irq_restore(flags);
                return false;
            }
    }

    uint16_t ep0_packet = speed_id == 4u ? 512u
                         : speed_id == 3u ? 64u : 8u;
    uint8_t slot_id = 0;
    if (!xhci_address_device(ctrl, port, speed_id, ep0_packet, &slot_id)) {
        xhci_irq_restore(flags);
        return false;
    }
    xhci_irq_restore(flags);
    return true;
}

static const usb_hcd_ops_t g_xhci_ops = {
    .control_transfer = xhci_control_transfer,
    .port_reset = xhci_port_reset,
    .bulk_transfer = xhci_bulk_transfer,
    .port_connected = xhci_port_connected,
    .supports_hubs = false,
};

static bool xhci_bios_handoff(xhci_controller_t *ctrl,
                              uint32_t hccparams1) {
    uint32_t ecp = (hccparams1 >> 16) & 0xFFFFu;
    if (!ecp)
        return true;
    uint32_t cap = ecp * 4u;
    for (uint32_t i = 0; cap >= 0x20u && cap <= 0xFFCu && i < 64u; i++) {
        uint32_t value = xhci_mmio_read32(ctrl, cap);
        uint8_t id = (uint8_t)value;
        uint32_t next = ((value >> 8) & 0xFFu) * 4u;
        if (id == 1u) {
            uint32_t legacy = value | (1u << 24);
            xhci_mmio_write32(ctrl, cap, legacy);
            for (uint32_t spin = 0; spin < 1000000u; spin++) {
                if (!(xhci_mmio_read32(ctrl, cap) & (1u << 16)))
                    return true;
                __asm__ volatile ("pause" ::: "memory");
            }
            DBG_MSG("XHCI", "BIOS ownership handoff timed out");
            return false;
        }
        if (!next || next == cap)
            break;
        cap = next;
    }
    return true;
}

static void xhci_release(xhci_controller_t *ctrl) {
    if (ctrl->op_phys) {
        xhci_op_write32(ctrl, 0, xhci_op_read32(ctrl, 0) & ~1u);
        (void)xhci_wait_bits(ctrl, 4, 1u << 0, true, 1000000u);
    }
    xhci_pci_write16(ctrl->pci_bus, ctrl->pci_dev, ctrl->pci_fn, 0x04,
                     (uint16_t)ctrl->pci_command);
}

static bool xhci_controller_start(xhci_controller_t *ctrl,
                                  uint8_t bus, uint8_t dev, uint8_t fn,
                                  uint64_t mmio_phys, uint32_t hccparams1) {
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->mmio_phys = mmio_phys;
    ctrl->pci_bus = bus;
    ctrl->pci_dev = dev;
    ctrl->pci_fn = fn;
    ctrl->cap_length = *(volatile uint8_t *)(uintptr_t)
        (XHCI_DIRECT_MAP + mmio_phys);
    ctrl->op_phys = mmio_phys + ctrl->cap_length;
    ctrl->doorbell_phys = mmio_phys
        + xhci_mmio_read32(ctrl, 0x14u);
    ctrl->runtime_phys = mmio_phys
        + (xhci_mmio_read32(ctrl, 0x18u) & ~0x1Fu);
    ctrl->context_words = (hccparams1 & (1u << 2)) ? 16u : 8u;

    uint32_t hcsparams1 = xhci_mmio_read32(ctrl, 0x04u);
    uint32_t hcsparams2 = xhci_mmio_read32(ctrl, 0x08u);
    uint32_t hcsparams3 = xhci_mmio_read32(ctrl, 0x0Cu);
    uint32_t max_slots = hcsparams1 & 0xFFu;
    uint32_t max_ports = (hcsparams1 >> 24) & 0xFFu;
    if (!max_slots || !max_ports || !(xhci_op_read32(ctrl, 8) & 1u)) {
        DBG_MSG("XHCI", "invalid controller capabilities");
        return false;
    }
    ctrl->max_slots = max_slots < XHCI_MAX_SLOTS ? max_slots : XHCI_MAX_SLOTS;
    ctrl->num_ports = (uint8_t)(max_ports > 255u ? 255u : max_ports);

    DBG_VAL("XHCI", "HCSPARAMS2", hcsparams2);
    DBG_VAL("XHCI", "HCSPARAMS3", hcsparams3);
    if (!xhci_bios_handoff(ctrl, hccparams1))
        return false;

    uint32_t command = xhci_op_read32(ctrl, 0);
    xhci_op_write32(ctrl, 0, command & ~1u);
    if (!xhci_wait_bits(ctrl, 4, 1u << 0, true, 10000000u)) {
        DBG_MSG("XHCI", "controller did not halt");
        return false;
    }
    xhci_op_write32(ctrl, 0, xhci_op_read32(ctrl, 0) | (1u << 1));
    if (!xhci_wait_bits(ctrl, 0, 1u << 1, false, 10000000u)
        || !xhci_wait_bits(ctrl, 4, 1u << 11, false, 10000000u)) {
        DBG_MSG("XHCI", "controller reset failed");
        return false;
    }

    uint64_t phys = 0;
    ctrl->dcbaa = (uint64_t *)xhci_dma_alloc(XHCI_PAGE_SIZE, 1, &phys);
    ctrl->dcbaa_phys = phys;
    ctrl->command.trbs = (xhci_trb_t *)xhci_dma_alloc(
        XHCI_PAGE_SIZE, 1, &ctrl->command.phys);
    ctrl->command.cycle = 1;
    ctrl->command.enqueue = 0;
    if (ctrl->command.trbs) {
        xhci_trb_t *link = &ctrl->command.trbs[XHCI_RING_TRBS - 1u];
        link->parameter = ctrl->command.phys;
        link->control = XHCI_TRB_TYPE(XHCI_LINK) | XHCI_TRB_TOGGLE
                      | XHCI_TRB_CYCLE;
    }
    ctrl->events = (xhci_trb_t *)xhci_dma_alloc(
        XHCI_PAGE_SIZE, 1, &ctrl->events_phys);
    ctrl->event_cycle = 1;
    ctrl->erst = (xhci_erst_entry_t *)xhci_dma_alloc(
        XHCI_PAGE_SIZE, 1, &ctrl->erst_phys);
    ctrl->scratch = (uint8_t *)xhci_dma_alloc(
        65536u, 16, &ctrl->scratch_phys);
    if (!ctrl->dcbaa || !ctrl->command.trbs || !ctrl->events
        || !ctrl->erst || !ctrl->scratch) {
        DBG_MSG("XHCI", "DMA memory allocation failed");
        return false;
    }

    uint32_t scratchpad_hi = (hcsparams2 >> 21) & 0x1Fu;
    uint32_t scratchpad_lo = (hcsparams2 >> 27) & 0x1Fu;
    ctrl->scratchpad_count = (scratchpad_hi << 5) | scratchpad_lo;
    if (ctrl->scratchpad_count) {
        uint64_t array_size = (uint64_t)ctrl->scratchpad_count * sizeof(uint64_t);
        ctrl->scratchpad_array = (uint64_t *)xhci_dma_alloc(
            array_size, 1, &ctrl->scratchpad_array_phys);
        uint64_t scratchpad_bytes =
            (uint64_t)ctrl->scratchpad_count * XHCI_PAGE_SIZE;
        uint8_t *scratchpads = (uint8_t *)xhci_dma_alloc(
            scratchpad_bytes, 1, &ctrl->scratchpad_phys);
        if (ctrl->scratchpad_array && scratchpads) {
            for (uint32_t i = 0; i < ctrl->scratchpad_count; i++)
                ctrl->scratchpad_array[i] =
                    ctrl->scratchpad_phys + (uint64_t)i * XHCI_PAGE_SIZE;
            ctrl->dcbaa[0] = ctrl->scratchpad_array_phys;
        } else {
            DBG_MSG("XHCI", "scratchpad allocation failed");
            return false;
        }
    }

    ctrl->erst[0].base = ctrl->events_phys;
    ctrl->erst[0].size = XHCI_RING_TRBS;
    xhci_mmio_write64(ctrl, ctrl->op_phys - ctrl->mmio_phys + 0x30u,
                      ctrl->dcbaa_phys);
    xhci_op_write32(ctrl, 0x38u, ctrl->max_slots);
    xhci_mmio_write64(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x28u, ctrl->erst_phys);
    xhci_mmio_write32(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x20u, 1u);
    xhci_mmio_write64(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x38u, ctrl->events_phys);
    xhci_mmio_write32(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x20u, 0u);
    xhci_mmio_write64(ctrl,
        ctrl->op_phys - ctrl->mmio_phys + 0x18u,
        ctrl->command.phys | ctrl->command.cycle);

     
    xhci_mmio_write32(ctrl,
        ctrl->runtime_phys - ctrl->mmio_phys + 0x20u, 0u);
    xhci_op_write32(ctrl, 0, 1u);
    if (!xhci_wait_bits(ctrl, 4, 1u << 0, false, 10000000u)) {
        DBG_MSG("XHCI", "controller did not start");
        return false;
    }

    ctrl->hcd.ops = &g_xhci_ops;
    ctrl->hcd.priv = ctrl;
    ctrl->hcd.name = "xhci";
    ctrl->hcd.num_ports = ctrl->num_ports;
    usb_register_hcd(&ctrl->hcd);
    DBG_VAL("XHCI", "root ports", ctrl->num_ports);
    DBG_VAL("XHCI", "slots", ctrl->max_slots);
    return true;
}

static bool xhci_probe(uint8_t bus, uint8_t dev, uint8_t fn) {
    if (g_xhci_count >= XHCI_MAX_CONTROLLERS)
        return false;

    uint32_t bar0 = xhci_pci_read32(bus, dev, fn, 0x10);
    if (bar0 & 1u)
        return false;
    uint32_t bar_type = (bar0 >> 1) & 3u;
    if (bar_type != 0u && bar_type != 2u)
        return false;
    uint64_t mmio_phys = bar0 & ~0xFULL;
    if (bar_type == 2u)
        mmio_phys |= (uint64_t)xhci_pci_read32(bus, dev, fn, 0x14) << 32;
    if (!mmio_phys || mmio_phys >= XHCI_DMA_MAX)
        return false;

    uint16_t pci_command = xhci_pci_read16(bus, dev, fn, 0x04);
    xhci_pci_write16(bus, dev, fn, 0x04, pci_command | 0x06u);

    xhci_controller_t *ctrl = &g_xhci[g_xhci_count];
    uint32_t hccparams1 =
        *(volatile uint32_t *)(uintptr_t)(XHCI_DIRECT_MAP + mmio_phys + 0x10u);
    bool started = xhci_controller_start(ctrl, bus, dev, fn,
                                         mmio_phys, hccparams1);
    ctrl->pci_command = pci_command;
    if (!started) {
        DBG_MSG("XHCI", "controller initialization failed");
        xhci_release(ctrl);
        return false;
    }
    g_xhci_count++;
    return true;
}

void xhci_pci_scan(void) {
    DBG_MSG("XHCI", "=== xhci_pci_scan ===");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            bool multifunction = false;
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = xhci_pci_read32((uint8_t)bus, dev, fn, 0);
                if ((id & 0xFFFFu) == 0xFFFFu) {
                    if (fn == 0)
                        break;
                    continue;
                }
                uint32_t class_reg =
                    xhci_pci_read32((uint8_t)bus, dev, fn, 8);
                uint8_t class_code = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t prog_if = (uint8_t)(class_reg >> 8);
                uint8_t header =
                    (uint8_t)(xhci_pci_read32((uint8_t)bus, dev, fn, 0x0C)
                              >> 16);
                if (fn == 0)
                    multifunction = (header & 0x80u) != 0;
                if (class_code == 0x0Cu && subclass == 0x03u
                    && prog_if == 0x30u)
                    (void)xhci_probe((uint8_t)bus, dev, fn);
                if (fn == 0 && !multifunction)
                    break;
            }
        }
    }
    DBG_VAL("XHCI", "controllers initialized", (uint64_t)g_xhci_count);
}
