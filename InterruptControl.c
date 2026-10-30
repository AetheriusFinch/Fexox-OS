/* Fexox OS Project, EULA GPLv2 */
 




#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "debug_out.h"

 


typedef uint64_t           phys_addr_t;
typedef uint64_t           virt_addr_t;

 
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
typedef _Bool bool;
#endif

#define true  1
#define false 0
#define NULL ((void*)0)
#define PAGE_SIZE       4096ULL
#define IRQ_BASE        0x20
#define TIMER_VECTOR    (IRQ_BASE + 0)
#define DIRECT_MAP_OFF  0xFFFF880000000000ULL

 


typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} cpu_frame_t;

typedef struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idtr_t;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

#define IDT_ENTRIES 256
#define IDT_GATE_INT  0x8E
#define IDT_GATE_TRAP 0x8F

static idt_entry_t g_idt[IDT_ENTRIES] __attribute__((aligned(16)));
typedef void (*isr_handler_t)(uint64_t num, uint64_t err, cpu_frame_t *frame);
static isr_handler_t g_isr_handlers[IDT_ENTRIES];

 
void apic_send_eoi(uint8_t vector);

 


static inline uint8_t inb(uint16_t port) { 
    uint8_t v; 
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); 
    return v; 
}

static inline void outb(uint16_t port, uint8_t v) { 
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); 
}

static inline void cli(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile ("hlt" ::: "memory"); }

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi; 
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t read_cr2(void) {
    uint64_t v; 
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v)); 
    return v;
}

static inline void lidt(const idtr_t *ptr) { 
    __asm__ volatile ("lidt %0" :: "m"(*ptr) : "memory"); 
}

 


static void idt_set_gate(uint8_t num, void *handler, uint8_t selector, uint8_t ist, uint8_t type) {
    uint64_t offset = (uint64_t)handler;
    idt_entry_t *e = &g_idt[num];
    e->offset_low   = (uint16_t)(offset & 0xFFFF);
    e->selector     = selector;
    e->ist          = ist & 0x7;
    e->type_attr    = type;
    e->offset_mid   = (uint16_t)((offset >> 16) & 0xFFFF);
    e->offset_high  = (uint32_t)((offset >> 32) & 0xFFFFFFFF);
    e->zero         = 0;
}

static void idt_load(void) {
    idtr_t idtr = { .limit = sizeof(g_idt) - 1, .base = (uint64_t)g_idt };
    lidt(&idtr);
}

 


 







extern uint32_t fb_width(void);
extern uint32_t fb_height(void);
extern void     fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern int32_t  fb_draw_string(int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg);
extern void     fb_flip(void);

static void panic_draw_fb(const char *msg) {
    uint32_t sw = fb_width(), sh = fb_height();
    if (!sw || !sh) return;

    int lines = 3;
    for (const char *p = msg; *p; p++) if (*p == '\n') lines++;
    uint32_t box_w = (sw > 16 + 60 * 8) ? 60 * 8 : sw - 16;
    if (16 + (uint32_t)lines * 18 + 8 > sh) return;
    fb_fill_rect(8, 8, box_w, (uint32_t)lines * 18 + 8, 0xAA0000u);

    int32_t y = 12;
    fb_draw_string(16, y, "*** KERNEL PANIC (Fexox OS) ***", 0xFFFF00u, 0xAA0000u);
    y += 18;

    char line[64];
    int n = 0;
    for (const char *p = msg; ; p++) {
        if (*p == '\n' || *p == '\0') {
            line[n] = '\0';
            fb_draw_string(16, y, line, 0xFFFFFFu, 0xAA0000u);
            y += 18;
            n = 0;
            if (*p == '\0') break;
        } else if (n < 58) {
            line[n++] = *p;
        }
    }
    fb_draw_string(16, y, "system halted", 0xFFFF00u, 0xAA0000u);
    fb_flip();
}

static void vga_panic(const char *msg) {
     




    dbg_puts("\n*** KERNEL PANIC ***\n");
    dbg_puts(msg);
    dbg_puts("\n*** halted ***\n");
    panic_draw_fb(msg);
    while (1) hlt();
}

