/*
 * UHCI.c — драйвер UHCI (Intel Universal Host Controller Interface)
 *
 * Регистры — только I/O-порты (BAR4, всегда IO space), поэтому весь
 * контроллер работает без MMIO-маппинга — как IDE.c рядом. Работает
 * синхронно/через polling (нет ни одной строчки про IRQ) — тот же стиль,
 * что у AHCI.c/IDE.c: устанавливаем передачу, крутим цикл, ждём флаг
 * завершения/ошибки в дескрипторе, читаем результат.
 *
 * Модель передачи: ОДНА постоянная Queue Head (ctrl_qh), подвешенная во
 * ВСЕ 1024 слота Frame List сразу (частый приём для минимальных
 * реализаций — control-транзакции не периодические по своей природе, но
 * аппаратура UHCI всё равно обслуживает их только через Frame List, и
 * "быть в каждом слоте" — самый простой способ гарантировать, что до
 * очереди дойдёт дело на следующем же кадре). Список Transfer Descriptor
 * строится заново на каждый control_transfer() и подвешивается на
 * ctrl_qh.element — поддержки нескольких одновременных транзакций нет
 * (не нужна: USB.c дергает control_transfer строго последовательно).
 *
 * Источники: Intel "Universal Host Controller Interface (UHCI) Design
 * Guide" rev 1.1 — регистры, формат QH/TD, разводка PORTSC.
 */
#include "UHCI.h"
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Внешние зависимости ядра (см. AHCI.c/IDE.c рядом — тот же паттерн:
 * общего заголовка ядра тут нет, каждый файл сам объявляет нужные extern).
 * ========================================================================= */
extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);
extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

typedef uint64_t phys_addr_t;
/* Верхняя граница обязательна — UHCI аппаратно только 32-битный DMA
 * (все поля указателей в QH/TD — 32 бита). См. MemoryControl.c. */
extern phys_addr_t pmm_alloc_frames_range(size_t count, size_t align,
                                          phys_addr_t min_phys, phys_addr_t max_phys);
extern void        pmm_free_frames(phys_addr_t pa, size_t count);

#ifndef DIRECT_MAP_OFFSET
#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#endif
#define DMA32_MIN_PHYS 0x100000ULL         /* не лезем в conventional memory (0-1МБ) */
#define DMA32_MAX_PHYS 0x100000000ULL      /* строго < 4 ГБ — аппаратный лимит UHCI */

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

