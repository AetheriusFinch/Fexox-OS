/* Fexox OS Project, EULA GPLv2 */
 

#include "EHCI.h"
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);
extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames_range(size_t count, size_t align,
                                          phys_addr_t min_phys, phys_addr_t max_phys);
extern void        pmm_free_frames(phys_addr_t pa, size_t count);

#ifndef DIRECT_MAP_OFFSET
#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#endif
#define DMA32_MIN_PHYS 0x100000ULL
#define DMA32_MAX_PHYS 0x100000000ULL    

static void *dma32_alloc(uint64_t size, uint64_t *out_phys) {
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = pmm_alloc_frames_range(pages, 1, DMA32_MIN_PHYS, DMA32_MAX_PHYS);
    if (pa == (phys_addr_t)-1) return NULL;
    void *va = (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
    uint8_t *p = (uint8_t *)va;
    for (uint64_t i = 0; i < pages * 4096; i++) p[i] = 0;
    if (out_phys) *out_phys = pa;
    return va;
}

static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t a = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC);
    __asm__ volatile ("outl %0,%1" :: "a"(a), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile ("inl %1,%0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}
static inline void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    uint32_t a = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC);
    __asm__ volatile ("outl %0,%1" :: "a"(a), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outl %0,%1" :: "a"(v), "Nd"((uint16_t)0xCFC));
}

 




static inline uint32_t mmio_r32(uint64_t va) {
    return *(volatile uint32_t *)(uintptr_t)va;
}
static inline void mmio_w32(uint64_t va, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)va = v;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline uint64_t ehci_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
 

static void ehci_delay_ms(uint32_t ms) { usb_delay_ms(ms); }

 


#define EHCI_CAP_CAPLENGTH   0x00    
#define EHCI_CAP_HCIVERSION  0x02    
#define EHCI_CAP_HCSPARAMS   0x04    
#define EHCI_CAP_HCCPARAMS   0x08    

#define EHCI_OP_USBCMD           0x00
#define EHCI_OP_USBSTS           0x04
#define EHCI_OP_USBINTR          0x08
#define EHCI_OP_FRINDEX          0x0C
#define EHCI_OP_CTRLDSSEGMENT    0x10
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR    0x18
#define EHCI_OP_CONFIGFLAG       0x40
#define EHCI_OP_PORTSC_BASE      0x44

#define EHCI_CMD_RS         0x00000001u    
#define EHCI_CMD_HCRESET    0x00000002u
#define EHCI_CMD_PSE        0x00000010u    
#define EHCI_CMD_ASE        0x00000020u    

#define EHCI_STS_HALTED     0x00001000u
#define EHCI_STS_ASS        0x00008000u    
#define EHCI_STS_PSS        0x00004000u    

#define EHCI_PORTSC_CCS     0x00000001u    
#define EHCI_PORTSC_CSC     0x00000002u    
#define EHCI_PORTSC_PE      0x00000004u    
#define EHCI_PORTSC_PEC     0x00000008u    
#define EHCI_PORTSC_OCC     0x00000020u    
#define EHCI_PORTSC_PR      0x00000100u    
#define EHCI_PORTSC_PP      0x00001000u    
#define EHCI_PORTSC_OWNER   0x00002000u    
 
#define EHCI_PORTSC_RWC_MASK (EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC)

 


#define EHCI_PTR_TERMINATE 0x00000001u
#define EHCI_PTR_TYPE_QH   0x00000002u    

#define EHCI_EPS_FULL 0u
#define EHCI_EPS_LOW  1u
#define EHCI_EPS_HIGH 2u

 






#define EHCI_QH_ADDR(a)   ((uint32_t)(a) & 0x7Fu)
#define EHCI_QH_EPNUM(n)  (((uint32_t)(n) & 0x0Fu) << 8)
#define EHCI_QH_EPS(s)    (((uint32_t)(s) & 0x03u) << 12)
#define EHCI_QH_DTC       (1u << 14)
#define EHCI_QH_H         (1u << 15)
#define EHCI_QH_C         (1u << 27)    
#define EHCI_QH_MAXP(m)   (((uint32_t)(m) & 0x7FFu) << 16)

 










typedef struct {
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buf[5];
    volatile uint32_t buf_hi[5];      
    uint32_t          pad[3];         
} ehci_qtd_t;

typedef struct {
    volatile uint32_t horiz_link;
    volatile uint32_t ep_char;
    volatile uint32_t ep_cap;
    volatile uint32_t cur_qtd;
     
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buf[5];
    volatile uint32_t buf_hi[5];    
    uint32_t reserved[3];           
} ehci_qh_t;

#define EHCI_QTD_STATUS_XACTERR  (1u << 3)
#define EHCI_QTD_STATUS_BABBLE   (1u << 4)
#define EHCI_QTD_STATUS_DBUFERR  (1u << 5)
#define EHCI_QTD_STATUS_HALTED   (1u << 6)
#define EHCI_QTD_STATUS_ACTIVE   (1u << 7)

#define EHCI_QTD_PID_OUT   0u
#define EHCI_QTD_PID_IN    1u
#define EHCI_QTD_PID_SETUP 2u

#define EHCI_TD_POOL_COUNT 128    
#define EHCI_BULK_BUF_SIZE (64u * 1024u)    

 


 




#define EHCI_INT_SLOTS         4
#define EHCI_INT_FRAME_STRIDE  8
#define EHCI_INT_SMASK         0x01u     
#define EHCI_INT_CMASK         0x1Cu     

typedef struct {
    bool     used;
    uint8_t  dev_addr, ep_addr, tt_hub, tt_port, toggle;
    uint16_t maxp;
    uint32_t len;      
} ehci_int_slot_t;

#define EHCI_MAX_CONTROLLERS 4
#define EHCI_MAX_PORTS 15    

typedef struct {
    uint64_t   mmio_va;       
    uint64_t   op_va;         
    uint8_t    num_ports;
     

    uint8_t    tt_hub[128], tt_port[128];
    uint32_t   pci_id, hcsparams, hccparams;    

    ehci_qh_t   *ctrl_qh;
    uint64_t     ctrl_qh_phys;
    ehci_qtd_t  *td_pool;
    uint64_t     td_pool_phys;
    uint8_t     *scratch;
    uint64_t     scratch_phys;

    uint8_t     *bulk_buf;       
    uint64_t     bulk_buf_phys;

     
    uint32_t        *frame_list;        
    uint64_t         frame_list_phys;
    uint8_t         *int_mem;           
    uint64_t         int_mem_phys;
    ehci_int_slot_t  islot[EHCI_INT_SLOTS];

    usb_hcd_t  hcd;
} ehci_ctrl_t;

static ehci_ctrl_t g_ehci[EHCI_MAX_CONTROLLERS];
static int         g_ehci_count = 0;

static inline uint32_t op_r32(ehci_ctrl_t *c, uint32_t off) { return mmio_r32(c->op_va + off); }
static inline void     op_w32(ehci_ctrl_t *c, uint32_t off, uint32_t v) { mmio_w32(c->op_va + off, v); }

 



static bool ehci_async_set(ehci_ctrl_t *c, bool on) {
    uint32_t cmd = op_r32(c, EHCI_OP_USBCMD);
    uint32_t want = on ? (cmd | EHCI_CMD_ASE) : (cmd & ~EHCI_CMD_ASE);
    if (want != cmd) op_w32(c, EHCI_OP_USBCMD, want);
     




    uint64_t deadline = ehci_rdtsc() + usb_tsc_per_ms() * 40ULL;
    for (;;) {
        bool running = (op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_ASS) != 0;
        if (running == on) return true;
        if (ehci_rdtsc() > deadline) break;
        __asm__ volatile ("pause" ::: "memory");
    }
    DBG_MSG("EHCI", "async schedule state change timed out");
    return false;
}

 


static bool ehci_ensure_running(ehci_ctrl_t *c) {
    uint32_t sts = op_r32(c, EHCI_OP_USBSTS);
    if (!(sts & EHCI_STS_HALTED)) return true;
    DBG_VAL("EHCI", "controller halted (HSE?), restarting, USBSTS", sts);
    op_w32(c, EHCI_OP_USBSTS, 0x3Fu);
    op_w32(c, EHCI_OP_USBCMD, (op_r32(c, EHCI_OP_USBCMD) & ~EHCI_CMD_ASE) | EHCI_CMD_RS);
    for (int i = 0; i < 50; i++) {
        if (!(op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED)) return true;
        ehci_delay_ms(1);
    }
    return false;
}

 






typedef struct {
    bool     valid;
    int      rc;
    uint8_t  ctrl_idx, dev_addr, maxp0, n_qtd;
    uint32_t usbcmd, usbsts, qh_epchar, qh_next, qh_token, qtd_tok[3], qtd_last;
} ehci_err_snap_t;
static ehci_err_snap_t g_err;

static void ehci_snapshot_err(ehci_ctrl_t *c, int rc, uint8_t dev_addr, uint8_t maxp0, uint32_t n) {
    if (g_err.valid) return;
    g_err.valid     = true;
    g_err.rc        = rc;
    g_err.ctrl_idx  = (uint8_t)(c - g_ehci);
    g_err.dev_addr  = dev_addr;
    g_err.maxp0     = maxp0;
    g_err.n_qtd     = (uint8_t)n;
    g_err.usbcmd    = op_r32(c, EHCI_OP_USBCMD);
    g_err.usbsts    = op_r32(c, EHCI_OP_USBSTS);
    g_err.qh_epchar = c->ctrl_qh->ep_char;
    g_err.qh_next   = c->ctrl_qh->next_qtd;
    g_err.qh_token  = c->ctrl_qh->token;
    for (int i = 0; i < 3; i++) g_err.qtd_tok[i] = (n > (uint32_t)i) ? c->td_pool[i].token : 0;
    g_err.qtd_last  = (n > 0) ? c->td_pool[n - 1].token : 0;
}

static char *dp_str(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
static char *dp_hex(char *d, uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) *d++ = hx[(v >> (i * 4)) & 0xF];
    return d;
}

void ehci_diag_dump(void (*out)(const char *)) {
    char line[100];
    for (int i = 0; i < g_ehci_count; i++) {
        ehci_ctrl_t *c = &g_ehci[i];
        char *d = line;
        d = dp_str(d, "  EHCI"); *d++ = (char)('0' + i);
        d = dp_str(d, " pci="); d = dp_hex(d, c->pci_id);
        d = dp_str(d, " ports="); *d++ = (char)('0' + (c->num_ports % 10));
        *d++ = '\n'; *d = 0; out(line);

        d = line;
        d = dp_str(d, "   cmd="); d = dp_hex(d, op_r32(c, EHCI_OP_USBCMD));
        d = dp_str(d, " sts="); d = dp_hex(d, op_r32(c, EHCI_OP_USBSTS));
        d = dp_str(d, " cf="); d = dp_hex(d, op_r32(c, EHCI_OP_CONFIGFLAG));
        *d++ = '\n'; *d = 0; out(line);

        d = line;
        d = dp_str(d, "   portsc:");
        for (uint8_t p = 0; p < c->num_ports && p < 4; p++) {
            *d++ = ' '; d = dp_hex(d, op_r32(c, EHCI_OP_PORTSC_BASE + (uint32_t)p * 4));
        }
        *d++ = '\n'; *d = 0; out(line);
    }
    if (g_err.valid) {
        char *d = line;
        d = dp_str(d, "  1st xfer error: ehci"); *d++ = (char)('0' + g_err.ctrl_idx);
        d = dp_str(d, " rc="); *d++ = '-'; *d++ = (char)('0' + (-g_err.rc) % 10);
        d = dp_str(d, " addr="); d = dp_hex(d, g_err.dev_addr);
        d = dp_str(d, " n="); *d++ = (char)('0' + g_err.n_qtd % 10);
        *d++ = '\n'; *d = 0; out(line);

        d = line;
        d = dp_str(d, "   cmd="); d = dp_hex(d, g_err.usbcmd);
        d = dp_str(d, " sts="); d = dp_hex(d, g_err.usbsts);
        d = dp_str(d, " qh.ep="); d = dp_hex(d, g_err.qh_epchar);
        *d++ = '\n'; *d = 0; out(line);

        d = line;
        d = dp_str(d, "   qh.next="); d = dp_hex(d, g_err.qh_next);
        d = dp_str(d, " qh.tok="); d = dp_hex(d, g_err.qh_token);
        *d++ = '\n'; *d = 0; out(line);

        d = line;
        d = dp_str(d, "   qtd0="); d = dp_hex(d, g_err.qtd_tok[0]);
        d = dp_str(d, " qtd1="); d = dp_hex(d, g_err.qtd_tok[1]);
        d = dp_str(d, " qtd2="); d = dp_hex(d, g_err.qtd_tok[2]);
        *d++ = '\n'; *d = 0; out(line);
    } else {
        out("  no failed EHCI transfer recorded\n");
    }
}

 


static bool ehci_port_reset(usb_hcd_t *hcd, uint8_t port, uint8_t *out_speed) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    if (port >= c->num_ports) return false;
    uint32_t reg = EHCI_OP_PORTSC_BASE + (uint32_t)port * 4;

    uint32_t val = op_r32(c, reg);
    if (!(val & EHCI_PORTSC_CCS)) return false;    

     


    if (((val >> 10) & 0x3u) == 0x1u) {
        op_w32(c, reg, (val & ~EHCI_PORTSC_RWC_MASK) | EHCI_PORTSC_OWNER);
        DBG_VAL("EHCI", "low-speed device, handed off to companion, port", port);
        return false;
    }

     
    val &= ~EHCI_PORTSC_RWC_MASK;
    val &= ~EHCI_PORTSC_PE;
    op_w32(c, reg, val | EHCI_PORTSC_PR);
    ehci_delay_ms(50);

    val = op_r32(c, reg) & ~EHCI_PORTSC_RWC_MASK;
    op_w32(c, reg, val & ~EHCI_PORTSC_PR);

     

    bool cleared = false;
    for (int i = 0; i < 20; i++) {
        if (!(op_r32(c, reg) & EHCI_PORTSC_PR)) { cleared = true; break; }
        ehci_delay_ms(1);
    }
    if (!cleared) {
        DBG_VAL("EHCI", "port reset did not clear (hardware hung?), port", port);
        return false;
    }

    ehci_delay_ms(10);
    val = op_r32(c, reg);
    if (!(val & EHCI_PORTSC_CCS)) return false;    

    if (val & EHCI_PORTSC_PE) {
        *out_speed = USB_SPEED_HIGH;
        return true;
    }

     



    val = op_r32(c, reg) & ~EHCI_PORTSC_RWC_MASK;
    op_w32(c, reg, val | EHCI_PORTSC_OWNER);
    DBG_VAL("EHCI", "port not high-speed, handed off to companion, port", port);
    return false;
}

 