static void format_hex(uint64_t val, char *buf) {
    const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x'; buf[2] = '\0';
    int pos = 2;
    for (int i = 60; i >= 0; i -= 4) buf[pos++] = hex[(val >> i) & 0xF];
    buf[pos] = '\0';
}

 




static int pm_add(char *dst, int pos, const char *s) {
    while (*s) dst[pos++] = *s++;
    return pos;
}
static int pm_hex(char *dst, int pos, uint64_t v, int digits) {
    static const char hx[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) dst[pos++] = hx[(v >> i) & 0xF];
    return pos;
}
static int panic_append_ctx(char *dst, int pos, const cpu_frame_t *f) {
    pos = pm_add(dst, pos, "\nFLG : "); pos = pm_hex(dst, pos, f->rflags, 16);
    pos = pm_add(dst, pos, "\nRAX : "); pos = pm_hex(dst, pos, f->rax, 16);
    pos = pm_add(dst, pos, " RBX: ");   pos = pm_hex(dst, pos, f->rbx, 16);
    pos = pm_add(dst, pos, "\nRCX : "); pos = pm_hex(dst, pos, f->rcx, 16);
    pos = pm_add(dst, pos, " RDX: ");   pos = pm_hex(dst, pos, f->rdx, 16);
    pos = pm_add(dst, pos, "\nRSI : "); pos = pm_hex(dst, pos, f->rsi, 16);
    pos = pm_add(dst, pos, " RDI: ");   pos = pm_hex(dst, pos, f->rdi, 16);
    pos = pm_add(dst, pos, "\nRBP : "); pos = pm_hex(dst, pos, f->rbp, 16);
    pos = pm_add(dst, pos, " R8 : ");   pos = pm_hex(dst, pos, f->r8, 16);

    if (f->rip >= 0xFFFFFFFF80000000ULL) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)f->rip;
        pos = pm_add(dst, pos, "\nCODE:");
        for (int i = 0; i < 12; i++) { dst[pos++] = ' '; pos = pm_hex(dst, pos, code[i], 2); }
    }
    if (f->rsp >= 0xFFFFFFFF80000000ULL && (f->rsp & 7) == 0) {
        const uint64_t *st = (const uint64_t *)(uintptr_t)f->rsp;
        pos = pm_add(dst, pos, "\nSTK0: "); pos = pm_hex(dst, pos, st[0], 16);
        pos = pm_add(dst, pos, " ");        pos = pm_hex(dst, pos, st[1], 16);
        pos = pm_add(dst, pos, "\nSTK2: "); pos = pm_hex(dst, pos, st[2], 16);
        pos = pm_add(dst, pos, " ");        pos = pm_hex(dst, pos, st[3], 16);
    }
    return pos;
}

 





