static inline void     outb(uint16_t p, uint8_t  v) { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void     outw(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void     outl(uint16_t p, uint32_t v) { __asm__ volatile ("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t  inb (uint16_t p) { uint8_t  v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw (uint16_t p) { uint16_t v; __asm__ volatile ("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl (uint16_t p) { uint32_t v; __asm__ volatile ("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }

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

static inline uint64_t uhci_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static void uhci_delay_ms(uint32_t ms) {
    const uint64_t tsc_per_ms = 3000000ULL; /* ~3 ГГц, приближённо — см. USB.c */
    uint64_t target = uhci_rdtsc() + tsc_per_ms * ms;
    while (uhci_rdtsc() < target) __asm__ volatile ("pause" ::: "memory");
}

/* =========================================================================
 * Регистры UHCI (I/O, смещения от io_base) — Design Guide §2.1
 * ========================================================================= */
#define UHCI_REG_USBCMD    0x00  /* 16-bit */
#define UHCI_REG_USBSTS    0x02  /* 16-bit */
#define UHCI_REG_USBINTR   0x04  /* 16-bit */
#define UHCI_REG_FRNUM     0x06  /* 16-bit */
#define UHCI_REG_FRBASEADD 0x08  /* 32-bit */
#define UHCI_REG_SOFMOD    0x0C  /* 8-bit  */
#define UHCI_REG_PORTSC1   0x10  /* 16-bit */
#define UHCI_REG_PORTSC2   0x12  /* 16-bit */

#define UHCI_CMD_RS        0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET   0x0002
#define UHCI_CMD_GRESET    0x0004
#define UHCI_CMD_CF        0x0040  /* Configure Flag */
#define UHCI_CMD_MAXP      0x0080  /* Max Packet (64 байта) */

#define UHCI_STS_HCHALTED  0x0020

#define UHCI_PORTSC_CCS    0x0001  /* Current Connect Status (RO) */
#define UHCI_PORTSC_CSC    0x0002  /* Connect Status Change (R/WC) */
#define UHCI_PORTSC_PE     0x0004  /* Port Enable (R/W) */
#define UHCI_PORTSC_PEC    0x0008  /* Port Enable Change (R/WC) */
#define UHCI_PORTSC_LSDA   0x0100  /* Low Speed Device Attached (RO) */
#define UHCI_PORTSC_RESET  0x0200  /* Port Reset (R/W) */
/* CSC/PEC — write-1-to-clear; при обычном read-modify-write их нужно
 * обнулять перед записью, иначе случайно "подтвердим" ещё не
 * обработанное изменение состояния порта. */
#define UHCI_PORTSC_RWC_MASK (UHCI_PORTSC_CSC | UHCI_PORTSC_PEC)

/* =========================================================================
 * Queue Head / Transfer Descriptor — Design Guide §3.2/§3.3
 * ========================================================================= */
#define UHCI_PTR_TERMINATE 0x00000001u
#define UHCI_PTR_QH        0x00000002u
#define UHCI_PTR_VF        0x00000004u   /* Depth-first (только в TD.link) */

typedef struct {
    volatile uint32_t link;
    volatile uint32_t element;
    uint32_t reserved[2];   /* добивка до 16 байт — требуемое аппаратное выравнивание */
} uhci_qh_t;

typedef struct {
    volatile uint32_t link;
    volatile uint32_t status;
    volatile uint32_t token;
    volatile uint32_t buffer;
    uint32_t sw[4];          /* НЕ используется контроллером — своё, для выравнивания до 32 байт */
} uhci_td_t;

#define UHCI_TD_STATUS_BITSTUFF  (1u << 17)
#define UHCI_TD_STATUS_CRCTO     (1u << 18)
#define UHCI_TD_STATUS_NAK       (1u << 19)
#define UHCI_TD_STATUS_BABBLE    (1u << 20)
#define UHCI_TD_STATUS_DBUFERR   (1u << 21)
#define UHCI_TD_STATUS_STALLED   (1u << 22)
#define UHCI_TD_STATUS_ACTIVE    (1u << 23)
#define UHCI_TD_IOC              (1u << 24)
#define UHCI_TD_IOS              (1u << 25)
#define UHCI_TD_LS               (1u << 26)
#define UHCI_TD_CERR_SHIFT       27
#define UHCI_TD_SPD              (1u << 29)

#define UHCI_TD_STATUS_ANY_ERROR (UHCI_TD_STATUS_BITSTUFF | UHCI_TD_STATUS_CRCTO | \
                                  UHCI_TD_STATUS_BABBLE | UHCI_TD_STATUS_DBUFERR | \
                                  UHCI_TD_STATUS_STALLED)

#define UHCI_PID_SETUP 0x2Du
#define UHCI_PID_IN    0x69u
#define UHCI_PID_OUT   0xE1u

#define UHCI_TD_POOL_COUNT 128   /* ровно одна страница (128 * 32 байта = 4096) —
                                  * с запасом покрывает control-transfer с
                                  * wLength<=512 даже при maxpacket0=8 (худший
                                  * случай: 1 SETUP + 64 DATA + 1 STATUS = 66) */

/* Буфер под bulk_transfer (см. Mass Storage BOT) — за один вызов HCD может
 * передать не больше этого объёма; вызывающий (usb_msc_blkdev_read/write в
 * USBMassStorage.c) сам разбивает более крупные запросы на несколько
 * вызовов. 64 КБ ровно совпадает с тем, что покрывает один проход по
 * UHCI_TD_POOL_COUNT дескрипторов при максимальном full-speed пакете
 * (128 * 512 — с запасом даже сверх реального full-speed maxpacket=64). */
#define UHCI_BULK_BUF_SIZE (64u * 1024u)

/* =========================================================================
 * Контекст контроллера
 * ========================================================================= */
#define UHCI_MAX_CONTROLLERS 4
#define UHCI_NUM_PORTS 2   /* стандарт UHCI — ровно 2 root-порта на контроллер */

typedef struct {
    uint16_t   io_base;

    uint32_t  *frame_list;       /* виртуальный адрес, 1024 x uint32_t */
    uint64_t   frame_list_phys;

    uhci_qh_t *ctrl_qh;
    uint64_t   ctrl_qh_phys;

    uhci_td_t *td_pool;          /* UHCI_TD_POOL_COUNT дескрипторов подряд */
    uint64_t   td_pool_phys;

    uint8_t   *scratch;          /* DMA-буфер: setup (8Б) @0, данные @64 — только control_transfer */
    uint64_t   scratch_phys;

    uint8_t   *bulk_buf;         /* DMA-буфер под bulk_transfer (Mass Storage и т.п.) —
                                   * отдельный от scratch, т.к. размер тут намного больше
                                   * (см. UHCI_BULK_BUF_SIZE) */
    uint64_t   bulk_buf_phys;

    usb_hcd_t  hcd;              /* встроенный usb_hcd_t, hcd.priv == this */
} uhci_ctrl_t;

static uhci_ctrl_t g_uhci[UHCI_MAX_CONTROLLERS];
static int         g_uhci_count = 0;

/* =========================================================================
 * port_reset — USB 2.0 §7.1.7.5 (root hub reset через software)
 * ========================================================================= */
static bool uhci_port_reset(usb_hcd_t *hcd, uint8_t port, uint8_t *out_speed) {
    uhci_ctrl_t *c = (uhci_ctrl_t *)hcd->priv;
    if (port >= UHCI_NUM_PORTS) return false;
    uint16_t reg = (uint16_t)(c->io_base + UHCI_REG_PORTSC1 + port * 2);

    uint16_t val = inw(reg);
    if (!(val & UHCI_PORTSC_CCS)) return false;   /* порт пуст */

    /* Держим PR=1 минимум 50мс (Design Guide рекомендует именно столько
     * для root-порта, а не минимальные 10мс из спеки USB — контроллеры в
     * QEMU и на реальном железе это переживают одинаково). */
    outw(reg, (val & ~UHCI_PORTSC_RWC_MASK) | UHCI_PORTSC_RESET);
    uhci_delay_ms(50);
    val = inw(reg);
    outw(reg, (val & ~UHCI_PORTSC_RWC_MASK) & ~UHCI_PORTSC_RESET);
    uhci_delay_ms(10);

    /* Включаем порт */
    val = inw(reg);
    outw(reg, (val & ~UHCI_PORTSC_RWC_MASK) | UHCI_PORTSC_PE);
    uhci_delay_ms(10);

    val = inw(reg);
    if (!(val & UHCI_PORTSC_PE))  return false;   /* не включился */
    if (!(val & UHCI_PORTSC_CCS)) return false;   /* отключили прямо во время reset */

    *out_speed = (val & UHCI_PORTSC_LSDA) ? USB_SPEED_LOW : USB_SPEED_FULL;
    return true;
}

/* =========================================================================
 * control_transfer — см. подробное описание алгоритма в шапке файла
 * ========================================================================= */
static void uhci_td_fill(uhci_td_t *td, uint8_t pid, uint8_t addr, uint8_t ep,
                         uint8_t toggle, uint16_t len, uint64_t buf_phys,
                         uint32_t base_flags, bool spd) {
    uint32_t maxlen_field = (len == 0) ? 0x7FFu : (uint32_t)(len - 1);
    td->token  = pid | ((uint32_t)addr << 8) | ((uint32_t)ep << 15) |
                ((uint32_t)toggle << 19) | (maxlen_field << 21);
    td->buffer = (uint32_t)buf_phys;
    td->status = base_flags | (spd ? UHCI_TD_SPD : 0);
    td->link   = UHCI_PTR_TERMINATE;   /* переустановится при линковке ниже */
}

static int uhci_control_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                                 uint8_t maxp0, const usb_setup_pkt_t *setup,
                                 void *data, uint16_t len) {
    uhci_ctrl_t *c = (uhci_ctrl_t *)hcd->priv;
    if (maxp0 == 0) maxp0 = 8;
    if (len > 512) return -1;   /* см. ограничение в USB.c: конфигурации режем до 512 */

    /* Копируем setup + (если OUT) данные в DMA-буфер контроллера — buffer
     * пришедшего вызывающего может быть выше 4ГБ или просто не годиться
     * для DMA как есть. */
    usb_setup_pkt_t *setup_dma = (usb_setup_pkt_t *)c->scratch;
    *setup_dma = *setup;
    uint64_t setup_phys = c->scratch_phys;

    uint8_t *data_dma   = c->scratch + 64;
    uint64_t data_phys  = c->scratch_phys + 64;
    bool dir_in = (setup->bmRequestType & USB_REQ_DIR_D2H) != 0;
    if (data && len > 0 && !dir_in) memcpy(data_dma, data, len);

    uhci_td_t *tds = c->td_pool;
    uint32_t   n   = 0;
    bool low_speed = (speed == USB_SPEED_LOW);
    uint32_t base_flags = (low_speed ? UHCI_TD_LS : 0) |
                          (3u << UHCI_TD_CERR_SHIFT) | UHCI_TD_STATUS_ACTIVE;

    /* SETUP — toggle всегда 0 */
    uhci_td_fill(&tds[n++], UHCI_PID_SETUP, dev_addr, 0, 0, 8, setup_phys, base_flags, false);

    /* DATA — чередуем DATA0/DATA1, начиная с DATA1 */
    uint8_t  toggle    = 1;
    uint16_t remaining = len;
    uint16_t off       = 0;
    uint8_t  data_pid  = dir_in ? UHCI_PID_IN : UHCI_PID_OUT;
    while (remaining > 0 && n < UHCI_TD_POOL_COUNT - 1) {
        uint16_t chunk = remaining > maxp0 ? maxp0 : remaining;
        uhci_td_fill(&tds[n++], data_pid, dev_addr, 0, toggle,
                    chunk, data_phys + off, base_flags, dir_in /* SPD только для IN */);
        off       += chunk;
        remaining -= chunk;
        toggle    ^= 1;
    }

    /* STATUS — направление обратное данным (для len==0 всегда IN), toggle всегда 1 */
    uint8_t status_pid;
    if (len == 0)       status_pid = UHCI_PID_IN;
    else                status_pid = dir_in ? UHCI_PID_OUT : UHCI_PID_IN;
    uhci_td_fill(&tds[n++], status_pid, dev_addr, 0, 1, 0, 0, base_flags, false);

    /* Линкуем TD друг за другом (Vf=1 — обработать всё в этом же кадре) */
    for (uint32_t i = 0; i < n - 1; i++)
        tds[i].link = (uint32_t)(c->td_pool_phys + (i + 1) * sizeof(uhci_td_t)) | UHCI_PTR_VF;
    tds[n - 1].link = UHCI_PTR_TERMINATE;

    /* Подвешиваем на постоянную QH */
    c->ctrl_qh->element = (uint32_t)c->td_pool_phys;

    /* Poll: успех — element дошёл до Terminate. Ошибка — какой-то TD
     * снял Active, не дойдя до конца цепочки. Таймаут — на случай
     * полностью зависшего/отсутствующего устройства. */
    /* Таймаут именно control_transfer'а держим коротким (не как у
     * bulk_transfer, там ~2с — Mass Storage реально бывает медленным).
     * control_transfer теперь дёргается не только при enumerate (один
     * раз на устройство), но и КАЖДЫЙ HID-опрос (см. usb_hid_poll_all в
     * Resources/USBHID.c, вызывается из sched_tick раз в 3 тика) — если
     * устройство перестало отвечать, 500мс блокировки на каждый такой
     * опрос убивали отзывчивость всей системы (единственное ядро, тик
     * синхронный — 500мс "заморозки" каждые ~30мс это уже практически
     * постоянный лаг). 50мс с запасом хватает рабочему устройству
     * (реальный ответ — доли миллисекунды) и ограничивает ущерб от
     * неотвечающего/отключённого. */
    uint64_t deadline = uhci_rdtsc() + 3000000ULL * 50;   /* ~50мс */
    int result = -3;
    for (;;) {
        if (c->ctrl_qh->element & UHCI_PTR_TERMINATE) { result = 0; break; }

        bool err = false;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t st = tds[i].status;
            if (st & UHCI_TD_STATUS_ACTIVE) continue;
            if (st & UHCI_TD_STATUS_ANY_ERROR) { err = true; break; }
        }
        if (err) { result = -2; break; }

        if (uhci_rdtsc() > deadline) { result = -3; break; }
        __asm__ volatile ("pause" ::: "memory");
    }

    /* Снимаем цепочку независимо от результата — не оставляем "живые"
     * TD висеть на общей QH после нашей функции. */
    c->ctrl_qh->element = UHCI_PTR_TERMINATE;

    if (result == 0 && data && len > 0 && dir_in) memcpy(data, data_dma, len);
    return result;
}

/* =========================================================================
 * bulk_transfer — как control_transfer, но без SETUP/STATUS стадий и с
 * Data Toggle, который переживает между вызовами (см. USB.h). Данные
 * большего объёма, чем влезает в один проход по td_pool (POOL_COUNT-1
 * пакетов), передаются несколькими последовательными подвешиваниями на
 * ту же постоянную QH — каждое такое "подвешивание" ниже называется
 * батчем. bulk_buf — отдельный от scratch DMA-буфер, физически
 * непрерывный на весь UHCI_BULK_BUF_SIZE, поэтому батчи внутри одного
 * вызова просто используют разные смещения в НЁМ ЖЕ (копирование туда/
 * обратно — один раз в начале/конце вызова, а не на каждый батч).
 * ========================================================================= */
static int uhci_bulk_transfer(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                              uint8_t ep_addr, uint16_t max_packet,
                              void *data, uint32_t len, uint8_t *toggle_inout) {
    uhci_ctrl_t *c = (uhci_ctrl_t *)hcd->priv;
    if (max_packet == 0) max_packet = 8;
    if (len > UHCI_BULK_BUF_SIZE) return -1;

    bool    dir_in = (ep_addr & 0x80u) != 0;
    uint8_t ep_num = ep_addr & 0x0Fu;
    uint8_t toggle = toggle_inout ? *toggle_inout : 0;
    bool    low_speed = (speed == USB_SPEED_LOW);
    uint32_t base_flags = (low_speed ? UHCI_TD_LS : 0) |
                          (3u << UHCI_TD_CERR_SHIFT) | UHCI_TD_STATUS_ACTIVE;
    uint8_t pid = dir_in ? UHCI_PID_IN : UHCI_PID_OUT;

    if (!dir_in && data && len > 0) memcpy(c->bulk_buf, data, len);

    uint32_t remaining = len;
    uint32_t off = 0;
    bool     first_iter = true;

    while (remaining > 0 || first_iter) {
        first_iter = false;
        uhci_td_t *tds = c->td_pool;
        uint32_t   n   = 0;

        if (len == 0) {
            /* Нулевой пакет — единственный случай, когда шлём TD без данных. */
            uhci_td_fill(&tds[0], pid, dev_addr, ep_num, toggle, 0, c->bulk_buf_phys, base_flags, false);
            n = 1;
            toggle ^= 1;
        } else {
            while (remaining > 0 && n < UHCI_TD_POOL_COUNT) {
                uint16_t chunk = remaining > max_packet ? max_packet : (uint16_t)remaining;
                uhci_td_fill(&tds[n], pid, dev_addr, ep_num, toggle, chunk,
                            c->bulk_buf_phys + off, base_flags, dir_in);
                n++;
                off       += chunk;
                remaining -= chunk;
                toggle    ^= 1;
            }
        }

        for (uint32_t i = 0; i < n - 1; i++)
            tds[i].link = (uint32_t)(c->td_pool_phys + (i + 1) * sizeof(uhci_td_t)) | UHCI_PTR_VF;
        tds[n - 1].link = UHCI_PTR_TERMINATE;

        c->ctrl_qh->element = (uint32_t)c->td_pool_phys;

        /* Bulk-устройства (особенно флешки) бывают заметно медленнее
         * control-транзакций — таймаут щедрее, чем в control_transfer. */
        uint64_t deadline = uhci_rdtsc() + 3000000ULL * 2000;   /* ~2с на батч */
        int result = -3;
        for (;;) {
            if (c->ctrl_qh->element & UHCI_PTR_TERMINATE) { result = 0; break; }
            bool err = false;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t st = tds[i].status;
                if (st & UHCI_TD_STATUS_ACTIVE) continue;
                if (st & UHCI_TD_STATUS_ANY_ERROR) { err = true; break; }
            }
            if (err) { result = -2; break; }
            if (uhci_rdtsc() > deadline) { result = -3; break; }
            __asm__ volatile ("pause" ::: "memory");
        }
        c->ctrl_qh->element = UHCI_PTR_TERMINATE;

        if (result != 0) { if (toggle_inout) *toggle_inout = toggle; return result; }
        if (len == 0) break;
    }

    if (dir_in && data && len > 0) memcpy(data, c->bulk_buf, len);
    if (toggle_inout) *toggle_inout = toggle;
    return 0;
}

static const usb_hcd_ops_t g_uhci_ops = {
    .control_transfer = uhci_control_transfer,
    .port_reset       = uhci_port_reset,
    .bulk_transfer    = uhci_bulk_transfer,
};

/* =========================================================================
 * Инициализация одного контроллера
 * ========================================================================= */
static bool uhci_init_controller(uint16_t io_base) {
    if (g_uhci_count >= UHCI_MAX_CONTROLLERS) return false;
    uhci_ctrl_t *c = &g_uhci[g_uhci_count];
    memset(c, 0, sizeof(*c));
    c->io_base = io_base;

    /* Global reset — сбрасывает вообще всё, включая подключённые устройства */
    outw((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_CMD_GRESET);
    uhci_delay_ms(50);
    outw((uint16_t)(io_base + UHCI_REG_USBCMD), 0);
    uhci_delay_ms(10);

    /* Host controller reset — сбрасывает сам контроллер (не порты) */
    outw((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_CMD_HCRESET);
    bool reset_ok = false;
    for (int i = 0; i < 1000; i++) {
        if (!(inw((uint16_t)(io_base + UHCI_REG_USBCMD)) & UHCI_CMD_HCRESET)) { reset_ok = true; break; }
        uhci_delay_ms(1);
    }
    if (!reset_ok) {
        DBG_MSG("UHCI", "HCRESET timeout, skipping controller");
        return false;
    }

    /* DMA-память: Frame List (страница ровно под 1024 x uint32_t),
     * персистентная QH и пул TD, scratch-буфер под setup+данные. */
    c->frame_list = (uint32_t *)dma32_alloc(4096, &c->frame_list_phys);
    c->ctrl_qh    = (uhci_qh_t *)dma32_alloc(4096, &c->ctrl_qh_phys);
    c->td_pool    = (uhci_td_t *)dma32_alloc(UHCI_TD_POOL_COUNT * sizeof(uhci_td_t), &c->td_pool_phys);
    c->scratch    = (uint8_t  *)dma32_alloc(4096, &c->scratch_phys);
    c->bulk_buf   = (uint8_t  *)dma32_alloc(UHCI_BULK_BUF_SIZE, &c->bulk_buf_phys);
    if (!c->frame_list || !c->ctrl_qh || !c->td_pool || !c->scratch || !c->bulk_buf) {
        DBG_MSG("UHCI", "DMA allocation failed, skipping controller");
        return false;
    }

    c->ctrl_qh->link    = UHCI_PTR_TERMINATE;
    c->ctrl_qh->element = UHCI_PTR_TERMINATE;

    /* Frame List: persistent control QH висит во ВСЕХ 1024 слотах (см.
     * комментарий в шапке файла). */
    uint32_t qh_entry = (uint32_t)c->ctrl_qh_phys | UHCI_PTR_QH;
    for (int i = 0; i < 1024; i++) c->frame_list[i] = qh_entry;

    outw((uint16_t)(io_base + UHCI_REG_USBINTR), 0);      /* без прерываний — работаем через polling */
    outw((uint16_t)(io_base + UHCI_REG_FRNUM), 0);
    outl((uint16_t)(io_base + UHCI_REG_FRBASEADD), (uint32_t)c->frame_list_phys);
    outb((uint16_t)(io_base + UHCI_REG_SOFMOD), 0x40);

    /* Запуск: Run/Stop + Configure Flag + Max Packet 64 */
    outw((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    uhci_delay_ms(10);

    uint16_t sts = inw((uint16_t)(io_base + UHCI_REG_USBSTS));
    if (sts & UHCI_STS_HCHALTED) {
        DBG_MSG("UHCI", "controller halted after start, skipping");
        return false;
    }

    c->hcd.ops       = &g_uhci_ops;
    c->hcd.priv      = c;
    c->hcd.name      = "uhci";
    c->hcd.num_ports = UHCI_NUM_PORTS;

    usb_register_hcd(&c->hcd);
    g_uhci_count++;
    return true;
}

/* =========================================================================
 * PCI-сканирование — class 0x0C (Serial Bus), subclass 0x03 (USB),
 * prog-if 0x00 (UHCI). Отдельно от EHCI.c (prog-if 0x20) и будущих
 * OHCI (0x10) / xHCI (0x30) — все они делят один class/subclass, только
 * prog-if разный.
 * ========================================================================= */
void uhci_pci_scan(void) {
    DBG_MSG("UHCI", "=== uhci_pci_scan ===");
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

                if (class_code != 0x0C || subclass != 0x03 || prog_if != 0x00) {
                    uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, dev, fn, 0x0C) >> 16);
                    if (fn == 0 && !(hdr & 0x80)) break;
                    continue;
                }

                DBG_VAL("UHCI", "found UHCI at bus", bus);
                DBG_VAL("UHCI", "  device", dev);
                DBG_VAL("UHCI", "  vendor:device", id);

                /* Забираем контроллер из-под BIOS SMM (Legacy Support
                 * register, смещение 0xC0) — стандартный приём: запись
                 * 0x8F00 гасит все SMI-триггеры и одновременно
                 * подтверждает (write-1-to-clear) уже взведённые
                 * статусные биты. Без этого шага BIOS может продолжать
                 * "владеть" контроллером через SMI и конфликтовать с нами. */
                uint32_t legsup = pci_read32((uint8_t)bus, dev, fn, 0xC0);
                pci_write32((uint8_t)bus, dev, fn, 0xC0, (legsup & 0xFFFF0000u) | 0x8F00u);

                /* Включаем I/O space + Bus Master */
                uint32_t pcmd = pci_read32((uint8_t)bus, dev, fn, 0x04);
                pci_write32((uint8_t)bus, dev, fn, 0x04, pcmd | 0x05);

                /* BAR4 — всегда I/O space у UHCI (бит0=1) */
                uint32_t bar4 = pci_read32((uint8_t)bus, dev, fn, 0x20);
                if (!(bar4 & 1)) {
                    DBG_MSG("UHCI", "BAR4 is not I/O space — skip (не соответствует спеке)");
                    continue;
                }
                uint16_t io_base = (uint16_t)(bar4 & 0xFFFCu);
                DBG_VAL("UHCI", "  io_base", io_base);

                if (uhci_init_controller(io_base)) found++;

                uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, dev, fn, 0x0C) >> 16);
                if (fn == 0 && !(hdr & 0x80)) break;
            }
        }
    }

    DBG_VAL("UHCI", "controllers found", (uint64_t)found);
    DBG_MSG("UHCI", "=== uhci_pci_scan done ===");
}
