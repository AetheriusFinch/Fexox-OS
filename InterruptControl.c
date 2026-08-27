/*
InterruptControl.c
Production-ready x86_64 Interrupt & Exception Manager
ИСПРАВЛЕНО: C23 bool, удален no_caller_saved_registers, исправлены опечатки
*/
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "debug_out.h"

/* ==========================================================================
БАЗОВЫЕ ТИПЫ И МАКРОСЫ
========================================================================== */
typedef uint64_t           phys_addr_t;
typedef uint64_t           virt_addr_t;

/* C23 совместимость для bool */
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

/* ==========================================================================
CPU FRAME & IDT СТРУКТУРЫ
========================================================================== */
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

/* forward decl: нужна в irq_dispatch, определена ниже */
void apic_send_eoi(uint8_t vector);

/* ==========================================================================
АППАРАТНЫЕ ОПЕРАЦИИ
========================================================================== */
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

/* ==========================================================================
IDT УПРАВЛЕНИЕ
========================================================================== */
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

/* ==========================================================================
VGA PANIC & PAGE FAULT HANDLER
========================================================================== */
static void vga_panic(const char *msg) {
    /*
     * ВАЖНО: после vmm_activate() физический адрес 0xB8000 не обязательно
     * замаплен 1:1 в текущем адресном пространстве. Прямая запись по этому
     * адресу может вызвать вложенный #PF -> #DF -> triple fault, из-за
     * чего сообщение об ошибке никогда не будет видно (система просто
     * "зависает"/перезагружается без вывода). Поэтому выводим панику ТОЛЬКО
     * через COM1 (порт ввода-вывода, не требует MMU).
     */
    dbg_puts("\n*** KERNEL PANIC ***\n");
    dbg_puts(msg);
    dbg_puts("\n*** halted ***\n");
    while (1) hlt();
}

static void format_hex(uint64_t val, char *buf) {
    const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x'; buf[2] = '\0';
    int pos = 2;
    for (int i = 60; i >= 0; i -= 4) buf[pos++] = hex[(val >> i) & 0xF];
    buf[pos] = '\0';
}

/*
 * ИСПРАВЛЕНО (см. большой комментарий у irq0_timer_isr ниже, тот же класс
 * бага): page_fault_handler() и ВСЕ exc_N() раньше были объявлены как
 * __attribute__((interrupt)) с аргументом типа cpu_frame_t* — большой
 * "программной" структурой (r15..rax, error_code, rip, cs, rflags, rsp, ss,
 * 20 полей), которую сам компилятор туда НЕ кладёт. __attribute__((interrupt))
 * сам решает, какие регистры и в каком порядке сохранить (это ЕГО личный
 * пролог, не наш), а реальный "frame", который он передаёт — это только
 * то, что аппаратно положил CPU (error_code[если есть]/rip/cs/rflags/rsp/ss),
 * причём часто СРАЗУ после ЕГО собственных push'ей callee-saved регистров,
 * а не по смещениям cpu_frame_t. Из-за этого frame->rip/frame->cs/frame->rsp
 * читались из совершенно случайных мест на стеке — отсюда в логе паники
 * "RIP : 0x0000000000000000 / CS : 0x0 / RSP : 0x0" при абсолютно реальном
 * (и ненулевом) page fault (ADDR/ERR читались верно — cr2 и код ошибки
 * компилятор передаёт как обычные значения, а не через this же "frame").
 *
 * Фикс: ручные naked-стабы (ISR_STUB_NOERR/ISR_STUB_ERR ниже), которые
 * сами пушат GPR В ТОМ ЖЕ порядке, что и setup_kstack()/irq0_timer_isr —
 * тогда frame реально указывает на валидный cpu_frame_t и дамп паники
 * показывает НАСТОЯЩИЙ RIP/CS/RSP упавшего кода (и можно будет понять,
 * ring3-приложение упало или сам код ядра).
 */
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

    static char panic_msg[400];
    int pos = 0;

    const char *prefix = "KERNEL PAGE FAULT\nADDR: 0x";
    for (int i = 0; prefix[i]; i++) panic_msg[pos++] = prefix[i];
    for (int i = 0; addr_buf[i]; i++) panic_msg[pos++] = addr_buf[i];

    const char *mid = "\nERR : 0x";
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

    const char *rip_lbl = "\nRIP : 0x";
    for (int i = 0; rip_lbl[i]; i++) panic_msg[pos++] = rip_lbl[i];
    for (int i = 0; rip_buf[i]; i++) panic_msg[pos++] = rip_buf[i];

    const char *cs_lbl = "\nCS  : 0x";
    for (int i = 0; cs_lbl[i]; i++) panic_msg[pos++] = cs_lbl[i];
    for (int i = 0; cs_buf[i]; i++) panic_msg[pos++] = cs_buf[i];

    const char *rsp_lbl = "\nRSP : 0x";
    for (int i = 0; rsp_lbl[i]; i++) panic_msg[pos++] = rsp_lbl[i];
    for (int i = 0; rsp_buf[i]; i++) panic_msg[pos++] = rsp_buf[i];
    panic_msg[pos] = '\0';

    vga_panic(panic_msg);
}