__attribute__((used))
static void page_fault_handler_c(uint8_t num, uint64_t err, cpu_frame_t *frame) {
    (void)num;
    uint64_t cr2 = read_cr2();
    char addr_buf[20], err_buf[20], rip_buf[20], cs_buf[20], rsp_buf[20];
    format_hex(cr2, addr_buf);
    format_hex(err, err_buf);
    format_hex(frame->rip, rip_buf);
    format_hex(frame->cs,  cs_buf);
    format_hex(frame->rsp, rsp_buf);
    const char *p = (err & 1) ? "P" : "NP";
    const char *wr = (err & 2) ? "W" : "R";
    const char *us = (err & 4) ? "U" : "S";

    static char panic_msg[900];
    int pos = 0;

    const char *prefix = "KERNEL PAGE FAULT\nADDR: ";
    for (int i = 0; prefix[i]; i++) panic_msg[pos++] = prefix[i];
    for (int i = 0; addr_buf[i]; i++) panic_msg[pos++] = addr_buf[i];

    const char *mid = "\nERR : ";
    for (int i = 0; mid[i]; i++) panic_msg[pos++] = mid[i];
    for (int i = 0; err_buf[i]; i++) panic_msg[pos++] = err_buf[i];

    const char *flags = "\nFLAGS: P= ";
    for (int i = 0; flags[i]; i++) panic_msg[pos++] = flags[i];
    panic_msg[pos++] = p[0]; panic_msg[pos++] = ' ';

    const char *wr_str = "W/R= ";
    for (int i = 0; wr_str[i]; i++) panic_msg[pos++] = wr_str[i];
    panic_msg[pos++] = wr[0]; panic_msg[pos++] = ' ';

    const char *us_str = "U/S= ";
    for (int i = 0; us_str[i]; i++) panic_msg[pos++] = us_str[i];
    panic_msg[pos++] = us[0];

    const char *rip_lbl = "\nRIP : ";
    for (int i = 0; rip_lbl[i]; i++) panic_msg[pos++] = rip_lbl[i];
    for (int i = 0; rip_buf[i]; i++) panic_msg[pos++] = rip_buf[i];

    const char *cs_lbl = "\nCS  : ";
    for (int i = 0; cs_lbl[i]; i++) panic_msg[pos++] = cs_lbl[i];
    for (int i = 0; cs_buf[i]; i++) panic_msg[pos++] = cs_buf[i];

    const char *rsp_lbl = "\nRSP : ";
    for (int i = 0; rsp_lbl[i]; i++) panic_msg[pos++] = rsp_lbl[i];
    for (int i = 0; rsp_buf[i]; i++) panic_msg[pos++] = rsp_buf[i];
    pos = panic_append_ctx(panic_msg, pos, frame);
    panic_msg[pos] = '\0';

    vga_panic(panic_msg);
}

 


 






__attribute__((used))
static void exception_dispatch(uint8_t num, uint64_t err, cpu_frame_t *frame);

 






















#define ISR_PUSH_GPR \
    "push %%rax\n\t" "push %%rbx\n\t" "push %%rcx\n\t" "push %%rdx\n\t" \
    "push %%rbp\n\t" "push %%rsi\n\t" "push %%rdi\n\t" \
    "push %%r8\n\t"  "push %%r9\n\t"  "push %%r10\n\t" "push %%r11\n\t" \
    "push %%r12\n\t" "push %%r13\n\t" "push %%r14\n\t" "push %%r15\n\t"

#define ISR_POP_GPR \
    "pop %%r15\n\t" "pop %%r14\n\t" "pop %%r13\n\t" "pop %%r12\n\t" \
    "pop %%r11\n\t" "pop %%r10\n\t" "pop %%r9\n\t"  "pop %%r8\n\t" \
    "pop %%rdi\n\t" "pop %%rsi\n\t" "pop %%rbp\n\t" "pop %%rdx\n\t" \
    "pop %%rcx\n\t" "pop %%rbx\n\t" "pop %%rax\n\t"

#define ISR_STUB_NOERR(NAME, NUM, HANDLER) \
__attribute__((naked)) void NAME(void) { \
    __asm__ volatile ( \
        "push $0\n\t"                   \
        ISR_PUSH_GPR \
        "mov %%rsp, %%rdx\n\t"          \
        "xor %%esi, %%esi\n\t"          \
        "mov $" #NUM ", %%edi\n\t"      \
        "call " #HANDLER "\n\t" \
        ISR_POP_GPR \
        "add $8, %%rsp\n\t"             \
        "iretq\n\t" \
        ::: "memory" \
    ); \
}

 


#define ISR_STUB_IRQ(NAME, VEC, HANDLER) \
__attribute__((naked)) void NAME(void) { \
    __asm__ volatile ( \
        "push $0\n\t"                   \
        ISR_PUSH_GPR \
        "mov %%rsp, %%rsi\n\t"          \
        "mov $" #VEC ", %%edi\n\t"      \
        "call " #HANDLER "\n\t" \
        ISR_POP_GPR \
        "add $8, %%rsp\n\t" \
        "iretq\n\t" \
        ::: "memory" \
    ); \
}