static bool ehci_qh_target(ehci_ctrl_t *c, uint8_t dev_addr, uint8_t speed, bool is_control,
                           uint32_t *out_eps_bits, uint32_t *out_cap) {
    if (speed == USB_SPEED_HIGH) {
        *out_eps_bits = EHCI_QH_EPS(EHCI_EPS_HIGH);
        *out_cap      = (1u << 30);
        return true;
    }
    uint8_t hub  = c->tt_hub[dev_addr & 0x7F];
    uint8_t port = c->tt_port[dev_addr & 0x7F];
    if (!hub) return false;    
    *out_eps_bits = EHCI_QH_EPS(speed == USB_SPEED_LOW ? EHCI_EPS_LOW : EHCI_EPS_FULL) |
                    (is_control ? EHCI_QH_C : 0u);
    *out_cap      = (1u << 30) | (((uint32_t)port & 0x7Fu) << 23) | (((uint32_t)hub & 0x7Fu) << 16);
    return true;
}

static void ehci_set_route(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t tt_hub_addr, uint8_t tt_port) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    c->tt_hub[dev_addr & 0x7F]  = tt_hub_addr;
    c->tt_port[dev_addr & 0x7F] = tt_port;
}

 


static void ehci_qtd_fill(ehci_qtd_t *td, uint8_t pid, uint16_t len,
                          uint64_t buf_phys, uint8_t toggle) {
    td->token = EHCI_QTD_STATUS_ACTIVE |
               ((uint32_t)pid << 8) |
               (3u << 10)            |
               (0u << 12)            |
               ((uint32_t)len << 16) |
               ((uint32_t)toggle << 31);
    td->buf[0] = (uint32_t)buf_phys;
    td->buf[1] = 0; td->buf[2] = 0; td->buf[3] = 0; td->buf[4] = 0;
    for (int i = 0; i < 5; i++) td->buf_hi[i] = 0;
    td->next_qtd     = EHCI_PTR_TERMINATE;
    td->alt_next_qtd = EHCI_PTR_TERMINATE;
}

 







