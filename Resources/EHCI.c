/*
 * EHCI.c — драйвер EHCI (Enhanced Host Controller Interface), USB 2.0
 * High-Speed (480 Мбит/с).
 *
 * Область покрытия этой версии (сознательно, см. также USB.h):
 *   - Регистры: Capability + Operational (MMIO, BAR0).
 *   - BIOS->OS handoff через Extended Capabilities (USB Legacy Support,
 *     EECP) — без этого некоторые прошивки продолжают дёргать
 *     контроллер через SMI параллельно с нами.
 *   - Полный сброс контроллера, запуск ТОЛЬКО асинхронного расписания
 *     (Async Schedule) — периодическое (Periodic Schedule/Frame List)
 *     не поднимается вообще: оно нужно для isochronous/interrupt
 *     эндпоинтов, а этот драйвер (как и UHCI.c рядом) пока умеет только
 *     control-передачи, которые идут через Async Schedule.
 *   - Один постоянный async QH (по тому же принципу, что постоянная QH
 *     в UHCI.c) — зациклен сам на себя (horiz_link -> сам QH), на его
 *     next_qtd подвешивается свежепостроенная цепочка qTD на каждый
 *     control_transfer().
 *   - Порты: сброс, определение High-Speed (PORTSC.PE встаёт сама после
 *     аппаратного сброса, если устройство HS) — если PE не встал, но
 *     устройство подключено, порт считается full/low-speed и НЕ наш:
 *     отдаём владение companion-контроллеру через PORTSC.PortOwner
 *     (см. EHCI.h про порядок вызова относительно uhci_pci_scan()).
 *
 * НЕ реализовано (сознательная граница первой версии):
 *   - Split-транзакции (S-mask/C-mask/Hub Addr/Port Num в Endpoint
 *     Capabilities) — то есть full/low-speed устройства ЗА USB-хабом,
 *     висящим на high-speed порту EHCI, эта версия не потянет; такое
 *     устройство просто не ответит на control_transfer (таймаут). Прямо
 *     подключенные full/low-speed устройства работают нормально — они
 *     передаются companion-контроллеру ДО того, как до них доходит
 *     очередь через EHCI.
 *   - Periodic Schedule (interrupt/isochronous эндпоинты) — не нужен
 *     для перечисления устройств (control-transfer-only), появится
 *     вместе с классовыми драйверами (HID и т.п.), см. USB.h.
 *   - 64-битный DMA (CTRLDSSEGMENT всегда 0) — не требуется, вся память
 *     этого ядра при желании прекрасно достаётся из первых 4 ГБ
 *     (см. pmm_alloc_frames_range в MemoryControl.c).
 *
 * Источник: Intel "Enhanced Host Controller Interface Specification for
 * Universal Serial Bus" rev 1.0 — регистры, форматы QH/qTD, EECP.
 */
#include "EHCI.h"
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Внешние зависимости ядра — тот же паттерн, что в AHCI.c/UHCI.c рядом.
 * ========================================================================= */
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
#define DMA32_MAX_PHYS 0x100000000ULL   /* CTRLDSSEGMENT=0 -> все указатели 32-битные, см. шапку файла */

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

/* MMIO — тот же приём, что в AHCI.c: DIRECT_MAP уже покрывает всю
 * физическую память 1:1 со сдвигом DIRECT_MAP_OFFSET, поэтому регистры
 * читаются/пишутся напрямую по va = phys + DIRECT_MAP_OFFSET, без
 * отдельного vmm_map. mfence — чтобы компилятор/CPU не переупорядочили
 * запись относительно последующего опроса статуса. */
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
static void ehci_delay_ms(uint32_t ms) {
    const uint64_t tsc_per_ms = 3000000ULL;
    uint64_t target = ehci_rdtsc() + tsc_per_ms * ms;
    while (ehci_rdtsc() < target) __asm__ volatile ("pause" ::: "memory");
}

/* =========================================================================
 * Регистры (EHCI spec §2)
 * ========================================================================= */
#define EHCI_CAP_CAPLENGTH   0x00   /* 8-bit  */
#define EHCI_CAP_HCIVERSION  0x02   /* 16-bit */
#define EHCI_CAP_HCSPARAMS   0x04   /* 32-bit */
#define EHCI_CAP_HCCPARAMS   0x08   /* 32-bit */