#define ISR_STUB_ERR(NAME, NUM, HANDLER) \
__attribute__((naked)) void NAME(void) { \
    __asm__ volatile ( \
        ISR_PUSH_GPR \
        "mov %%rsp, %%rdx\n\t"          \
        "mov 120(%%rsp), %%rsi\n\t"     \
        "mov $" #NUM ", %%edi\n\t"      \
        "call " #HANDLER "\n\t" \
        ISR_POP_GPR \
        "add $8, %%rsp\n\t"             \
        "iretq\n\t" \
        ::: "memory" \
    ); \
}

__attribute__((used))
static void exception_dispatch(uint8_t num, uint64_t err, cpu_frame_t *frame) {
    if (g_isr_handlers[num]) {
        g_isr_handlers[num](num, err, frame);
        return;
    }
    static const char *exc_names[32] = {
        "#DE Divide by Zero", "#DB Debug", "NMI", "#BP Breakpoint",
        "#OF Overflow", "#BR Bound Range", "#UD Invalid Opcode", "#NM Device Not Available",
        "#DF Double Fault", "#MF Coprocessor Segment", "#TS Invalid TSS", "#NP Segment Not Present",
        "#SS Stack Fault", "#GP General Protection", "#PF Page Fault", "#RF Reserved",
        "#MF x87 FPU", "#AC Alignment Check", "#MC Machine Check", "#XF SIMD FPE",
        "#VE Virtualization", "#RF Reserved", "#RF Reserved", "#RF Reserved",
        "#RF Reserved", "#RF Reserved", "#RF Reserved", "#RF Reserved",
        "#RF Reserved", "#RF Reserved", "#SX Security Exception", "#TF Triple Fault"
    };

     







    char rip_buf[20], cs_buf[20], rsp_buf[20], err_buf[20];
    format_hex(frame->rip, rip_buf);
    format_hex(frame->cs,  cs_buf);
    format_hex(frame->rsp, rsp_buf);
    format_hex(err,        err_buf);

    static char panic_msg[900];
    int pos = 0;
    const char *name = (num < 32) ? exc_names[num] : "Unknown Exception";
    for (int i = 0; name[i]; i++) panic_msg[pos++] = name[i];

    const char *err_lbl = "\nERR : ";
    for (int i = 0; err_lbl[i]; i++) panic_msg[pos++] = err_lbl[i];
    for (int i = 0; err_buf[i]; i++) panic_msg[pos++] = err_buf[i];

    const char *rip_lbl = "\nRIP : ";
    for (int i = 0; rip_lbl[i]; i++) panic_msg[pos++] = rip_lbl[i];
    for (int i = 0; rip_buf[i]; i++) panic_msg[pos++] = rip_buf[i];

    const char *cs_lbl = "\nCS  : ";
    for (int i = 0; cs_lbl[i]; i++) panic_msg[pos++] = cs_lbl[i];
    for (int i = 0; cs_buf[i]; i++) panic_msg[pos++] = cs_buf[i];

    const char *rsp_lbl = "\nRSP : ";
    for (int i = 0; rsp_lbl[i]; i++) panic_msg[pos++] = rsp_lbl[i];
    for (int i = 0; rsp_buf[i]; i++) panic_msg[pos++] = rsp_buf[i];
    pos = panic_append_ctx(panic_msg, pos, frame);
    panic_msg[pos] = '\0';

    vga_panic(panic_msg);
}