/* ==========================================================================
DISPATCHERS & STUBS
========================================================================== */
/* __attribute__((used)) ОБЯЗАТЕЛЕН: единственный вызов этой static-функции —
 * из текста инлайн-асма (ISR_STUB_NOERR/ISR_STUB_ERR ниже). Компилятор не
 * разбирает строки инлайн-асма и не видит там вызова, поэтому с его точки
 * зрения это "неиспользуемая static-функция" — и он (даже на -O0) не
 * эмиттирует её в объектник вовсе. Без атрибута: "call exception_dispatch"
 * в асме ссылается на символ, которого физически нет в .o -> ld.lld:
 * undefined symbol. */
__attribute__((used))
static void exception_dispatch(uint8_t num, uint64_t err, cpu_frame_t *frame);

/* -------------------------------------------------------------------------
 * ISR_STUB_NOERR / ISR_STUB_ERR — генерируют naked-обработчик вектора NUM,
 * который аппаратно-корректно строит cpu_frame_t (см. большой комментарий
 * выше у page_fault_handler_c) и зовёт HANDLER(NUM, err, &frame).
 *
 * Порядок push ДОЛЖЕН точно совпадать с cpu_frame_t и с тем, что строит
 * setup_kstack() в Scheduler.c (см. комментарий там же):
 *   r15,r14,r13,r12,r11,r10,r9,r8, rdi,rsi,rbp,rdx,rcx,rbx,rax, error_code,
 *   [rip,cs,rflags,rsp,ss — уже положены самим CPU].
 * Значит физический порядок push-инструкций (каждый push кладёт значение
 * НИЖЕ по адресу) должен быть обратным списку полей: сначала (fake)
 * error_code, потом rax,rbx,rcx,rdx,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,
 * и r15 — последним (r15 окажется по самому маленькому адресу = struct
 * offset 0, ровно как ожидает cpu_frame_t).
 *
 * ISR_STUB_NOERR: у вектора нет аппаратного error_code — пушим фиктивный
 * $0 сами (как и в irq0_timer_isr), чтобы смещения совпали с cpu_frame_t.
 *
 * ISR_STUB_ERR: аппаратный error_code уже лежит на вершине стека (CPU
 * кладёт его ДО rip/cs/rflags/rsp/ss) — фиктивный push НЕ нужен, реальное
 * значение читаем после наших push'ей по смещению 120(%rsp) (offsetof
 * error_code в cpu_frame_t) и передаём вторым аргументом.
 * ------------------------------------------------------------------------- */
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
        "push $0\n\t"                 /* фиктивный error_code */ \
        ISR_PUSH_GPR \
        "mov %%rsp, %%rdx\n\t"        /* 3-й аргумент: cpu_frame_t* */ \
        "xor %%esi, %%esi\n\t"        /* 2-й аргумент: err = 0 */ \
        "mov $" #NUM ", %%edi\n\t"    /* 1-й аргумент: num */ \
        "call " #HANDLER "\n\t" \
        ISR_POP_GPR \
        "add $8, %%rsp\n\t"           /* убрать фиктивный error_code */ \
        "iretq\n\t" \
        ::: "memory" \
    ); \
}

/* Для IRQ (irq_dispatch(num, frame) — 2 аргумента, без error_code, вектор
 * не в SysV-регистре err, а сразу num/frame): та же безопасная сборка
 * cpu_frame_t, что и ISR_STUB_NOERR, но с другой сигнатурой вызова. */