#define EHCI_OP_USBCMD           0x00
#define EHCI_OP_USBSTS           0x04
#define EHCI_OP_USBINTR          0x08
#define EHCI_OP_FRINDEX          0x0C
#define EHCI_OP_CTRLDSSEGMENT    0x10
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR    0x18
#define EHCI_OP_CONFIGFLAG       0x40
#define EHCI_OP_PORTSC_BASE      0x44

#define EHCI_CMD_RS         0x00000001u   /* Run/Stop */
#define EHCI_CMD_HCRESET    0x00000002u
#define EHCI_CMD_PSE        0x00000010u   /* Periodic Schedule Enable — НЕ используем */
#define EHCI_CMD_ASE        0x00000020u   /* Async Schedule Enable */

#define EHCI_STS_HALTED     0x00001000u

#define EHCI_PORTSC_CCS     0x00000001u   /* Current Connect Status (RO) */
#define EHCI_PORTSC_CSC     0x00000002u   /* Connect Status Change (R/WC) */
#define EHCI_PORTSC_PE      0x00000004u   /* Port Enable (R/W) */
#define EHCI_PORTSC_PEC     0x00000008u   /* Port Enable Change (R/WC) */
#define EHCI_PORTSC_OCC     0x00000020u   /* Over-current Change (R/WC) */
#define EHCI_PORTSC_PR      0x00000100u   /* Port Reset (R/W) */
#define EHCI_PORTSC_OWNER   0x00002000u   /* Port Owner: 1 = отдать companion-контроллеру */
/* RWC-биты — обнулять при read-modify-write, если не хотим их явно подтвердить */
#define EHCI_PORTSC_RWC_MASK (EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC)

/* =========================================================================
 * Queue Head / qTD (EHCI spec §3.5/§3.6)
 * ========================================================================= */
#define EHCI_PTR_TERMINATE 0x00000001u
#define EHCI_PTR_TYPE_QH   0x00000002u   /* Typ, биты 2:1 = 01 для QH */

#define EHCI_EPS_FULL 0u
#define EHCI_EPS_LOW  1u
#define EHCI_EPS_HIGH 2u

typedef struct {
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buf[5];
} ehci_qtd_t;   /* 32 байта — естественное выравнивание совпадает с требуемым */

typedef struct {
    volatile uint32_t horiz_link;
    volatile uint32_t ep_char;
    volatile uint32_t ep_cap;
    volatile uint32_t cur_qtd;
    /* qTD overlay — сюда HW сам копирует поля исполняемого сейчас qTD */
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buf[5];
    uint32_t reserved[3];   /* добивка — не аппаратные поля */
} ehci_qh_t;

#define EHCI_QTD_STATUS_XACTERR  (1u << 3)
#define EHCI_QTD_STATUS_BABBLE   (1u << 4)
#define EHCI_QTD_STATUS_DBUFERR  (1u << 5)
#define EHCI_QTD_STATUS_HALTED   (1u << 6)
#define EHCI_QTD_STATUS_ACTIVE   (1u << 7)

#define EHCI_QTD_PID_OUT   0u
#define EHCI_QTD_PID_IN    1u
#define EHCI_QTD_PID_SETUP 2u

#define EHCI_TD_POOL_COUNT 128   /* ровно одна страница, см. UHCI.c — те же соображения по размеру */
#define EHCI_BULK_BUF_SIZE (64u * 1024u)   /* см. UHCI_BULK_BUF_SIZE — тот же приём */

/* =========================================================================
 * Контекст контроллера
 * ========================================================================= */
#define EHCI_MAX_CONTROLLERS 4
#define EHCI_MAX_PORTS 15   /* HCSPARAMS.N_PORTS — максимум по спеке */

typedef struct {
    uint64_t   mmio_va;      /* виртуальный адрес начала capability-регистров */
    uint64_t   op_va;        /* mmio_va + CAPLENGTH — начало operational-регистров */
    uint8_t    num_ports;

    ehci_qh_t   *ctrl_qh;
    uint64_t     ctrl_qh_phys;
    ehci_qtd_t  *td_pool;
    uint64_t     td_pool_phys;
    uint8_t     *scratch;
    uint64_t     scratch_phys;

    uint8_t     *bulk_buf;      /* см. UHCI.c — отдельный DMA-буфер под bulk_transfer */
    uint64_t     bulk_buf_phys;

    usb_hcd_t  hcd;
} ehci_ctrl_t;