__attribute__((used))
static void irq_dispatch(uint8_t num, cpu_frame_t *frame) {
    if (g_isr_handlers[num]) g_isr_handlers[num](num, 0, frame);
     








    apic_send_eoi(num);
}

 
ISR_STUB_NOERR(exc_0,  0,  exception_dispatch)
ISR_STUB_NOERR(exc_1,  1,  exception_dispatch)
ISR_STUB_NOERR(exc_2,  2,  exception_dispatch)
ISR_STUB_NOERR(exc_3,  3,  exception_dispatch)
ISR_STUB_NOERR(exc_4,  4,  exception_dispatch)
ISR_STUB_NOERR(exc_5,  5,  exception_dispatch)
ISR_STUB_NOERR(exc_6,  6,  exception_dispatch)
ISR_STUB_NOERR(exc_7,  7,  exception_dispatch)
ISR_STUB_NOERR(exc_9,  9,  exception_dispatch)
ISR_STUB_NOERR(exc_15, 15, exception_dispatch)
ISR_STUB_NOERR(exc_16, 16, exception_dispatch)
ISR_STUB_NOERR(exc_18, 18, exception_dispatch)
ISR_STUB_NOERR(exc_19, 19, exception_dispatch)
ISR_STUB_NOERR(exc_20, 20, exception_dispatch)

 
ISR_STUB_ERR(exc_8,  8,  exception_dispatch)
ISR_STUB_ERR(exc_10, 10, exception_dispatch)
ISR_STUB_ERR(exc_11, 11, exception_dispatch)
ISR_STUB_ERR(exc_12, 12, exception_dispatch)
ISR_STUB_ERR(exc_13, 13, exception_dispatch)
ISR_STUB_ERR(exc_17, 17, exception_dispatch)
ISR_STUB_ERR(exc_30, 30, exception_dispatch)

 
ISR_STUB_ERR(pf_entry, 14, page_fault_handler_c)

 
 






























__attribute__((naked)) void irq0_timer_isr(void) {
    __asm__ volatile (
         
















































        "testb $3, 8(%%rsp)\n\t"    
        "jz 1f\n\t"
        "swapgs\n\t"
        "1:\n\t"
        "push $0\n\t"               
        "push %%rax\n\t"
        "push %%rbx\n\t"
        "push %%rcx\n\t"
        "push %%rdx\n\t"
        "push %%rbp\n\t"
        "push %%rsi\n\t"
        "push %%rdi\n\t"
        "push %%r8\n\t"
        "push %%r9\n\t"
        "push %%r10\n\t"
        "push %%r11\n\t"
        "push %%r12\n\t"
        "push %%r13\n\t"
        "push %%r14\n\t"
        "push %%r15\n\t"
        "mov %%rsp, %%rdi\n\t"      
        "call timer_isr_c\n\t"      
        "mov %%rax, %%rsp\n\t"      
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%r11\n\t"
        "pop %%r10\n\t"
        "pop %%r9\n\t"
        "pop %%r8\n\t"
        "pop %%rdi\n\t"
        "pop %%rsi\n\t"
        "pop %%rbp\n\t"
        "pop %%rdx\n\t"
        "pop %%rcx\n\t"
        "pop %%rbx\n\t"
        "pop %%rax\n\t"
        "add $8, %%rsp\n\t"         
         




        "testb $3, 8(%%rsp)\n\t"
        "jz 2f\n\t"
        "swapgs\n\t"
        "2:\n\t"
        "iretq\n\t"
        ::: "memory"
    );
}
 



__attribute__((used))
static void irq_dispatch(uint8_t num, cpu_frame_t *frame);
ISR_STUB_IRQ(irq_1,  0x21, irq_dispatch)
ISR_STUB_IRQ(irq_2,  0x22, irq_dispatch)
ISR_STUB_IRQ(irq_3,  0x23, irq_dispatch)
ISR_STUB_IRQ(irq_4,  0x24, irq_dispatch)
ISR_STUB_IRQ(irq_5,  0x25, irq_dispatch)
ISR_STUB_IRQ(irq_6,  0x26, irq_dispatch)
ISR_STUB_IRQ(irq_7,  0x27, irq_dispatch)
ISR_STUB_IRQ(irq_8,  0x28, irq_dispatch)
ISR_STUB_IRQ(irq_9,  0x29, irq_dispatch)
ISR_STUB_IRQ(irq_10, 0x2A, irq_dispatch)
ISR_STUB_IRQ(irq_11, 0x2B, irq_dispatch)
ISR_STUB_IRQ(irq_12, 0x2C, irq_dispatch)
ISR_STUB_IRQ(irq_13, 0x2D, irq_dispatch)
ISR_STUB_IRQ(irq_14, 0x2E, irq_dispatch)
ISR_STUB_IRQ(irq_15, 0x2F, irq_dispatch)
/* NVMe: 16 vectors 0x50..0x5F (MSI-X, one per queue) */
ISR_STUB_IRQ(irq_nvme_0, 0x50, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_1, 0x51, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_2, 0x52, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_3, 0x53, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_4, 0x54, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_5, 0x55, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_6, 0x56, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_7, 0x57, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_8, 0x58, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_9, 0x59, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_10, 0x5A, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_11, 0x5B, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_12, 0x5C, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_13, 0x5D, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_14, 0x5E, irq_dispatch)
ISR_STUB_IRQ(irq_nvme_15, 0x5F, irq_dispatch)

 