static inline uint64_t usb_irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void usb_irq_restore(uint64_t f) {
    if (f & 0x200u) __asm__ volatile ("sti" ::: "memory");
}

static int ehci_control_transfer_impl(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                      uint8_t maxp0, const usb_setup_pkt_t *setup,
                                      void *data, uint16_t len) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    uint32_t eps_bits, qh_cap;
    if (!ehci_qh_target(c, dev_addr, speed, true, &eps_bits, &qh_cap)) return -1;    
    if (maxp0 == 0) maxp0 = (speed == USB_SPEED_HIGH) ? 64 : 8;
    if (len > 512) return -1;

    usb_setup_pkt_t *setup_dma = (usb_setup_pkt_t *)c->scratch;
    *setup_dma = *setup;
    uint64_t setup_phys = c->scratch_phys;

    uint8_t *data_dma  = c->scratch + 64;
    uint64_t data_phys = c->scratch_phys + 64;
    bool dir_in = (setup->bmRequestType & USB_REQ_DIR_D2H) != 0;
    if (data && len > 0 && !dir_in) memcpy(data_dma, data, len);

    ehci_qtd_t *tds = c->td_pool;
    uint32_t    n    = 0;

    ehci_qtd_fill(&tds[n++], EHCI_QTD_PID_SETUP, 8, setup_phys, 0);

    uint8_t  toggle    = 1;
    uint16_t remaining = len;
    uint16_t off       = 0;
    uint8_t  data_pid  = dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
    while (remaining > 0 && n < EHCI_TD_POOL_COUNT - 1) {
        uint16_t chunk = remaining > maxp0 ? maxp0 : remaining;
        ehci_qtd_fill(&tds[n++], data_pid, chunk, data_phys + off, toggle);
        off       += chunk;
        remaining -= chunk;
        toggle    ^= 1;
    }

    uint8_t status_pid;
    if (len == 0) status_pid = EHCI_QTD_PID_IN;
    else          status_pid = dir_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN;
    ehci_qtd_fill(&tds[n++], status_pid, 0, 0, 1);

     

    for (uint32_t i = 0; i < n - 1; i++)
        tds[i].next_qtd = (uint32_t)(c->td_pool_phys + (i + 1) * sizeof(ehci_qtd_t));
    tds[n - 1].next_qtd = EHCI_PTR_TERMINATE;
     


    for (uint32_t i = 1; i + 1 < n; i++)
        tds[i].alt_next_qtd = (uint32_t)(c->td_pool_phys + (uint64_t)(n - 1) * sizeof(ehci_qtd_t));

     


     




    if (!ehci_ensure_running(c)) return -4;
    ehci_async_set(c, false);
    c->ctrl_qh->ep_char = EHCI_QH_ADDR(dev_addr) | EHCI_QH_EPNUM(0) |
                          eps_bits | EHCI_QH_DTC | EHCI_QH_H |
                          EHCI_QH_MAXP(maxp0);
    c->ctrl_qh->ep_cap = qh_cap;    

     


    c->ctrl_qh->token        = 0;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    c->ctrl_qh->next_qtd     = (uint32_t)c->td_pool_phys;
    op_w32(c, EHCI_OP_USBSTS, 0x3Fu);    
    ehci_async_set(c, true);

     




    uint64_t deadline = ehci_rdtsc() + usb_tsc_per_ms() * 50ULL;    
    int result = -3;
    for (;;) {
        uint32_t next = c->ctrl_qh->next_qtd;
        uint32_t tok  = c->ctrl_qh->token;
        if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) { result = 0; break; }
        if (tok & EHCI_QTD_STATUS_HALTED) { result = -2; break; }
        if (ehci_rdtsc() > deadline) { result = -3; break; }
        __asm__ volatile ("pause" ::: "memory");
    }

    if (result != 0) ehci_snapshot_err(c, result, dev_addr, maxp0, n);
    ehci_async_set(c, false);
    c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

    if (result == 0 && data && len > 0 && dir_in) memcpy(data, data_dma, len);
    return result;
}

 