static ehci_ctrl_t g_ehci[EHCI_MAX_CONTROLLERS];
static int         g_ehci_count = 0;

static inline uint32_t op_r32(ehci_ctrl_t *c, uint32_t off) { return mmio_r32(c->op_va + off); }
static inline void     op_w32(ehci_ctrl_t *c, uint32_t off, uint32_t v) { mmio_w32(c->op_va + off, v); }

/* =========================================================================
 * port_reset — см. подробности в шапке файла про companion-хендофф
 * ========================================================================= */
static bool ehci_port_reset(usb_hcd_t *hcd, uint8_t port, uint8_t *out_speed) {
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    if (port >= c->num_ports) return false;
    uint32_t reg = EHCI_OP_PORTSC_BASE + (uint32_t)port * 4;

    uint32_t val = op_r32(c, reg);
    if (!(val & EHCI_PORTSC_CCS)) return false;   /* порт пуст */

    /* USB2.0 spec §7.1.7.5: держим Port Reset минимум 50мс. */
    val &= ~EHCI_PORTSC_RWC_MASK;
    val &= ~EHCI_PORTSC_PE;
    op_w32(c, reg, val | EHCI_PORTSC_PR);
    ehci_delay_ms(50);

    val = op_r32(c, reg) & ~EHCI_PORTSC_RWC_MASK;
    op_w32(c, reg, val & ~EHCI_PORTSC_PR);

    /* Аппаратура обязана сама снять PR в течение ~2мс после того, как мы
     * его сняли программно (EHCI spec §2.3.9) — ждём с запасом. */
    bool cleared = false;
    for (int i = 0; i < 20; i++) {
        if (!(op_r32(c, reg) & EHCI_PORTSC_PR)) { cleared = true; break; }
        ehci_delay_ms(1);
    }
    if (!cleared) {
        DBG_VAL("EHCI", "port reset didn't clear (аппаратура зависла?), port", port);
        return false;
    }

    ehci_delay_ms(10);
    val = op_r32(c, reg);
    if (!(val & EHCI_PORTSC_CCS)) return false;   /* отключили прямо во время reset */

    if (val & EHCI_PORTSC_PE) {
        *out_speed = USB_SPEED_HIGH;
        return true;
    }

    /* Не high-speed — эта версия драйвера такие устройства не обслуживает
     * (нет split-транзакций), отдаём порт companion-контроллеру. Он
     * подхватит его на своём сканировании портов (см. порядок вызовов в
     * usb_init(): ehci_pci_scan() строго до uhci_pci_scan()). */
    val = op_r32(c, reg) & ~EHCI_PORTSC_RWC_MASK;
    op_w32(c, reg, val | EHCI_PORTSC_OWNER);
    DBG_VAL("EHCI", "port not high-speed, handed off to companion, port", port);
    return false;
}

/* =========================================================================
 * control_transfer — только High Speed (см. шапку файла)
 * ========================================================================= */
static void ehci_qtd_fill(ehci_qtd_t *td, uint8_t pid, uint16_t len,
                          uint64_t buf_phys, uint8_t toggle) {
    td->token = EHCI_QTD_STATUS_ACTIVE |
               ((uint32_t)pid << 8) |
               (3u << 10)          /* CERR = 3 */ |
               (0u << 12)          /* Current Page = 0 — буфер не пересекает границу страницы */ |
               ((uint32_t)len << 16) |
               ((uint32_t)toggle << 31);
    td->buf[0] = (uint32_t)buf_phys;
    td->buf[1] = 0; td->buf[2] = 0; td->buf[3] = 0; td->buf[4] = 0;
    td->next_qtd     = EHCI_PTR_TERMINATE;
    td->alt_next_qtd = EHCI_PTR_TERMINATE;
}