static void pic_remap(uint8_t off1, uint8_t off2) {
    uint8_t m1 = inb(0x21), m2 = inb(0xA1);
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, off1); outb(0xA1, off2);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, m1); outb(0xA1, m2);
}
static void pic_disable(void) { outb(0x21, 0xFF); outb(0xA1, 0xFF); }

#define APIC_BASE_MSR    0x1B
#define APIC_SVR         0xF0
#define APIC_EOI         0xB0
#define APIC_LVT_TIMER   0x320
#define APIC_LVT_LINT0   0x350
#define APIC_LVT_LINT1   0x360
#define APIC_LVT_ERR     0x370
#define APIC_INIT_CNT    0x380
#define APIC_CURR_CNT    0x390
#define APIC_DIV_CONF    0x3E0
#define IOAPIC_ID   0x00
#define IOAPIC_VER  0x01
#define IOAPIC_RED  0x10

static volatile uint32_t *g_lapic_base;
static volatile uint32_t *g_ioapic_base;

static inline void lapic_write(uint32_t reg, uint32_t val) { g_lapic_base[reg >> 2] = val; __asm__ volatile ("mfence" ::: "memory"); }
static inline uint32_t lapic_read(uint32_t reg) { uint32_t v = g_lapic_base[reg >> 2]; __asm__ volatile ("mfence" ::: "memory"); return v; }
static inline void ioapic_write(uint32_t reg, uint32_t val) { g_ioapic_base[0] = reg; __asm__ volatile ("mfence" ::: "memory"); g_ioapic_base[4] = val; __asm__ volatile ("mfence" ::: "memory"); }
static inline uint32_t ioapic_read(uint32_t reg) { g_ioapic_base[0] = reg; __asm__ volatile ("mfence" ::: "memory"); uint32_t v = g_ioapic_base[4]; __asm__ volatile ("mfence" ::: "memory"); return v; }

void apic_send_eoi(uint8_t vector) { (void)vector; lapic_write(APIC_EOI, 0); }

uint32_t interrupt_get_apic_id(void) {
    return lapic_read(0x020) >> 24;
}

static void apic_init(void) {
    uint64_t msr = rdmsr(APIC_BASE_MSR);
    g_lapic_base = (volatile uint32_t *)(uintptr_t)((msr & 0xFFFFFFFFFFFFF000ULL) + DIRECT_MAP_OFF);
    lapic_write(APIC_SVR, 0x100 | 0xFF);
    lapic_write(APIC_LVT_LINT0, 0x10000 | 0xFF);
    lapic_write(APIC_LVT_LINT1, 0x10000 | 0xFF);
    lapic_write(APIC_LVT_ERR, 0x10000 | 0xFF);
}

static void ioapic_init(uint64_t phys_addr) {
    g_ioapic_base = (volatile uint32_t *)(uintptr_t)(phys_addr + DIRECT_MAP_OFF);
    uint32_t ver = ioapic_read(IOAPIC_VER);
    uint8_t entries = ((ver >> 16) & 0xFF) + 1;
    for (uint8_t i = 0; i < entries; i++) {
        ioapic_write(IOAPIC_RED + (2 * i), 0x00010000 | (IRQ_BASE + i));
        ioapic_write(IOAPIC_RED + (2 * i) + 1, 0x00000000);
    }
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic_id, bool level, bool low_pol) {
    uint32_t lo = vector | (1 << 8);
    if (level) lo |= (1 << 15);
    if (low_pol) lo |= (1 << 13);
    ioapic_write(IOAPIC_RED + (2 * irq), lo);
    ioapic_write(IOAPIC_RED + (2 * irq) + 1, dest_lapic_id << 24);
}

 