static int ehci_bulk_transfer_impl(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                   uint8_t ep_addr, uint16_t max_packet,
                                   void *data, uint32_t len, uint8_t *toggle_inout) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    uint32_t eps_bits, qh_cap;
    if (!ehci_qh_target(c, dev_addr, speed, false, &eps_bits, &qh_cap)) return -1;
    if (max_packet == 0) max_packet = (speed == USB_SPEED_HIGH) ? 512 : 64;
    if (len > EHCI_BULK_BUF_SIZE) return -1;

    bool    dir_in = (ep_addr & 0x80u) != 0;
    uint8_t ep_num = ep_addr & 0x0Fu;
    uint8_t toggle = toggle_inout ? *toggle_inout : 0;
    uint8_t pid    = dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;

    if (!ehci_ensure_running(c)) return -4;
    ehci_async_set(c, false);
    c->ctrl_qh->ep_char = EHCI_QH_ADDR(dev_addr) | EHCI_QH_EPNUM(ep_num) |
                          eps_bits | EHCI_QH_DTC | EHCI_QH_H |
                          EHCI_QH_MAXP(max_packet);
    c->ctrl_qh->ep_cap = qh_cap;

    if (!dir_in && data && len > 0) memcpy(c->bulk_buf, data, len);

    uint32_t remaining = len;
    uint32_t off       = 0;
    bool     first_iter = true;

    while (remaining > 0 || first_iter) {
        first_iter = false;
        ehci_qtd_t *tds = c->td_pool;
        uint32_t    n    = 0;

        if (len == 0) {
            ehci_qtd_fill(&tds[0], pid, 0, c->bulk_buf_phys, toggle);
            n = 1;
            toggle ^= 1;
        } else {
            while (remaining > 0 && n < EHCI_TD_POOL_COUNT) {
                uint16_t chunk = remaining > max_packet ? max_packet : (uint16_t)remaining;
                ehci_qtd_fill(&tds[n], pid, chunk, c->bulk_buf_phys + off, toggle);
                n++;
                off       += chunk;
                remaining -= chunk;
                toggle    ^= 1;
            }
        }

        for (uint32_t i = 0; i < n - 1; i++)
            tds[i].next_qtd = (uint32_t)(c->td_pool_phys + (i + 1) * sizeof(ehci_qtd_t));
        tds[n - 1].next_qtd = EHCI_PTR_TERMINATE;

        c->ctrl_qh->token        = 0;
        c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;
        c->ctrl_qh->next_qtd     = (uint32_t)c->td_pool_phys;
        op_w32(c, EHCI_OP_USBSTS, 0x3Fu);
        ehci_async_set(c, true);

        uint64_t deadline = ehci_rdtsc() + usb_tsc_per_ms() * 2000ULL;    
        int result = -3;
        for (;;) {
            uint32_t next = c->ctrl_qh->next_qtd;
            uint32_t tok  = c->ctrl_qh->token;
            if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) { result = 0; break; }
            if (tok & EHCI_QTD_STATUS_HALTED) { result = -2; break; }
            if (ehci_rdtsc() > deadline) { result = -3; break; }
            __asm__ volatile ("pause" ::: "memory");
        }
        if (result != 0) ehci_snapshot_err(c, result, dev_addr, (uint8_t)max_packet, n);
        ehci_async_set(c, false);
        c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
        c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

        if (result != 0) { if (toggle_inout) *toggle_inout = toggle; return result; }
        if (len == 0) break;
    }

    if (dir_in && data && len > 0) memcpy(data, c->bulk_buf, len);
    if (toggle_inout) *toggle_inout = toggle;
    return 0;
}

static int ehci_control_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                 uint8_t maxp0, const usb_setup_pkt_t *setup,
                                 void *data, uint16_t len) {
    uint64_t fl = usb_irq_save();
    int rc = ehci_control_transfer_impl(hcd, dev_addr, speed, maxp0, setup, data, len);
    usb_irq_restore(fl);
    return rc;
}

static int ehci_bulk_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                              uint8_t ep_addr, uint16_t max_packet,
                              void *data, uint32_t len, uint8_t *toggle_inout) {
    uint64_t fl = usb_irq_save();
    int rc = ehci_bulk_transfer_impl(hcd, dev_addr, speed, ep_addr, max_packet, data, len, toggle_inout);
    usb_irq_restore(fl);
    return rc;
}

 