static int ehci_control_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                 uint8_t maxp0, const usb_setup_pkt_t *setup,
                                 void *data, uint16_t len) {
    if (speed != USB_SPEED_HIGH) return -1;   /* full/low speed — через UHCI-компаньон */
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    if (maxp0 == 0) maxp0 = 64;
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

    /* Линкуем цепочку через next_qtd (Typ-биты для qTD.next_qtd не имеют
     * смысла — только адрес + T-бит). */
    for (uint32_t i = 0; i < n - 1; i++)
        tds[i].next_qtd = (uint32_t)(c->td_pool_phys + (i + 1) * sizeof(ehci_qtd_t));
    tds[n - 1].next_qtd = EHCI_PTR_TERMINATE;

    /* Обновляем Endpoint Characteristics под текущее устройство/maxpacket
     * (постоянная QH используется для ВСЕХ устройств по очереди, не
     * только для одного — переписываем перед каждой транзакцией). */
    c->ctrl_qh->ep_char = ((uint32_t)dev_addr & 0x7Fu) |
                          ((uint32_t)EHCI_EPS_HIGH << 11) |
                          (1u << 13)   /* DTC */ |
                          (1u << 14)   /* H — голова, единственная QH */ |
                          (((uint32_t)maxp0 & 0x7FFu) << 16);
    c->ctrl_qh->ep_cap = (1u << 30);   /* Mult = 1 */

    /* Подвешиваем цепочку на QH — оборвём текущий overlay (Active=0),
     * next_qtd укажет на первый qTD; аппаратура сама "зафетчит" его на
     * следующем проходе async-расписания (см. подробности в шапке файла). */
    c->ctrl_qh->token        = 0;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    c->ctrl_qh->next_qtd     = (uint32_t)c->td_pool_phys;

    /* См. подробный комментарий у того же места в UHCI.c: 500мс тут было
     * слишком много специально для периодического HID-опроса (см.
     * Resources/USBHID.c) — неотвечающее устройство блокировало систему
     * на полсекунды каждые ~30мс. 50мс с запасом хватает рабочему
     * устройству, ограничивает ущерб от неотвечающего. */
    uint64_t deadline = ehci_rdtsc() + 3000000ULL * 50;   /* ~50мс */
    int result = -3;
    for (;;) {
        uint32_t next = c->ctrl_qh->next_qtd;
        uint32_t tok  = c->ctrl_qh->token;
        if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) { result = 0; break; }
        if (tok & EHCI_QTD_STATUS_HALTED) { result = -2; break; }
        if (ehci_rdtsc() > deadline) { result = -3; break; }
        __asm__ volatile ("pause" ::: "memory");
    }

    c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

    if (result == 0 && data && len > 0 && dir_in) memcpy(data, data_dma, len);
    return result;
}

/* =========================================================================
 * bulk_transfer — см. подробный комментарий у uhci_bulk_transfer в
 * UHCI.c, тот же приём (батчи по td_pool, персистентный bulk_buf,
 * Data Toggle между вызовами) — только структуры EHCI (qTD/QH-overlay)
 * вместо UHCI TD/QH.
 * ========================================================================= */
static int ehci_bulk_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                              uint8_t ep_addr, uint16_t max_packet,
                              void *data, uint32_t len, uint8_t *toggle_inout) {
    if (speed != USB_SPEED_HIGH) return -1;   /* full/low speed — через UHCI-компаньон */
    ehci_ctrl_t *c = (ehci_ctrl_t *)hcd->priv;
    if (max_packet == 0) max_packet = 512;
    if (len > EHCI_BULK_BUF_SIZE) return -1;

    bool    dir_in = (ep_addr & 0x80u) != 0;
    uint8_t ep_num = ep_addr & 0x0Fu;
    uint8_t toggle = toggle_inout ? *toggle_inout : 0;
    uint8_t pid    = dir_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;

    c->ctrl_qh->ep_char = ((uint32_t)dev_addr & 0x7Fu) |
                          ((uint32_t)ep_num << 8) |
                          ((uint32_t)EHCI_EPS_HIGH << 11) |
                          (1u << 13) | (1u << 14) |
                          (((uint32_t)max_packet & 0x7FFu) << 16);
    c->ctrl_qh->ep_cap = (1u << 30);

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

        uint64_t deadline = ehci_rdtsc() + 3000000ULL * 2000;   /* ~2с на батч */
        int result = -3;
        for (;;) {
            uint32_t next = c->ctrl_qh->next_qtd;
            uint32_t tok  = c->ctrl_qh->token;
            if ((next & EHCI_PTR_TERMINATE) && !(tok & EHCI_QTD_STATUS_ACTIVE)) { result = 0; break; }
            if (tok & EHCI_QTD_STATUS_HALTED) { result = -2; break; }
            if (ehci_rdtsc() > deadline) { result = -3; break; }
            __asm__ volatile ("pause" ::: "memory");
        }
        c->ctrl_qh->next_qtd     = EHCI_PTR_TERMINATE;
        c->ctrl_qh->alt_next_qtd = EHCI_PTR_TERMINATE;

        if (result != 0) { if (toggle_inout) *toggle_inout = toggle; return result; }
        if (len == 0) break;
    }

    if (dir_in && data && len > 0) memcpy(data, c->bulk_buf, len);
    if (toggle_inout) *toggle_inout = toggle;
    return 0;
}