#define ISR_STUB_IRQ(NAME, VEC, HANDLER) \
__attribute__((naked)) void NAME(void) { \
    __asm__ volatile ( \
        "push $0\n\t"                 /* фиктивный error_code */ \
        ISR_PUSH_GPR \
        "mov %%rsp, %%rsi\n\t"        /* 2-й аргумент: cpu_frame_t* */ \
        "mov $" #VEC ", %%edi\n\t"    /* 1-й аргумент: вектор */ \
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
        "mov %%rsp, %%rdx\n\t"        /* 3-й аргумент: cpu_frame_t* */ \
        "mov 120(%%rsp), %%rsi\n\t"   /* 2-й аргумент: реальный error_code */ \
        "mov $" #NUM ", %%edi\n\t"    /* 1-й аргумент: num */ \
        "call " #HANDLER "\n\t" \
        ISR_POP_GPR \
        "add $8, %%rsp\n\t"           /* убрать error_code перед iretq */ \
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

    /* ВАЖНО: раньше здесь печаталось только имя исключения (frame
     * игнорировался — (void)frame), поэтому для любого исключения КРОМЕ
     * #PF (у него свой обработчик с полным дампом) в логе не было ни
     * RIP, ни CS/RSP — только "#UD Invalid Opcode" без единой зацепки,
     * где именно в коде это произошло и был ли это ring0 или ring3
     * (см. calc.elf/syslogs.elf — CS сразу скажет, ring3-код упал или
     * сам обработчик прерывания/переключения контекста в ядре). Дублируем
     * тот же формат, что и в page_fault_handler ниже. */
    char rip_buf[20], cs_buf[20], rsp_buf[20], err_buf[20];
    format_hex(frame->rip, rip_buf);
    format_hex(frame->cs,  cs_buf);
    format_hex(frame->rsp, rsp_buf);
    format_hex(err,        err_buf);

    static char panic_msg[300];
    int pos = 0;
    const char *name = (num < 32) ? exc_names[num] : "Unknown Exception";
    for (int i = 0; name[i]; i++) panic_msg[pos++] = name[i];

    const char *err_lbl = "\nERR : 0x";
    for (int i = 0; err_lbl[i]; i++) panic_msg[pos++] = err_lbl[i];
    for (int i = 0; err_buf[i]; i++) panic_msg[pos++] = err_buf[i];

    const char *rip_lbl = "\nRIP : 0x";
    for (int i = 0; rip_lbl[i]; i++) panic_msg[pos++] = rip_lbl[i];
    for (int i = 0; rip_buf[i]; i++) panic_msg[pos++] = rip_buf[i];

    const char *cs_lbl = "\nCS  : 0x";
    for (int i = 0; cs_lbl[i]; i++) panic_msg[pos++] = cs_lbl[i];
    for (int i = 0; cs_buf[i]; i++) panic_msg[pos++] = cs_buf[i];

    const char *rsp_lbl = "\nRSP : 0x";
    for (int i = 0; rsp_lbl[i]; i++) panic_msg[pos++] = rsp_lbl[i];
    for (int i = 0; rsp_buf[i]; i++) panic_msg[pos++] = rsp_buf[i];
    panic_msg[pos] = '\0';

    vga_panic(panic_msg);
}

__attribute__((used))
static void irq_dispatch(uint8_t num, cpu_frame_t *frame) {
    if (g_isr_handlers[num]) g_isr_handlers[num](num, 0, frame);
    /* КРИТИЧНО: без EOI LAPIC считает вектор "всё ещё в обработке"
     * и никогда не доставит следующее прерывание этого вектора
     * (таймер, клавиатура и т.п.). PIC отключён (pic_disable() в
     * interrupt_init) — работаем только через LAPIC, поэтому шлём
     * EOI туда, а не в 8259. Раньше EOI не отправлялся вообще,
     * из-за чего таймер срабатывал ровно один раз за всю работу
     * ядра — отсюда "часы не тикают" и "курсор не обновляется"
     * (ui_tick вызывается только из sched_tick, который висит на
     * этом же таймерном прерывании). */
    apic_send_eoi(num);
}

/* --- Исключения БЕЗ кода ошибки (naked, корректный cpu_frame_t) --- */
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

/* --- Исключения С кодом ошибки (naked, корректный cpu_frame_t) --- */
ISR_STUB_ERR(exc_8,  8,  exception_dispatch)
ISR_STUB_ERR(exc_10, 10, exception_dispatch)
ISR_STUB_ERR(exc_11, 11, exception_dispatch)
ISR_STUB_ERR(exc_12, 12, exception_dispatch)
ISR_STUB_ERR(exc_13, 13, exception_dispatch)
ISR_STUB_ERR(exc_17, 17, exception_dispatch)
ISR_STUB_ERR(exc_30, 30, exception_dispatch)

/* --- #PF (14) — свой обработчик с полным дампом, тот же механизм --- */
ISR_STUB_ERR(pf_entry, 14, page_fault_handler_c)

/* --- IRQ Handlers --- */
/*
 * irq0_timer_isr — обработчик таймера (LAPIC, вектор 0x20).
 *
 * ПОЧЕМУ НЕ __attribute__((interrupt)):
 * Этот атрибут сам решает, какие регистры сохранять и в каком порядке —
 * это деталь реализации компилятора, НЕ ABI, и адресация "лишних" полей
 * cpu_frame_t (r15..rax, error_code), которые атрибут не обязан класть
 * именно туда, была undefined behavior. Пока обработчики (page fault,
 * exceptions) эти поля не читали — всё "работало". Но планировщику нужно
 * реально переключать RSP на стек другой задачи, а для этого нужен
 * ПОЛНЫЙ контроль над тем, что и в каком порядке кладётся/снимается со
 * стека. Отсюда — ручной naked-стаб.
 *
 * Смещения полей должны точно совпадать с cpu_frame_t (и sched_frame_t
 * в Scheduler.c) и с тем, что строит setup_kstack() для новых задач:
 *   r15,r14,r13,r12,r11,r10,r9,r8, rdi,rsi,rbp,rdx,rcx,rbx,rax,
 *   error_code, rip, cs, rflags, [rsp, ss — не используются iretq
 *   при переходе ring0->ring0, но хранятся для симметрии].
 *
 * Логика:
 *   1. Кладём фиктивный error_code (у IRQ0 его нет).
 *   2. Пушим регистры в порядке, который после всех push даёт RSP,
 *      указывающий ровно на начало cpu_frame_t (поле r15).
 *   3. Зовём timer_isr_c(rsp) — она шлёт EOI, вызывает планировщик и
 *      возвращает RSP, с которого продолжать: тот же (без переключения)
 *      либо RSP другой задачи (реальное переключение стека).
 *   4. Переключаем RSP на то, что вернула timer_isr_c.
 *   5. Снимаем регистры в обратном порядке с ЭТОГО (возможно, чужого)
 *      стека и делаем iretq — так мы восстанавливаем чужие регистры
 *      и продолжаем выполнение чужой задачи.
 */
__attribute__((naked)) void irq0_timer_isr(void) {
    __asm__ volatile (
        /* =====================================================================
         * КРИТИЧНЫЙ ФИКС (паника при открытии 2-го usermode-приложения):
         *
         * usermode_init() (main.c) держит инвариант "GS.base == &g_percpu
         * ВСЕГДА, пока выполняется код ядра (ring0)". user_entry_trampoline()
         * (ELF64.c) и syscall_entry_stub() (main.c) поддерживают этот
         * инвариант КАЖДЫЙ явным парным swapgs на СВОИХ собственных
         * переходах ring3<->ring0. Но у IDT interrupt-gate вход по
         * аппаратному прерыванию (наш случай — LAPIC-таймер) НЕ делает
         * swapgs автоматически — а этот стаб раньше вообще не трогал GS.
         *
         * Пока в системе только ОДНА ring3-задача, это "случайно" не
         * стреляло: клик мышью (ui_tick() ниже) чаще всего попадал на тик,
         * прервавший ЕЁ ЖЕ синхронный SYS_SCHED_YIELD (см. sched_yield() в
         * Scheduler.c) — а там GS.base к моменту тика УЖЕ = &g_percpu
         * (мы внутри syscall_entry_stub, между его входным и выходным
         * swapgs), так что "случайная" база совпадала с тем, что дальше
         * безусловно предполагает user_entry_trampoline().
         *
         * При открытии ВТОРОГО приложения этот тик почти гарантированно
         * прерывает первое приложение прямо в ring3 (обычный код, а не
         * внутри syscall) — там GS.base = 0 (user-сторона). elf64_spawn()
         * для второго приложения вызывается прямо тут, внутри ui_tick(),
         * создаёт kthread-трамплин, переключение на него происходит НИЖЕ
         * по этому же стабу, и когда task_wrapper() затем зовёт
         * user_entry_trampoline(), тот делает БЕЗУСЛОВНЫЙ swapgs, думая,
         * что уходит из ring0(&g_percpu) в ring3(0) — а на деле уходит из
         * ring3(0) в "ring3"(&g_percpu): GS.base после этого = &g_percpu,
         * хотя код уже выполняется в ring3! Первый же SYSCALL (любого из
         * двух процессов) делает СВОЙ swapgs поверх уже сбитой пары —
         * syscall_entry_stub пишет "mov %%rsp, %%gs:8" по адресу
         * (0 + 8) = 0x8 либо соседний код читает/пишет по gs:0 — отсюда
         * #PF ADDR=0x0, ERR=W, ровно как в панике из багрепорта.
         *
         * Фикс — классический подход (как в Linux entry_64.S): swapgs
         * ЗДЕСЬ должен быть УСЛОВНЫМ, а не безусловным отсутствующим.
         * Проверяем CPL сохранённого CS (бит RPL=0..1 в младших битах):
         *   - на ВХОДЕ: если прервали ring3 (CS.RPL==3) — GS.base сейчас
         *     0(user), делаем swapgs -> &g_percpu(kernel) на время работы
         *     обработчика/планировщика. Если прервали ring0 — GS.base уже
         *     &g_percpu (инвариант), swapgs НЕ делаем.
         *   - на ВЫХОДЕ: смотрим CS кадра, В КОТОРЫЙ реально возвращаемся
         *     (после возможного переключения задачи это может быть СОВСЕМ
         *     другая задача!). Если это ring3 — делаем swapgs обратно в
         *     user(0). Если это ring0 (например task_wrapper только что
         *     созданного треда) — НЕ делаем, оставляем &g_percpu, и тогда
         *     последующий безусловный swapgs внутри user_entry_trampoline()
         *     снова видит корректную базовую точку отсчёта.
         * ===================================================================== */
        "testb $3, 8(%%rsp)\n\t"   /* CS.RPL прерванного кадра (RIP=+0,CS=+8) */
        "jz 1f\n\t"
        "swapgs\n\t"
        "1:\n\t"
        "push $0\n\t"              /* фиктивный error_code */
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
        "mov %%rsp, %%rdi\n\t"     /* rdi = &cpu_frame_t (аргумент №1 SysV) */
        "call timer_isr_c\n\t"     /* rax = RSP, с которого продолжать */
        "mov %%rax, %%rsp\n\t"     /* переключаемся на (возможно чужой) стек */
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
        "add $8, %%rsp\n\t"        /* убрать фиктивный error_code */
        /* Симметричная проверка на выходе — см. большой комментарий выше.
         * ВАЖНО: смотрим CS кадра, на который сейчас указывает %rsp — это
         * кадр ЦЕЛЕВОЙ задачи (после возможного переключения), не обязательно
         * той же, что была прервана на входе. testb/jz не трогают регистры,
         * которые мы только что восстановили pop'ами выше — safe перед iretq. */
        "testb $3, 8(%%rsp)\n\t"
        "jz 2f\n\t"
        "swapgs\n\t"
        "2:\n\t"
        "iretq\n\t"
        ::: "memory"
    );
}
/* __attribute__((used)) — та же причина, что и у exception_dispatch выше:
 * вызывается только из текста инлайн-асма (ISR_STUB_IRQ), компилятор
 * этого не видит и без атрибута выкинет функцию из объектника как
 * "неиспользуемую static" -> undefined symbol на этапе линковки. */
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

/* ==========================================================================
PIC, APIC & IOAPIC
========================================================================== */
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

/* ==========================================================================
APIC TIMER CALIBRATION
========================================================================== */
static volatile bool g_pit_calib_done = false;
static uint32_t g_apic_bus_freq = 0;

__attribute__((interrupt))
void pit_calib_stub(cpu_frame_t *frame) {
    (void)frame;
    g_pit_calib_done = true;
    /* EOI для PIC (работаем через PIC во время калибровки) */
    outb(0x20, 0x20);  /* master PIC EOI */
    /* EOI для LAPIC на всякий случай */
    lapic_write(APIC_EOI, 0);
    /* Маскируем IRQ0 чтобы больше не прерывал */
    outb(0x21, inb(0x21) | 0x01);
}

static void apic_calibrate_timer(void) {
    uint16_t cs; __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    idt_set_gate(0x20, pit_calib_stub, cs, 0, IDT_GATE_INT);
    idt_load();

    /* Временно открываем LINT0 в режиме ExtINT чтобы PIC IRQ0 дошёл до CPU */
    lapic_write(APIC_LVT_LINT0, 0x00700);  /* ExtINT, не маскирован */

    /* PIT: ~10 мс (divisor=11931, частота 1193182 Гц) */
    uint16_t divisor = 11931;
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    /* Разрешаем только IRQ0 на master PIC */
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    lapic_write(APIC_DIV_CONF, 0x03);  /* делитель /16 */
    lapic_write(APIC_LVT_TIMER, 0x10000 | 0xFF);  /* маскируем таймер APIC */
    lapic_write(APIC_INIT_CNT, 0xFFFFFFFF);

    g_pit_calib_done = false;
    sti();

    /* Ждём одного тика PIT (~10 мс) */
    uint32_t timeout = 10000000;
    while (!g_pit_calib_done && timeout--) __asm__ volatile ("pause");

    cli();

    uint32_t elapsed = 0xFFFFFFFF - lapic_read(APIC_CURR_CNT);

    /* Закрываем LINT0 обратно */
    lapic_write(APIC_LVT_LINT0, 0x10000 | 0xFF);

    pic_disable();

    if (g_pit_calib_done && elapsed > 1000) {
        /* ВАЖНО (баг): APIC_DIV_CONF был выставлен в 0x03 (÷16) ДО отсчёта,
         * поэтому elapsed тикал на частоте bus_freq/16, а не bus_freq.
         * "elapsed*100" даёт именно bus_freq/16, а не bus_freq/16/16, как
         * ошибочно предполагалось в комментарии ниже (закомментированная
         * строка). apic_timer_start() ниже делит на div=16 ЕЩЁ РАЗ — если
         * здесь хранить bus_freq/16, таймер получается в 16 раз быстрее
         * запрошенных 100 Гц (и это ещё не считая совпадения по времени с
         * другими провалами калибровки). Умножаем обратно на 16, чтобы
         * g_apic_bus_freq всегда означал ИСТИННУЮ (неделённую) частоту шины
         * APIC — ровно как в fallback-ветке ниже (100 МГц, тоже неделённая) */
        g_apic_bus_freq = elapsed * 100 * 16;  /* elapsed был на bus_freq/16 за 10мс */
    } else {
        /* Калибровка не удалась — используем типичное значение для QEMU */
        g_apic_bus_freq = 100000000;  /* 100 МГц */
    }
}

static void apic_timer_start(uint32_t freq_hz) {
    if (g_apic_bus_freq == 0) g_apic_bus_freq = 100000000;
    uint32_t div = 16;
    uint32_t init_cnt = (g_apic_bus_freq / div) / freq_hz;

    /* ВРЕМЕННЫЙ диагностический принт — убрать после отладки. */
    DBG_VAL("KR", "apic_timer_start: g_apic_bus_freq", (uint64_t)g_apic_bus_freq);
    DBG_VAL("KR", "apic_timer_start: init_cnt", (uint64_t)init_cnt);

    lapic_write(APIC_DIV_CONF, 0x03);
    lapic_write(APIC_INIT_CNT, init_cnt);
    lapic_write(APIC_LVT_TIMER, 0x20000 | TIMER_VECTOR);
}

/* ==========================================================================
РЕГИСТРАЦИЯ И ИНИЦИАЛИЗАЦИЯ
========================================================================== */
typedef uint64_t (*scheduler_tick_fn_t)(uint64_t frame_rsp);
static scheduler_tick_fn_t g_sched_tick = NULL;

int interrupt_register_handler(uint16_t vector, isr_handler_t handler) {
    if (vector >= IDT_ENTRIES) return -1;
    g_isr_handlers[vector] = handler;
    return 0;
}

void interrupt_set_scheduler_tick(scheduler_tick_fn_t fn) { g_sched_tick = fn; }
void interrupt_enable(void) { sti(); }
void interrupt_disable(void) { cli(); }

/* Вызывается ИЗ naked-стаба irq0_timer_isr (см. ниже), а не через
 * g_isr_handlers/irq_dispatch — таймеру нужен настоящий сырой доступ
 * к регистрам ради переключения контекста, что несовместимо с
 * __attribute__((interrupt)) (см. комментарий у irq0_timer_isr). */
uint64_t timer_isr_c(uint64_t frame_rsp) {
    apic_send_eoi(TIMER_VECTOR);
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

    /* Exceptions */
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

    /* IRQs */
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