static int ehci_bulk_in_poll_impl(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                  uint8_t ep_addr, uint16_t max_packet,
                                  void *data, uint32_t len, uint8_t *toggle_inout,
                                  uint32_t timeout_us) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    uint32_t eps_bits, qh_cap;
    if (!(ep_addr & 0x80u) || !data || len == 0) return -1;
    if (!ehci_qh_target(c, dev_addr, speed, false, &eps_bits, &qh_cap)) return -1;
    if (max_packet == 0) max_packet = (speed == USB_SPEED_HIGH) ? 512 : 64;
    if (len > EHCI_BULK_BUF_SIZE) len = EHCI_BULK_BUF_SIZE;
    if (len > (uint32_t)max_packet * EHCI_TD_POOL_COUNT) len = (uint32_t)max_packet * EHCI_TD_POOL_COUNT;

    uint8_t ep_num  = ep_addr & 0x0Fu;
    uint8_t toggle0 = toggle_inout ? (uint8_t)(*toggle_inout & 1u) : 0;

    ehci_qtd_t *tds = c->td_pool;
    uint16_t    chunks[EHCI_TD_POOL_COUNT];
    uint32_t    n = 0, off = 0, remaining = len;
    while (remaining > 0 && n < EHCI_TD_POOL_COUNT) {
        uint16_t chunk = remaining > max_packet ? max_packet : (uint16_t)remaining;
        ehci_qtd_fill(&tds[n], EHCI_QTD_PID_IN, chunk, c->bulk_buf_phys + off,
                      (uint8_t)(toggle0 ^ (n & 1u)));
        chunks[n] = chunk;
        n++; off += chunk; remaining -= chunk;
    }
    for (uint32_t i = 0; i + 1 < n; i++)
        tds[i].next_qtd = (uint32_t)(c->td_pool_phys + (uint64_t)(i + 1) * sizeof(ehci_qtd_t));
    tds[n - 1].next_qtd = EHCI_PTR_TERMINATE;

    if (!ehci_ensure_running(c)) return -4;
    ehci_async_set(c, false);
    c->ctrl_qh->ep_char = EHCI_QH_ADDR(dev_addr) | EHCI_QH_EPNUM(ep_num) |
                          eps_bits | EHCI_QH_DTC | EHCI_QH_H |
                          EHCI_QH_MAXP(max_packet);
    c->ctrl_qh->ep_cap       = qh_cap;
    c->ctrl_qh->token        = 0;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    c->ctrl_qh->next_qtd     = (uint32_t)c->td_pool_phys;
    op_w32(c, EHCI_OP_USBSTS, 0x3Fu);
    ehci_async_set(c, true);

    uint64_t deadline = ehci_rdtsc() + (usb_tsc_per_ms() * (uint64_t)timeout_us) / 1000ULL;
    for (;;) {
        uint32_t next = c->ctrl_qh->next_qtd;
        uint32_t tok  = c->ctrl_qh->token;
        if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) break;    
        if (tok & EHCI_QTD_STATUS_HALTED) break;
        if (ehci_rdtsc() > deadline) break;
        __asm__ volatile ("pause" ::: "memory");
    }
    ehci_async_set(c, false);         
    c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

    uint32_t got = 0, flips = 0;
    bool     halted = false;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t tok = tds[k].token;
        if (tok & EHCI_QTD_STATUS_ACTIVE) break;             
        if (tok & EHCI_QTD_STATUS_HALTED) { halted = true; break; }
        uint32_t rem = (tok >> 16) & 0x7FFFu;
        if (rem > chunks[k]) rem = chunks[k];
        got += (uint32_t)chunks[k] - rem;
        flips++;
        if (rem) break;                                       
    }
    if (toggle_inout) *toggle_inout = (uint8_t)(toggle0 ^ (flips & 1u));
    if (got) memcpy(data, c->bulk_buf, got);
    if (halted && got == 0) { ehci_snapshot_err(c, -2, dev_addr, (uint8_t)max_packet, n); return -2; }
    if (got == 0 && flips > 0) return -1000;           
    return (int)got;
}

static int ehci_bulk_in_poll(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                             uint8_t ep_addr, uint16_t max_packet,
                             void *data, uint32_t len, uint8_t *toggle_inout,
                             uint32_t timeout_us) {
    uint64_t fl = usb_irq_save();
    int rc = ehci_bulk_in_poll_impl(hcd, dev_addr, speed, ep_addr, max_packet, data, len,
                                    toggle_inout, timeout_us);
    usb_irq_restore(fl);
    return rc;
}

 






static int ehci_int_in_poll_impl(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                 uint8_t ep_addr, uint16_t max_packet,
                                 void *data, uint32_t len, uint8_t *toggle_inout,
                                 uint32_t timeout_us) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    uint32_t eps_bits, qh_cap;
    if (!(ep_addr & 0x80u) || !data) return -1;
    if (!ehci_qh_target(c, dev_addr, speed, false, &eps_bits, &qh_cap)) return -1;
    if (max_packet == 0) max_packet = 8;
    if (len == 0 || len > max_packet || len > EHCI_BULK_BUF_SIZE) return -1;

    uint8_t ep_num = ep_addr & 0x0Fu;
    uint8_t toggle = toggle_inout ? (uint8_t)(*toggle_inout & 1u) : 0;

    if (!ehci_ensure_running(c)) return -4;
    ehci_async_set(c, false);
    c->ctrl_qh->ep_char = EHCI_QH_ADDR(dev_addr) | EHCI_QH_EPNUM(ep_num) |
                          eps_bits | EHCI_QH_DTC | EHCI_QH_H |
                          EHCI_QH_MAXP(max_packet);
    c->ctrl_qh->ep_cap = qh_cap;

    ehci_qtd_t *td = &c->td_pool[0];
    ehci_qtd_fill(td, EHCI_QTD_PID_IN, (uint16_t)len, c->bulk_buf_phys, toggle);
    td->next_qtd = EHCI_PTR_TERMINATE;

    c->ctrl_qh->token        = 0;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    c->ctrl_qh->next_qtd     = (uint32_t)c->td_pool_phys;
    op_w32(c, EHCI_OP_USBSTS, 0x3Fu);
    ehci_async_set(c, true);

    uint64_t deadline = ehci_rdtsc() + (usb_tsc_per_ms() * (uint64_t)timeout_us) / 1000ULL;
    int      result   = 0;      
    uint32_t tok      = 0;
    for (;;) {
        uint32_t next = c->ctrl_qh->next_qtd;
        tok = c->ctrl_qh->token;
        if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) { result = 1; break; }
        if (tok & EHCI_QTD_STATUS_HALTED) { result = -2; break; }
        if (ehci_rdtsc() > deadline) { result = 0; break; }
        __asm__ volatile ("pause" ::: "memory");
    }
    ehci_async_set(c, false);
    c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

    if (result == 0) {
         

        tok = c->ctrl_qh->token;
        if (!(tok & EHCI_QTD_STATUS_ACTIVE) && !(tok & EHCI_QTD_STATUS_HALTED) &&
            (c->ctrl_qh->next_qtd & EHCI_PTR_TERMINATE) &&
            (((tok >> 16) & 0x7FFFu) < len))
            result = 1;
    }
    if (result < 0) return result;
    if (result == 0) return 0;

    uint32_t remaining = (tok >> 16) & 0x7FFFu;
    uint32_t got = (remaining <= len) ? (len - remaining) : 0;
    if (got == 0) return 0;
    memcpy(data, c->bulk_buf, got);
    if (toggle_inout) *toggle_inout = (uint8_t)(toggle ^ 1u);
    return (int)got;
}

 
static uint32_t g_int_cfg, g_int_polls, g_int_done, g_int_halt, g_int_qhtok;

 