static const usb_hcd_ops_t g_ehci_ops = {
    .control_transfer = ehci_control_transfer,
    .port_reset       = ehci_port_reset,
    .bulk_transfer    = ehci_bulk_transfer,
};

/* =========================================================================
 * BIOS->OS handoff (EHCI spec §2.2.4, Extended Capabilities / EECP)
 * ========================================================================= */
static void ehci_bios_handoff(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t hccparams) {
    uint32_t eecp = (hccparams >> 8) & 0xFFu;
    if (eecp < 0x40) return;   /* нет расширенных возможностей вообще */

    int guard = 0;
    while (eecp != 0 && guard++ < 16) {
        uint32_t cap = pci_read32(bus, dev, fn, (uint8_t)eecp);
        uint8_t  cap_id = (uint8_t)(cap & 0xFFu);
        uint8_t  next   = (uint8_t)((cap >> 8) & 0xFFu);

        if (cap_id == 0x01) {   /* USB Legacy Support Capability */
            DBG_VAL("EHCI", "BIOS handoff: legacy cap at", eecp);
            pci_write32(bus, dev, fn, (uint8_t)eecp, cap | (1u << 16));   /* HC OS Owned = 1 */

            bool released = false;
            for (int i = 0; i < 1000; i++) {
                uint32_t v = pci_read32(bus, dev, fn, (uint8_t)eecp);
                if (!(v & (1u << 24))) { released = true; break; }   /* HC BIOS Owned == 0 */
                ehci_delay_ms(1);
            }
            if (!released) {
                DBG_MSG("EHCI", "BIOS didn't release ownership in time — форсируем");
                uint32_t v = pci_read32(bus, dev, fn, (uint8_t)eecp);
                pci_write32(bus, dev, fn, (uint8_t)eecp, v & ~(1u << 24));
            }

            /* USBLEGCTLSTS (следующий dword) — гасим все SMI-триггеры,
             * чтобы прошивка больше не пыталась вмешиваться параллельно
             * с нами (тот же приём, что LEGSUP в UHCI.c). */
            pci_write32(bus, dev, fn, (uint8_t)(eecp + 4), 0);
            break;
        }
        eecp = next;
    }
}

/* =========================================================================
 * Инициализация одного контроллера
 * ========================================================================= */