static volatile bool g_pit_calib_done = false;
static uint32_t g_apic_bus_freq = 0;

__attribute__((interrupt))
void pit_calib_stub(cpu_frame_t *frame) {
    (void)frame;
    g_pit_calib_done = true;
     
    outb(0x20, 0x20);   
     
    lapic_write(APIC_EOI, 0);
     
    outb(0x21, inb(0x21) | 0x01);
}

static void apic_calibrate_timer(void) {
    uint16_t cs; __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    idt_set_gate(0x20, pit_calib_stub, cs, 0, IDT_GATE_INT);
    idt_load();

     
    lapic_write(APIC_LVT_LINT0, 0x00700);   

     
    uint16_t divisor = 11931;
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

     
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    lapic_write(APIC_DIV_CONF, 0x03);   
    lapic_write(APIC_LVT_TIMER, 0x10000 | 0xFF);   
    lapic_write(APIC_INIT_CNT, 0xFFFFFFFF);

    g_pit_calib_done = false;
    sti();

     
    uint32_t timeout = 10000000;
    while (!g_pit_calib_done && timeout--) __asm__ volatile ("pause");

    cli();

    uint32_t elapsed = 0xFFFFFFFF - lapic_read(APIC_CURR_CNT);

     
    lapic_write(APIC_LVT_LINT0, 0x10000 | 0xFF);

    pic_disable();

    if (g_pit_calib_done && elapsed > 1000) {
         









        g_apic_bus_freq = elapsed * 100 * 16;   
    } else {
         
        g_apic_bus_freq = 100000000;   
    }
}

static volatile uint64_t g_timer_ticks = 0;
static volatile uint32_t g_timer_hz = 0;

uint64_t interrupt_get_ticks(void) { return __atomic_load_n(&g_timer_ticks, __ATOMIC_RELAXED); }
uint32_t interrupt_get_timer_hz(void) { return g_timer_hz; }

static void apic_timer_start(uint32_t freq_hz) {
    if (g_apic_bus_freq == 0) g_apic_bus_freq = 100000000;
    g_timer_hz = freq_hz;
    uint32_t div = 16;
    uint32_t init_cnt = (g_apic_bus_freq / div) / freq_hz;

     
    DBG_VAL("KR", "apic_timer_start: g_apic_bus_freq", (uint64_t)g_apic_bus_freq);
    DBG_VAL("KR", "apic_timer_start: init_cnt", (uint64_t)init_cnt);

    lapic_write(APIC_DIV_CONF, 0x03);
    lapic_write(APIC_INIT_CNT, init_cnt);
    lapic_write(APIC_LVT_TIMER, 0x20000 | TIMER_VECTOR);
}

 


typedef uint64_t (*scheduler_tick_fn_t)(uint64_t frame_rsp);
static scheduler_tick_fn_t g_sched_tick = NULL;

int interrupt_register_handler(uint16_t vector, isr_handler_t handler) {
    if (vector >= IDT_ENTRIES) return -1;
    g_isr_handlers[vector] = handler;
    return 0;
}

int interrupt_unregister_handler(uint16_t vector) {
    if (vector >= IDT_ENTRIES) return -1;
    g_isr_handlers[vector] = NULL;
    return 0;
}

void interrupt_set_scheduler_tick(scheduler_tick_fn_t fn) { g_sched_tick = fn; }
void interrupt_enable(void) { sti(); }
void interrupt_disable(void) { cli(); }

 



uint64_t timer_isr_c(uint64_t frame_rsp) {
    apic_send_eoi(TIMER_VECTOR);
    __atomic_fetch_add(&g_timer_ticks, 1, __ATOMIC_RELAXED);
    if (g_sched_tick) return g_sched_tick(frame_rsp);
    return frame_rsp;
}