void ehci_int_dbg(uint32_t out[5]) {
    out[0] = g_int_cfg; out[1] = g_int_polls; out[2] = g_int_done;
    out[3] = g_int_halt; out[4] = g_int_qhtok;
}

static inline ehci_qh_t  *int_qh(ehci_ctrl_t *c, int i)       { return (ehci_qh_t *)(c->int_mem + (uint32_t)i * 128u); }
static inline uint64_t    int_qh_phys(ehci_ctrl_t *c, int i)  { return c->int_mem_phys + (uint64_t)i * 128u; }
static inline ehci_qtd_t *int_qtd(ehci_ctrl_t *c, int i)      { return (ehci_qtd_t *)(c->int_mem + 512u + (uint32_t)i * 64u); }
static inline uint64_t    int_qtd_phys(ehci_ctrl_t *c, int i) { return c->int_mem_phys + 512u + (uint64_t)i * 64u; }
static inline uint8_t    *int_buf(ehci_ctrl_t *c, int i)      { return c->int_mem + 1024u + (uint32_t)i * 64u; }
static inline uint64_t    int_buf_phys(ehci_ctrl_t *c, int i) { return c->int_mem_phys + 1024u + (uint64_t)i * 64u; }

static bool ehci_periodic_set(ehci_ctrl_t *c, bool on) {
    uint32_t cmd  = op_r32(c, EHCI_OP_USBCMD);
    uint32_t want = on ? (cmd | EHCI_CMD_PSE) : (cmd & ~EHCI_CMD_PSE);
    if (want != cmd) op_w32(c, EHCI_OP_USBCMD, want);
    for (int i = 0; i < 40; i++) {
        bool running = (op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_PSS) != 0;
        if (running == on) return true;
        ehci_delay_ms(1);
    }
    DBG_MSG("EHCI", "periodic schedule state change timed out");
    return false;
}

 
static void ehci_periodic_init(ehci_ctrl_t *c) {
    c->frame_list = (uint32_t *)dma32_alloc(4096, &c->frame_list_phys);
    c->int_mem    = (uint8_t  *)dma32_alloc(4096, &c->int_mem_phys);
    if (!c->frame_list || !c->int_mem) {
        c->frame_list = NULL; c->int_mem = NULL;
        DBG_MSG("EHCI", "periodic schedule: DMA alloc failed (interrupt-split disabled)");
        return;
    }
    for (int i = 0; i < EHCI_INT_SLOTS; i++) {
        ehci_qh_t *qh = int_qh(c, i);
        qh->horiz_link   = (i + 1 < EHCI_INT_SLOTS) ? ((uint32_t)int_qh_phys(c, i + 1) | EHCI_PTR_TYPE_QH)
                                                    : EHCI_PTR_TERMINATE;
        qh->ep_char      = 0;
        qh->ep_cap       = (1u << 30);           
        qh->cur_qtd      = 0;
        qh->next_qtd     = EHCI_PTR_TERMINATE;
        qh->alt_next_qtd = EHCI_PTR_TERMINATE;
        qh->token        = 0;
    }
    for (uint32_t e = 0; e < 1024; e++)
        c->frame_list[e] = (e % EHCI_INT_FRAME_STRIDE == 0)
                           ? ((uint32_t)int_qh_phys(c, 0) | EHCI_PTR_TYPE_QH)
                           : EHCI_PTR_TERMINATE;
    op_w32(c, EHCI_OP_PERIODICLISTBASE, (uint32_t)c->frame_list_phys);
    if (!ehci_periodic_set(c, true)) {
        c->frame_list = NULL; c->int_mem = NULL;    
        DBG_MSG("EHCI", "periodic schedule: enable failed");
    } else {
        DBG_MSG("EHCI", "periodic schedule enabled");
    }
}

static void ehci_int_rearm(ehci_ctrl_t *c, int i, uint8_t toggle, uint32_t len) {
    ehci_qtd_t *td = int_qtd(c, i);
    ehci_qh_t  *qh = int_qh(c, i);
    ehci_qtd_fill(td, EHCI_QTD_PID_IN, (uint16_t)len, int_buf_phys(c, i), toggle);
    td->next_qtd = EHCI_PTR_TERMINATE;
     

    qh->token        = 0;
    qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    qh->next_qtd     = (uint32_t)int_qtd_phys(c, i);
}

 