static bool ehci_init_controller(uint8_t bus, uint8_t dev, uint8_t fn, uint64_t mmio_phys) {
    if (g_ehci_count >= EHCI_MAX_CONTROLLERS) return false;
    ehci_ctrl_t *c = &g_ehci[g_ehci_count];
    memset(c, 0, sizeof(*c));

    c->mmio_va = mmio_phys + DIRECT_MAP_OFFSET;

    uint8_t  caplength = (uint8_t)(mmio_r32(c->mmio_va) & 0xFFu);
    uint32_t hcsparams  = mmio_r32(c->mmio_va + EHCI_CAP_HCSPARAMS);
    uint32_t hccparams  = mmio_r32(c->mmio_va + EHCI_CAP_HCCPARAMS);
    c->op_va = c->mmio_va + caplength;

    c->num_ports = (uint8_t)(hcsparams & 0x0Fu);
    if (c->num_ports == 0 || c->num_ports > EHCI_MAX_PORTS) c->num_ports = EHCI_MAX_PORTS;

    ehci_bios_handoff(bus, dev, fn, hccparams);

    /* Останавливаем, если контроллер уже был запущен прошивкой */
    if (!(op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED)) {
        op_w32(c, EHCI_OP_USBCMD, op_r32(c, EHCI_OP_USBCMD) & ~EHCI_CMD_RS);
        bool halted = false;
        for (int i = 0; i < 100; i++) {
            if (op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED) { halted = true; break; }
            ehci_delay_ms(1);
        }
        if (!halted) { DBG_MSG("EHCI", "controller won't halt, skip"); return false; }
    }

    /* Полный сброс */
    op_w32(c, EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    bool reset_ok = false;
    for (int i = 0; i < 200; i++) {
        if (!(op_r32(c, EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) { reset_ok = true; break; }
        ehci_delay_ms(1);
    }
    if (!reset_ok) { DBG_MSG("EHCI", "HCRESET timeout, skip"); return false; }

    /* DMA-структуры: постоянная async QH, пул qTD, scratch под setup+данные */
    c->ctrl_qh = (ehci_qh_t *)dma32_alloc(4096, &c->ctrl_qh_phys);
    c->td_pool = (ehci_qtd_t *)dma32_alloc(EHCI_TD_POOL_COUNT * sizeof(ehci_qtd_t), &c->td_pool_phys);
    c->scratch = (uint8_t   *)dma32_alloc(4096, &c->scratch_phys);
    c->bulk_buf = (uint8_t  *)dma32_alloc(EHCI_BULK_BUF_SIZE, &c->bulk_buf_phys);
    if (!c->ctrl_qh || !c->td_pool || !c->scratch || !c->bulk_buf) {
        DBG_MSG("EHCI", "DMA allocation failed, skip");
        return false;
    }

    /* QH зациклена сама на себя — единственный элемент async-списка. */
    c->ctrl_qh->horiz_link    = (uint32_t)c->ctrl_qh_phys | EHCI_PTR_TYPE_QH;
    c->ctrl_qh->ep_char       = (1u << 14);   /* H=1, остальное дозаполнится в control_transfer */
    c->ctrl_qh->ep_cap        = (1u << 30);
    c->ctrl_qh->cur_qtd       = 0;
    c->ctrl_qh->next_qtd      = EHCI_PTR_TERMINATE;
    c->ctrl_qh->alt_next_qtd  = EHCI_PTR_TERMINATE;
    c->ctrl_qh->token         = 0;

    op_w32(c, EHCI_OP_CTRLDSSEGMENT, 0);              /* весь DMA этого драйвера < 4ГБ */
    op_w32(c, EHCI_OP_ASYNCLISTADDR, (uint32_t)c->ctrl_qh_phys);
    op_w32(c, EHCI_OP_USBINTR, 0);                     /* без прерываний — polling, как везде в этом ядре */

    /* Периодическое расписание сознательно не поднимаем (см. шапку файла) —
     * только Run/Stop + Async Schedule Enable. */
    op_w32(c, EHCI_OP_USBCMD, EHCI_CMD_RS | EHCI_CMD_ASE);

    bool started = false;
    for (int i = 0; i < 100; i++) {
        if (!(op_r32(c, EHCI_OP_USBSTS) & EHCI_STS_HALTED)) { started = true; break; }
        ehci_delay_ms(1);
    }
    if (!started) { DBG_MSG("EHCI", "controller didn't start (USBSTS.HCHalted stuck), skip"); return false; }

    /* Забираем все порты себе (маршрутизация по умолчанию — companion) —
     * без этого порты остаются "молчаливо" привязаны к companion-
     * контроллеру и до High-Speed устройств EHCI просто не достучится. */
    op_w32(c, EHCI_OP_CONFIGFLAG, 1);
    ehci_delay_ms(5);

    c->hcd.ops       = &g_ehci_ops;
    c->hcd.priv      = c;
    c->hcd.name      = "ehci";
    c->hcd.num_ports = c->num_ports;

    usb_register_hcd(&c->hcd);
    g_ehci_count++;
    return true;
}

/* =========================================================================
 * PCI-сканирование — class 0x0C, subclass 0x03, prog-if 0x20 (EHCI).
 * ========================================================================= */
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
                pci_write32((uint8_t)bus, dev, fn, 0x04, pcmd | 0x06);   /* Memory Space + Bus Master */

                /* BAR0 — MMIO, может быть 32- или 64-битным (бит2:1 типа
                 * в BAR: 00=32-bit, 10=64-bit). У большинства EHCI это
                 * 32-битный BAR, но проверяем честно. */
                uint32_t bar0 = pci_read32((uint8_t)bus, dev, fn, 0x10);
                if (bar0 & 1) {
                    DBG_MSG("EHCI", "BAR0 is I/O space?! — не по спеке, skip");
                    continue;
                }
                uint64_t mmio_phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
                if (((bar0 >> 1) & 0x3u) == 0x2u) {   /* 64-битный BAR — верхняя половина в следующем dword */
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