int interrupt_init(uint64_t ioapic_phys_addr, uint32_t timer_freq_hz) {
    DBG_MSG("KR", "14a interrupt_init: pic");
    for (size_t i = 0; i < IDT_ENTRIES; i++) {
        g_isr_handlers[i] = NULL;
        __asm__ volatile ("" : "+m"(g_idt[i]) :: "memory");
    }
    pic_remap(0x20, 0x28);
    pic_disable();
    DBG_MSG("KR", "14b apic");
    apic_init();
    DBG_MSG("KR", "14c ioapic");
    ioapic_init(ioapic_phys_addr);
    DBG_MSG("KR", "14d timer calibrate");
    apic_calibrate_timer();

    g_isr_handlers[14] = NULL;

    uint16_t cs; __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

     
    idt_set_gate(0,  exc_0,  cs, 0, IDT_GATE_INT);
    idt_set_gate(1,  exc_1,  cs, 0, IDT_GATE_TRAP);
    idt_set_gate(2,  exc_2,  cs, 0, IDT_GATE_INT);
    idt_set_gate(3,  exc_3,  cs, 0, IDT_GATE_TRAP);
    idt_set_gate(4,  exc_4,  cs, 0, IDT_GATE_TRAP);
    idt_set_gate(5,  exc_5,  cs, 0, IDT_GATE_INT);
    idt_set_gate(6,  exc_6,  cs, 0, IDT_GATE_INT);
    idt_set_gate(7,  exc_7,  cs, 0, IDT_GATE_INT);
    idt_set_gate(8,  exc_8,  cs, 0, IDT_GATE_INT);
    idt_set_gate(9,  exc_9,  cs, 0, IDT_GATE_INT);
    idt_set_gate(10, exc_10, cs, 0, IDT_GATE_INT);
    idt_set_gate(11, exc_11, cs, 0, IDT_GATE_INT);
    idt_set_gate(12, exc_12, cs, 0, IDT_GATE_INT);
    idt_set_gate(13, exc_13, cs, 0, IDT_GATE_INT);
    idt_set_gate(14, pf_entry, cs, 0, IDT_GATE_INT);
    idt_set_gate(15, exc_15, cs, 0, IDT_GATE_INT);
    idt_set_gate(16, exc_16, cs, 0, IDT_GATE_INT);
    idt_set_gate(17, exc_17, cs, 0, IDT_GATE_INT);
    idt_set_gate(18, exc_18, cs, 0, IDT_GATE_INT);
    idt_set_gate(19, exc_19, cs, 0, IDT_GATE_INT);
    idt_set_gate(20, exc_20, cs, 0, IDT_GATE_INT);
    idt_set_gate(30, exc_30, cs, 0, IDT_GATE_INT);

     
    idt_set_gate(0x20, irq0_timer_isr, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x21, irq_1,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x22, irq_2,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x23, irq_3,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x24, irq_4,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x25, irq_5,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x26, irq_6,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x27, irq_7,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x28, irq_8,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x29, irq_9,  cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2A, irq_10, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2B, irq_11, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2C, irq_12, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2D, irq_13, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2E, irq_14, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x2F, irq_15, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x50, irq_nvme_0, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x51, irq_nvme_1, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x52, irq_nvme_2, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x53, irq_nvme_3, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x54, irq_nvme_4, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x55, irq_nvme_5, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x56, irq_nvme_6, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x57, irq_nvme_7, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x58, irq_nvme_8, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x59, irq_nvme_9, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5A, irq_nvme_10, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5B, irq_nvme_11, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5C, irq_nvme_12, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5D, irq_nvme_13, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5E, irq_nvme_14, cs, 0, IDT_GATE_INT);
    idt_set_gate(0x5F, irq_nvme_15, cs, 0, IDT_GATE_INT);

    DBG_MSG("KR", "14e idt_load");
    idt_load();
    apic_timer_start(timer_freq_hz ? timer_freq_hz : 100);
    DBG_MSG("KR", "14f sti");
    sti();
    return 0;
}

#ifdef __cplusplus
}
#endif