static int ehci_int_in_poll_periodic(ehci_ctrl_t *c, uint8_t dev_addr, uint8_t speed,
                                     uint8_t ep_addr, uint16_t max_packet,
                                     void *data, uint32_t len, uint8_t *toggle_inout) {
    if (!(ep_addr & 0x80u) || !data) return -1;
    if (max_packet == 0) max_packet = 8;
    if (len == 0 || len > max_packet || len > 64) return -1;

    uint32_t eps_bits, qh_cap;
    if (!ehci_qh_target(c, dev_addr, speed, false, &eps_bits, &qh_cap)) return -1;
    uint8_t tt_hub  = c->tt_hub[dev_addr & 0x7F];
    uint8_t tt_port = c->tt_port[dev_addr & 0x7F];

    int si = -1, fi = -1;
    for (int i = 0; i < EHCI_INT_SLOTS; i++) {
        ehci_int_slot_t *sl = &c->islot[i];
        if (sl->used && sl->dev_addr == dev_addr && sl->ep_addr == ep_addr) { si = i; break; }
        if (!sl->used && fi < 0) fi = i;
    }
    bool configure = false;
    if (si < 0) {
        if (fi < 0) return -1;
        si = fi; configure = true;
    } else {
        ehci_int_slot_t *sl = &c->islot[si];
        if (sl->tt_hub != tt_hub || sl->tt_port != tt_port || sl->maxp != max_packet) configure = true;
    }

    ehci_qh_t       *qh = int_qh(c, si);
    ehci_qtd_t      *td = int_qtd(c, si);
    ehci_int_slot_t *sl = &c->islot[si];

    if (configure) {
        if (!ehci_ensure_running(c)) return -4;
        ehci_periodic_set(c, false);           
        qh->ep_cap = (1u << 30);               
        uint8_t tg = toggle_inout ? (uint8_t)(*toggle_inout & 1u) : 0;
        qh->ep_char = EHCI_QH_ADDR(dev_addr) | EHCI_QH_EPNUM(ep_addr & 0x0Fu) |
                      eps_bits | EHCI_QH_DTC | EHCI_QH_MAXP(max_packet);    
        qh->cur_qtd = 0;
        ehci_int_rearm(c, si, tg, len);
        qh->ep_cap = qh_cap | EHCI_INT_SMASK | (EHCI_INT_CMASK << 8);
        sl->used = true; sl->dev_addr = dev_addr; sl->ep_addr = ep_addr;
        sl->tt_hub = tt_hub; sl->tt_port = tt_port; sl->maxp = max_packet;
        sl->toggle = tg; sl->len = len;
        ehci_periodic_set(c, true);
        g_int_cfg++;
        DBG_VAL("EHCI", "interrupt-split slot configured, dev", dev_addr);
        return 0;
    }

    g_int_polls++;
    g_int_qhtok = qh->token & 0xFFu;
    uint32_t tok = td->token;
    if (tok & EHCI_QTD_STATUS_ACTIVE) return 0;       

    if (tok & EHCI_QTD_STATUS_HALTED) {
        g_int_halt++;
        g_int_qhtok = tok & 0xFFu;
        ehci_int_rearm(c, si, sl->toggle, sl->len);
        return -2;
    }

    uint32_t remaining = (tok >> 16) & 0x7FFFu;
    uint32_t got = (remaining <= sl->len) ? (sl->len - remaining) : 0;
    uint8_t  nt  = got ? (uint8_t)(sl->toggle ^ 1u) : sl->toggle;
    if (got) {
        uint32_t n = got < len ? got : len;
        memcpy(data, int_buf(c, si), n);
        g_int_done++;
    }
    sl->toggle = nt;
    if (toggle_inout) *toggle_inout = nt;
    ehci_int_rearm(c, si, nt, sl->len);
    return (int)got;
}

static int ehci_int_in_poll(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                            uint8_t ep_addr, uint16_t max_packet,
                            void *data, uint32_t len, uint8_t *toggle_inout,
                            uint32_t timeout_us) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    uint64_t fl = usb_irq_save();
    int rc;
    if (speed != USB_SPEED_HIGH && c->frame_list && c->int_mem)
        rc = ehci_int_in_poll_periodic(c, dev_addr, speed, ep_addr, max_packet, data, len, toggle_inout);
    else
        rc = ehci_int_in_poll_impl(hcd, dev_addr, speed, ep_addr, max_packet, data, len,
                                   toggle_inout, timeout_us);
    usb_irq_restore(fl);
    return rc;
}

 


static bool ehci_port_connected(usb_hcd_t *hcd, uint8_t port, bool *out_connected) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    if (port >= c->num_ports) return false;
    uint32_t val = op_r32(c, EHCI_OP_PORTSC_BASE + (uint32_t)port * 4);
    *out_connected = (val & EHCI_PORTSC_CCS) != 0;
    return true;
}

static const usb_hcd_ops_t g_ehci_ops = {
    .control_transfer = ehci_control_transfer,
    .port_reset       = ehci_port_reset,
    .bulk_transfer    = ehci_bulk_transfer,
    .set_route        = ehci_set_route,
    .port_connected   = ehci_port_connected,
    .int_in_poll      = ehci_int_in_poll,
    .bulk_in_poll     = ehci_bulk_in_poll,
    .supports_hubs    = true,
};

 


static void ehci_bios_handoff(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t hccparams) {
    uint32_t eecp = (hccparams >> 8) & 0xFFu;
    if (eecp < 0x40) return;    

    int guard = 0;
    while (eecp != 0 && guard++ < 16) {
        uint32_t cap = pci_read32(bus, dev, fn, (uint8_t)eecp);
        uint8_t  cap_id = (uint8_t)(cap & 0xFFu);
        uint8_t  next   = (uint8_t)((cap >> 8) & 0xFFu);

        if (cap_id == 0x01) {    
            DBG_VAL("EHCI", "BIOS handoff: legacy cap at", eecp);
            pci_write32(bus, dev, fn, (uint8_t)eecp, cap | (1u << 16));    

            bool released = false;
            for (int i = 0; i < 1000; i++) {
                uint32_t v = pci_read32(bus, dev, fn, (uint8_t)eecp);
                if (!(v & (1u << 24))) { released = true; break; }    
                ehci_delay_ms(1);
            }
            if (!released) {
                DBG_MSG("EHCI", "BIOS did not release ownership in time - forcing takeover");
                uint32_t v = pci_read32(bus, dev, fn, (uint8_t)eecp);
                pci_write32(bus, dev, fn, (uint8_t)eecp, v & ~(1u << 24));
            }

             


            pci_write32(bus, dev, fn, (uint8_t)(eecp + 4), 0);
            break;
        }
        eecp = next;
    }
}

 


static bool ehci_init_controller(uint8_t bus, uint8_t dev, uint8_t fn, uint64_t mmio_phys) {
    if (g_ehci_count >= EHCI_MAX_CONTROLLERS) return false;
    ehci_ctrl_t *c = &g_ehci[g_ehci_count];
    memset(c, 0, sizeof(*c));

    c->mmio_va = mmio_phys + DIRECT_MAP_OFFSET;

    uint8_t  caplength = (uint8_t)(mmio_r32(c->mmio_va) & 0xFFu);
    uint32_t hcsparams  = mmio_r32(c->mmio_va + EHCI_CAP_HCSPARAMS);
    uint32_t hccparams  = mmio_r32(c->mmio_va + EHCI_CAP_HCCPARAMS);
    c->op_va = c->mmio_va + caplength;

    c->pci_id = pci_read32(bus, dev, fn, 0x00);
    c->hcsparams = hcsparams; c->hccparams = hccparams;
    c->num_ports = (uint8_t)(hcsparams & 0x0Fu);
    if (c->num_ports == 0 || c->num_ports > EHCI_MAX_PORTS) c->num_ports = EHCI_MAX_PORTS;

    ehci_bios_handoff(bus, dev, fn, hccparams);

     
    if (!(op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED)) {
        op_w32(c, EHCI_OP_USBCMD, op_r32(c, EHCI_OP_USBCMD) & ~EHCI_CMD_RS);
        bool halted = false;
        for (int i = 0; i < 100; i++) {
            if (op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED) { halted = true; break; }
            ehci_delay_ms(1);
        }
        if (!halted) { DBG_MSG("EHCI", "controller won't halt, skip"); return false; }
    }

     
    op_w32(c, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    bool reset_ok = false;
    for (int i = 0; i < 200; i++) {
        if (!(op_r32(c, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) { reset_ok = true; break; }
        ehci_delay_ms(1);
    }
    if (!reset_ok) { DBG_MSG("EHCI", "HCRESET timeout, skip"); return false; }

     
    c->ctrl_qh = (ehci_qh_t *)dma32_alloc(4096, &c->ctrl_qh_phys);
    c->td_pool = (ehci_qtd_t *)dma32_alloc(EHCI_TD_POOL_COUNT * sizeof(ehci_qtd_t), &c->td_pool_phys);
    c->scratch = (uint8_t   *)dma32_alloc(4096, &c->scratch_phys);
    c->bulk_buf = (uint8_t  *)dma32_alloc(EHCI_BULK_BUF_SIZE, &c->bulk_buf_phys);
    if (!c->ctrl_qh || !c->td_pool || !c->scratch || !c->bulk_buf) {
        DBG_MSG("EHCI", "DMA allocation failed, skip");
        return false;
    }

     
    c->ctrl_qh->horiz_link    = (uint32_t)c->ctrl_qh_phys | EHCI_PTR_TYPE_QH;
    c->ctrl_qh->ep_char       = EHCI_QH_H;    
    c->ctrl_qh->ep_cap        = (1u << 30);
    c->ctrl_qh->cur_qtd       = 0;
    c->ctrl_qh->next_qtd      = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd  = EHCI_PTR_TERMINATE;
    c->ctrl_qh->token         = 0;

    op_w32(c, EHCI_OP_CTRLDSSEGMENT, 0);               
    op_w32(c, EHCI_OP_ASYNCLISTADDR, (uint32_t)c->ctrl_qh_phys);
    op_w32(c, EHCI_OP_USBINTR, 0);                      

     

     

    op_w32(c, EHCI_OP_USBCMD, EHCI_CMD_RS | (8u << 16));

    bool started = false;
    for (int i = 0; i < 100; i++) {
        if (!(op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED)) { started = true; break; }
        ehci_delay_ms(1);
    }
    if (!started) { DBG_MSG("EHCI", "controller didn't start (USBSTS.HCHalted stuck), skip"); return false; }

     
    ehci_periodic_init(c);

     


    op_w32(c, EHCI_OP_CONFIGFLAG, 1);
    ehci_delay_ms(5);

     





    for (uint8_t pn = 0; pn < c->num_ports; pn++) {
        uint32_t reg = EHCI_OP_PORTSC_BASE + (uint32_t)pn * 4;
        uint32_t v = op_r32(c, reg);
        if (!(v & EHCI_PORTSC_PP))
            op_w32(c, reg, (v & ~EHCI_PORTSC_RWC_MASK) | EHCI_PORTSC_PP);
    }
    ehci_delay_ms(20);
    ehci_delay_ms(150);

    c->hcd.ops       = &g_ehci_ops;
    c->hcd.priv      = c;
    c->hcd.name      = "ehci";
    c->hcd.num_ports = c->num_ports;

    usb_register_hcd(&c->hcd);
    g_ehci_count++;
    return true;
}

 


void ehci_pci_scan(void) {
    DBG_MSG("EHCI", "=== ehci_pci_scan ===");
    int found = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF) { if (fn == 0) break; continue; }

                uint32_t cls = pci_read32((uint8_t)bus, dev, fn, 0x08);
                uint8_t class_code = (uint8_t)(cls >> 24);
                uint8_t subclass   = (uint8_t)(cls >> 16);
                uint8_t prog_if    = (uint8_t)(cls >> 8);

                if (class_code != 0x0C || subclass != 0x03 || prog_if != 0x20) {
                    uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, dev, fn, 0x0C) >> 16);
                    if (fn == 0 && !(hdr & 0x80)) break;
                    continue;
                }

                DBG_VAL("EHCI", "found EHCI at bus", bus);
                DBG_VAL("EHCI", "  device", dev);
                DBG_VAL("EHCI", "  vendor:device", id);

                uint32_t pcmd = pci_read32((uint8_t)bus, dev, fn, 0x04);
                pci_write32((uint8_t)bus, dev, fn, 0x04, pcmd | 0x06);    

                 


                uint32_t bar0 = pci_read32((uint8_t)bus, dev, fn, 0x10);
                if (bar0 & 1) {
                    DBG_MSG("EHCI", "BAR0 is I/O space (not per spec), skip");
                    continue;
                }
                uint64_t mmio_phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
                if (((bar0 >> 1) & 0x3u) == 0x2u) {    
                    uint32_t bar1 = pci_read32((uint8_t)bus, dev, fn, 0x14);
                    mmio_phys |= ((uint64_t)bar1 << 32);
                }
                DBG_VAL("EHCI", "  mmio_phys", mmio_phys);

                if (ehci_init_controller((uint8_t)bus, dev, fn, mmio_phys)) found++;

                uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, dev, fn, 0x0C) >> 16);
                if (fn == 0 && !(hdr & 0x80)) break;
            }
        }
    }

    DBG_VAL("EHCI", "controllers found", (uint64_t)found);
    DBG_MSG("EHCI", "=== ehci_pci_scan done ===");
}
