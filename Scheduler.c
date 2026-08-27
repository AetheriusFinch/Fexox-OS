/*
 * Scheduler.c — CFS-подобный планировщик FEXOS
 *
 * Алгоритм выбора задачи:
 *   - Min-heap по vruntime: всегда берём задачу с наименьшим vruntime.
 *   - vruntime += delta_real * (NICE_0_WEIGHT / task->weight)
 *   - Новая задача получает vruntime = min_vruntime очереди (не отстаёт).
 *   - Квант: SCHED_TIMESLICE_NS (4 мс). По истечению — вытеснение.
 *
 * Переключение контекста:
 *   - Таймерный обработчик (sched_tick) вызывается с указателем на cpu_frame_t
 *     на стеке прерывания. Мы подменяем rsp текущей задачи на этот кадр,
 *     берём следующую задачу и восстанавливаем её rsp.
 *   - iret вернёт управление в нужную точку следующей задачи.
 */

#include "Scheduler.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Прямые вызовы для логирования (без kmalloc рекурсии) */
extern void dbg_puts(const char *s);
extern void dbg_hex64(uint64_t v);

/* -------------------------------------------------------------------------
 * Внешние зависимости
 * ------------------------------------------------------------------------- */
extern void  *kmalloc(uint64_t size);
extern void   kfree(void *ptr);
extern void   pmm_free_frames(uint64_t pa, uint64_t count);
extern uint64_t vmm_kernel_pml4_phys(void);
extern void   interrupt_set_scheduler_tick(uint64_t (*fn)(uint64_t frame_rsp));

/* g_active_pml4_phys — физический адрес PML4, реально загруженной в CR3
 * прямо сейчас (0 = ещё ни разу не переключали явно, т.е. активна
 * "исходная" kernel_pml4 из vmm_activate() при загрузке ядра). Только
 * для того, чтобы context_switch() не перезагружал CR3 (и не флашил
 * TLB) без необходимости, когда next работает в том же адресном
 * пространстве, что и уже загружено. */
static uint64_t g_active_pml4_phys = 0;

/* -------------------------------------------------------------------------
 * Spinlock — однопроцессорная реализация через CLI/STI
 * На UP-ядре достаточно маскировать прерывания.
 * Сохраняем/восстанавливаем EFLAGS чтобы sti не вызвал #GP если IF=0.
 * ------------------------------------------------------------------------- */
typedef volatile int spinlock_t;

static inline void spinlock_acquire(spinlock_t *l) {
    /* ВАЖНО: раньше здесь было "pushfq; cli; popfq" — popfq тут же
     * восстанавливал флаги, СОХРАНЁННЫЕ ДО cli, то есть откатывал IF
     * обратно и cli не имел вообще никакого эффекта. Прерывания во
     * время "захваченной" секции никогда реально не выключались.
     * Из-за этого таймерное прерывание могло прилететь прямо посреди
     * rq_push()/сплайса списка (например в sched_create_kthread при
     * добавлении только что созданной задачи) и рекурсивно войти в
     * ЭТУ ЖЕ (ничего не блокирующую) секцию — куча очереди готовности
     * портилась на середине обновления, и свежесозданный поток мог
     * молча пропасть из планирования без единого краша или строки в
     * логе. Теперь реально сохраняем текущий IF и выключаем прерывания;
     * *l используется как хранилище сохранённых флагов (на UP-ядре с
     * cli-блокировками отдельный атомарный флаг не нужен). */
    uint64_t flags;
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r"(flags)
        :
        : "memory"
    );
    *l = (int)flags;
}

static inline void spinlock_release(spinlock_t *l) {
    /* Восстанавливаем IF, только если он реально был установлен —
     * иначе (например при вызове изнутри interrupt-gate обработчика,
     * где IF аппаратно уже 0) мы бы включили прерывания раньше времени,
     * прямо посреди обработки текущего прерывания. */
    if (*l & 0x200) {
        __asm__ volatile ("sti" ::: "memory");
    }
}

/* TSC для измерения времени */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Простое преобразование TSC -> нс (калибруется при init) */
static uint64_t g_tsc_per_ns = 3; /* ~3 ГГц по умолчанию */

static inline uint64_t tsc_to_ns(uint64_t tsc) {
    return tsc / g_tsc_per_ns;
}

/* -------------------------------------------------------------------------
 * Таблица весов (как в Linux, для nice [-20..19])
 * -------------------------------------------------------------------------
 * Каждый шаг ≈ 1.25x. nice=0 → 1024.
 * ------------------------------------------------------------------------- */
static const uint32_t prio_to_weight[40] = {
    /* -20 */  88761, 71755, 56483, 46273, 36291,
    /* -15 */  29154, 23254, 18705, 14949, 11916,
    /* -10 */   9548,  7620,  6100,  4904,  3906,
    /*  -5 */   3121,  2501,  1991,  1586,  1277,
    /*   0 */   1024,   820,   655,   526,   423,
    /*   5 */    335,   272,   215,   172,   137,
    /*  10 */    110,    87,    70,    56,    45,
    /*  15 */     36,    29,    23,    18,    15,
};

static inline uint32_t prio_to_w(int prio) {
    if (prio < SCHED_PRIO_MIN) prio = SCHED_PRIO_MIN;
    if (prio > SCHED_PRIO_MAX) prio = SCHED_PRIO_MAX;
    return prio_to_weight[prio + 20];
}

/* -------------------------------------------------------------------------
 * Min-heap по vruntime
 * Поддерживает до SCHED_MAX_TASKS элементов.
 * ------------------------------------------------------------------------- */
typedef struct {
    task_t  *data[SCHED_MAX_TASKS];
    int      size;
} run_queue_t;

static inline void heap_swap(run_queue_t *h, int a, int b) {
    task_t *tmp = h->data[a];
    h->data[a] = h->data[b];
    h->data[b] = tmp;
    h->data[a]->heap_idx = a;
    h->data[b]->heap_idx = b;
}

static void heap_up(run_queue_t *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[parent]->vruntime <= h->data[i]->vruntime)
            break;
        heap_swap(h, parent, i);
        i = parent;
    }
}

static void heap_down(run_queue_t *h, int i) {
    while (1) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < h->size && h->data[l]->vruntime < h->data[smallest]->vruntime)
            smallest = l;
        if (r < h->size && h->data[r]->vruntime < h->data[smallest]->vruntime)
            smallest = r;
        if (smallest == i) break;
        heap_swap(h, i, smallest);
        i = smallest;
    }
}

static void rq_push(run_queue_t *h, task_t *t) {
    if (h->size >= SCHED_MAX_TASKS) return;
    int i = h->size++;
    h->data[i] = t;
    t->heap_idx = i;
    heap_up(h, i);
}

static task_t *rq_pop(run_queue_t *h) {
    if (h->size == 0) return NULL;
    task_t *top = h->data[0];
    top->heap_idx = -1;
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];
        h->data[0]->heap_idx = 0;
        heap_down(h, 0);
    }
    return top;
}

__attribute__((unused)) static void rq_remove(run_queue_t *h, task_t *t) {
    int i = t->heap_idx;
    if (i < 0 || i >= h->size) return;
    t->heap_idx = -1;
    h->size--;
    if (i < h->size) {
        h->data[i] = h->data[h->size];
        h->data[i]->heap_idx = i;
        heap_up(h, i);
        heap_down(h, i);
    }
}

static inline uint64_t rq_min_vruntime(run_queue_t *h) {
    return h->size > 0 ? h->data[0]->vruntime : 0;
}

/* -------------------------------------------------------------------------
 * Глобальное состояние планировщика
 * ------------------------------------------------------------------------- */
typedef struct {
    run_queue_t  rq;                /* очередь готовых задач */
    task_t      *current;           /* текущая задача */
    task_t      *idle;              /* idle-задача (всегда runnable) */

    /* Двусвязный список всех задач */
    task_t      *all_head;
    uint32_t     task_count;

    /* Счётчик tid */
    uint32_t     next_tid;

    /* min_vruntime: нижняя граница, чтобы новые задачи не получали
       преимущество и старые не получали слишком много "долга" */
    uint64_t     min_vruntime;

    /* Spinlock */
    spinlock_t lock;

    /* Инициализирован? */
    bool         ready;

    /* TSC при старте квантования */
    uint64_t     slice_start_tsc;
} scheduler_t;

static scheduler_t g_sched;

/* -------------------------------------------------------------------------
 * Вспомогательные функции
 * ------------------------------------------------------------------------- */
static void kmemset_s(void *dst, uint8_t val, uint64_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void kstrncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Обновить min_vruntime по текущей очереди и current */
static void update_min_vruntime(void) {
    uint64_t mv = g_sched.min_vruntime;
    if (g_sched.current && g_sched.current != g_sched.idle)
        mv = g_sched.current->vruntime;
    if (g_sched.rq.size > 0) {
        uint64_t rq_min = rq_min_vruntime(&g_sched.rq);
        if (mv == 0 || rq_min < mv) mv = rq_min;
    }
    if (mv > g_sched.min_vruntime)
        g_sched.min_vruntime = mv;
}

/* Выбрать следующую задачу (не удаляя из очереди) */
__attribute__((unused)) static task_t *pick_next(void) {
    if (g_sched.rq.size > 0)
        return g_sched.rq.data[0];
    return g_sched.idle;
}

/* -------------------------------------------------------------------------
 * Idle-задача
 * ------------------------------------------------------------------------- */
static void idle_fn(void *arg) {
    (void)arg;
    while (1)
        __asm__ volatile ("hlt");
}

/* -------------------------------------------------------------------------
 * Выделение и инициализация TCB
 * ------------------------------------------------------------------------- */
static task_t *alloc_task(void) {
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    if (!t) return NULL;
    kmemset_s(t, 0, sizeof(task_t));
    t->heap_idx = -1;
    return t;
}

/* -------------------------------------------------------------------------
 * Подготовка начального стека kernel-потока
 *
 * Стек при первом запуске должен выглядеть как после прерывания:
 *   [стек растёт вниз]
 *   ss        ← rsp+0x28  (фиктивный, kernel ss=0x10)
 *   rsp       ← rsp+0x20  (начальный rsp = вершина стека)
 *   rflags    ← rsp+0x18  (IF=1)
 *   cs        ← rsp+0x10  (kernel cs=0x08)
 *   rip       ← rsp+0x08  (адрес функции)
 *   error     ← rsp+0x00  (0)
 *   + 15 регистров (r15..rax) = 15*8 = 120 байт
 *
 * Итого: cpu_frame_t = 20 * 8 = 160 байт на стеке.
 * task->ctx.rsp = указатель на этот кадр.
 * ------------------------------------------------------------------------- */

/* cpu_frame_t layout (должен совпадать с InterruptControl.c): */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} sched_frame_t;

/* Обёртка: задача завершилась нормально → вызываем sched_exit */
static void task_wrapper(void (*fn)(void *), void *arg) {
    /* ВРЕМЕННЫЙ диагностический маркер — убрать после отладки. Сырой
     * dbg_putc САМЫМ ПЕРВЫМ, до любого другого кода: DBG_MSG сам по себе
     * делает call/несколько push'ей, так что если стек ещё сломан, он
     * может не дожить даже до записи в лог. Если 'W' не появляется —
     * не доходим вообще ни до одной инструкции C-тела task_wrapper
     * (значит проблема в asm-стабе/iretq/бутстрап-кадре). Если 'W' есть,
     * а следующей строки ("entered") — нет, ломается уже на первом call
     * (т.е. именно на текущем RSP). */
    dbg_putc('W');   /* dbg_putc уже доступен через #include "debug_out.h" выше */

    /* ВРЕМЕННЫЙ диагностический принт — убрать после отладки. Если эта
     * строка НЕ появляется в логе, значит система виснет/падает ДО того,
     * как новая задача вообще успевает начать выполняться (т.е. где-то
     * в самом механизме переключения — asm-стаб/iretq/RSP0), а не внутри
     * calc_main/user_entry_trampoline. */
    DBG_MSG("SC", "task_wrapper: entered");
    fn(arg);
    sched_exit();
}

static int setup_kstack(task_t *t, void (*fn)(void *), void *arg) {
    DBG_VAL("SC", "setup_kstack: request size", t->kstack_size);
    uint8_t *stack = (uint8_t *)kmalloc(t->kstack_size);
    DBG_VAL("SC", "setup_kstack: kmalloc result", (uint64_t)(uintptr_t)stack);
    if (!stack) {
        DBG_MSG("SC", "setup_kstack: FAILED - kmalloc returned NULL");
        return -1;
    }
    DBG_VAL("SC", "setup_kstack: zeroing stack", (uint64_t)(uintptr_t)stack);
    kmemset_s(stack, 0, t->kstack_size);
    t->kstack_base = stack;

    /* ИСТИННАЯ вершина стека (выровнена на 16) — это и есть то, что должно
     * идти в TSS.RSP0 (см. tss_set_rsp0() в main.c). RSP0 — стек, который
     * CPU АППАРАТНО подставляет при входе в ring0 из ring3 (в т.ч. на
     * каждом таймерном тике, пока задача работает в ring3) — он должен
     * указывать на ПУСТУЮ, ничем не занятую вершину, а не внутрь уже
     * использованной области. */
    uint8_t *stack_top = stack + t->kstack_size;
    stack_top = (uint8_t *)((uint64_t)stack_top & ~0xFULL);

    /* Резервируем место под sched_frame_t (одноразовый bootstrap-кадр —
     * нужен только для самого первого переключения на task_wrapper).
     * ВАЖНО (баг, из-за которого калькулятор пропадал без следа №2):
     * раньше t->kstack_top присваивался ЭТОТ, УЖЕ ПОНИЖЕННЫЙ на
     * sizeof(sched_frame_t) адрес (базу bootstrap-кадра), а не истинную
     * вершину (stack_top выше). TSS.RSP0 в итоге указывал на 168 байт
     * НИЖЕ настоящего верха стека — то есть ровно на то место, которое
     * уже было "использовано" при самом первом входе в task_wrapper,
     * а не на свободную область. Тот же класс ошибки, что уже чинили
     * для общего g_syscall_kstack (см. комментарий у tss_set_rsp0 в
     * main.c) — только на этот раз внутри одного и того же стека задачи. */
    uint8_t *top = stack_top - sizeof(sched_frame_t);
    sched_frame_t *frame = (sched_frame_t *)top;
    kmemset_s(frame, 0, sizeof(sched_frame_t));

    /* RIP указывает на task_wrapper */
    frame->rip    = (uint64_t)(uintptr_t)task_wrapper;
    frame->cs     = 0x08;                  /* kernel code segment */
    frame->rflags = 0x202;                 /* IF=1, reserved=1 */
    frame->rsp    = (uint64_t)(uintptr_t)stack_top;
    frame->ss     = 0x10;                  /* kernel data segment */

    /* Аргументы task_wrapper через регистры (System V AMD64 ABI):
       rdi = fn, rsi = arg */
    frame->rdi = (uint64_t)(uintptr_t)fn;
    frame->rsi = (uint64_t)(uintptr_t)arg;

    t->ctx.rsp = (uint64_t)(uintptr_t)top;         /* для ПЕРВОГО переключения на задачу */
    t->ctx.cr3 = 0; /* kernel PML4 — берётся из CR3 текущего контекста */
    t->kstack_top = (uint64_t)(uintptr_t)stack_top; /* для TSS.RSP0 — ИСТИННАЯ вершина */
    return 0;
}

/* -------------------------------------------------------------------------
 * sched_init
 * ------------------------------------------------------------------------- */
int sched_init(void) {
    dbg_puts("[SC] sched_init: START\n");
    
    kmemset_s(&g_sched, 0, sizeof(g_sched));
    g_sched.next_tid = 1;
    g_sched.lock     = 0;
    g_sched.ready    = false;

    dbg_puts("[SC] sched_init: calling kmalloc for idle\n");
    task_t *idle = (task_t *)kmalloc(sizeof(task_t));
    dbg_puts("[SC] sched_init: kmalloc result=");
    dbg_hex64((uint64_t)(uintptr_t)idle);
    dbg_puts("\n");
    
    if (!idle) return -1;

    idle->tid      = 0;
    idle->state    = TASK_RUNNABLE;
    idle->priority = SCHED_PRIO_MAX;
    idle->weight   = prio_to_w(SCHED_PRIO_MAX);
    idle->vruntime = 0;
    idle->kstack_size = SCHED_KSTACK_SIZE;
    kstrncpy(idle->name, "idle", sizeof(idle->name));

    if (setup_kstack(idle, idle_fn, NULL) != 0) {
        kfree(idle);
        return -1;
    }

    g_sched.idle     = idle;
    g_sched.current  = idle;
    g_sched.all_head = idle;
    g_sched.task_count = 1;

    interrupt_set_scheduler_tick(sched_tick);

    g_sched.ready = true;
    return 0;
}

/* -------------------------------------------------------------------------
 * sched_create_kthread
 * ------------------------------------------------------------------------- */
int sched_create_kthread(void (*fn)(void *), void *arg,
                         const char *name, int prio) {
    if (!fn) return -1;

    task_t *t = alloc_task();
    if (!t) return -1;

    spinlock_acquire(&g_sched.lock);

    t->tid       = g_sched.next_tid++;
    t->state     = TASK_RUNNABLE;
    t->priority  = prio;
    t->weight    = prio_to_w(prio);
    t->kstack_size = SCHED_KSTACK_SIZE;
    kstrncpy(t->name, name ? name : "kthread", sizeof(t->name));

    /* Новая задача получает min_vruntime чтобы не уйти в долг */
    update_min_vruntime();
    t->vruntime = g_sched.min_vruntime;

    if (setup_kstack(t, fn, arg) != 0) {
        spinlock_release(&g_sched.lock);
        kfree(t);
        return -1;
    }

    /* Добавляем в список всех задач */
    t->next_all = g_sched.all_head;
    if (g_sched.all_head) g_sched.all_head->prev_all = t;
    g_sched.all_head = t;
    g_sched.task_count++;

    /* Добавляем в очередь */
    rq_push(&g_sched.rq, t);

    spinlock_release(&g_sched.lock);

    DBG_VAL("SC", "created tid", t->tid);
    return (int)t->tid;
}

/* -------------------------------------------------------------------------
 * Ядро переключения контекста
 *
 * Вызывается из sched_tick (в контексте прерывания, на СТЕКЕ прерванной
 * задачи). frame_rsp — значение RSP на момент входа в обработчик, т.е.
 * указатель на cpu_frame_t (см. setup_kstack) на СОБСТВЕННОМ стеке
 * прерванной задачи.
 *
 * Что делаем:
 *   1. Запоминаем frame_rsp как ctx.rsp прерванной задачи "как есть" —
 *      это её собственный стек, его содержимое НЕ трогаем.
 *   2. Обновляем vruntime текущей задачи.
 *   3. Если текущая задача ещё RUNNING — переводим в RUNNABLE, кладём в rq.
 *   4. Выбираем следующую задачу.
 *   5. Возвращаем next->ctx.rsp — это RSP на СОБСТВЕННОМ стеке next,
 *      куда naked-стаб (irq0_timer_isr) переключит RSP перед pop+iretq.
 *
 * ВАЖНО: раньше здесь копировалось содержимое next_frame поверх frame
 * (т.е. поверх стека ПРЕРВАННОЙ задачи). Это ломало сохранённое состояние
 * prev — при следующем переключении на неё восстанавливались чужие
 * регистры, что и приводило к #GP. Правильный подход — не копировать
 * данные между стеками, а переключать сам регистр RSP на чужой стек.
 *
 * Мы не переключаем CR3 здесь, т.к. все kernel-потоки в одном адресном
 * пространстве. Для user-mode нужно добавить переключение CR3.
 * ------------------------------------------------------------------------- */
/* tss_set_rsp0 (main.c) — переставляет RSP0 в TSS на стек ядра ТЕКУЩЕЙ
 * задачи. Раньше RSP0 был зафиксирован один раз при загрузке на общий
 * g_syscall_kstack — из-за этого кадр прерывания ring3-процесса (calc.elf)
 * при каждом таймерном тике строился поверх ЧУЖОГО стека и затирался
 * следующим же входом в ядро другой задачи (см. подробный комментарий в
 * main.c рядом с tss_set_rsp0). Без этого вызова RSP3-процесс переживает
 * максимум один квант — после первого же переключения его сохранённый
 * context_switch-контекст оказывается мусором. */
extern void tss_set_rsp0(uint64_t rsp0);
extern void syscall_set_kstack(uint64_t rsp0);  /* см. main.c — тот же баг, что и с RSP0, но для входа через SYSCALL */

static uint64_t context_switch(uint64_t frame_rsp) {
    task_t *prev = g_sched.current;
    uint64_t now_tsc = rdtsc();

    /* Вычисляем delta реального времени */
    uint64_t delta_tsc = now_tsc - g_sched.slice_start_tsc;
    uint64_t delta_ns  = tsc_to_ns(delta_tsc);

    /* Обновляем vruntime: delta * (NICE_0_WEIGHT / weight) */
    if (prev && prev != g_sched.idle) {
        uint64_t weighted = (delta_ns * NICE_0_WEIGHT) / prev->weight;
        prev->vruntime     += weighted;
        prev->total_runtime += delta_ns;
    }

    update_min_vruntime();

    /* Сохраняем rsp текущей задачи — это её собственный стек, не трогаем */
    if (prev) {
        prev->ctx.rsp = frame_rsp;
        if (prev->state == TASK_RUNNING) {
            prev->state = TASK_RUNNABLE;
            if (prev != g_sched.idle)
                rq_push(&g_sched.rq, prev);
        }
        /* SLEEPING / ZOMBIE — не кладём в очередь */
    }

    /* Выбираем следующую задачу */
    task_t *next = rq_pop(&g_sched.rq);
    if (!next) next = g_sched.idle;

    next->state   = TASK_RUNNING;
    next->switches++;
    g_sched.current = next;
    g_sched.slice_start_tsc = now_tsc;

    /* ИСПРАВЛЕНО: переключение CR3 между процессами. Раньше здесь ничего
     * не было (см. старый комментарий "все kernel-потоки в одном
     * адресном пространстве, для user-mode нужно добавить переключение
     * CR3" — вот это "добавить"), и все userspace-процессы фактически
     * делили ОДНУ таблицу страниц (g_vmm.kernel_pml4) — второй
     * запущенный процесс переиспользовал те же виртуальные адреса
     * (стек, код non-PIE ET_EXEC) и стирал память первого. Теперь у
     * каждого процесса своя PML4 (next->pml4_phys, см. ELF64.c/
     * MemoryControl.c) — грузим её в CR3 ПЕРЕД чем-либо ещё при каждом
     * переключении на него (важно сделать это РАНЬШЕ реапа ZOMBIE ниже:
     * когда prev — только что завершившийся процесс, CR3 к моменту
     * реапа должен уже указывать на next/kernel, а не на prev, чтобы
     * можно было безопасно освободить его PML4). pml4_phys == 0 —
     * обычный kernel-поток (idle, task_wrapper и т.п.) без своего
     * адресного пространства: используем "исходную" kernel_pml4 (в ней
     * тоже есть весь код/данные ядра — они скопированы в верхнюю
     * половину КАЖДОЙ пользовательской PML4). g_active_pml4_phys — чтобы
     * не дёргать CR3 (и не флашить TLB) лишний раз, когда следующая
     * задача работает в том же адресном пространстве, что и уже
     * загружено. */
    {
        uint64_t want = next->pml4_phys ? next->pml4_phys : vmm_kernel_pml4_phys();
        if (want != g_active_pml4_phys) {
            __asm__ volatile ("mov %0, %%cr3" :: "r"(want) : "memory");
            g_active_pml4_phys = want;
        }
    }

    /* ИСПРАВЛЕНО (утечка памяти при закрытии приложений): раньше ZOMBIE-
     * задача (после sched_exit()) навсегда оставалась в памяти — её TCB
     * (task_t, ~несколько сотен байт), весь kernel-стек (SCHED_KSTACK_SIZE
     * = 16 KiB) и (после появления per-process PML4 выше) её персональная
     * таблица страниц никогда не освобождались, т.к. sched_exit() лишь
     * снимает задачу со всех списков и зависает в "sti;hlt", а её
     * собственный kfree(себя) невозможен — задача физически ИСПОЛНЯЕТСЯ
     * на этом самом стеке (и под этим самым CR3). При многократном
     * открытии/закрытии калькулятора и SysLogs (см. calc_running_tid/
     * syslogs_running_tid в Framebuffer.c — второй экземпляр не
     * запускается, пока sched_task_alive() не станет false, т.е. только
     * после реального ухода в ZOMBIE) это уходило в никуда — куча
     * фрагментировалась/истощалась, что и объясняло странные повторные
     * kfree() с совпадающим phys_prev в логе паники.
     *
     * Единственный БЕЗОПАСНЫЙ момент освободить ZOMBIE-задачу — сразу
     * ЗДЕСЬ, сразу после того как мы гарантированно переключились на
     * next (включая CR3, см. блок выше) и больше никогда не вернёмся в
     * prev (ZOMBIE никогда не попадает обратно в rq — см. блок выше по
     * коду). К этому моменту prev уже снят со всех списков планировщика
     * (см. sched_exit), а CR3 уже точно не указывает на её PML4 —
     * освобождаем стек, персональную PML4 (если была — т.е. это был
     * настоящий userspace-процесс, а не просто kernel-поток) и сам TCB. */
    if (prev && prev->state == TASK_ZOMBIE) {
        if (prev->kstack_base) {
            kfree(prev->kstack_base);
            prev->kstack_base = NULL;
        }
        if (prev->pml4_phys) {
            pmm_free_frames(prev->pml4_phys, 1);
            prev->pml4_phys = 0;
        }
        kfree(prev);
        prev = NULL;
    }

    /* ВРЕМЕННЫЙ диагностический принт — убрать после отладки. Показывает,
     * доходит ли планировщик вообще до выбора tid только что созданной
     * задачи, и на каком именно тике. */
    DBG_VAL("SC", "context_switch: next tid", (uint64_t)next->tid);

    /* КРИТИЧНО для ring3-задач (см. комментарий у extern-объявления выше и
     * у tss_set_rsp0() в main.c): следующая задача может в любой момент
     * получить прерывание из ring3 (или уже сейчас в ring3, если это
     * возобновление). RSP0 обязан указывать на её СОБСТВЕННЫЙ kernel-стек
     * (kstack_top из setup_kstack), а не оставаться на чужом/общем — иначе
     * следующий же таймерный тик построит кадр прерывания поверх памяти,
     * которую использует кто-то другой, и контекст задачи будет испорчен
     * без единого сообщения об ошибке (именно так "пропадал" калькулятор:
     * лог показывал "spawned tid" и тишину). Для чисто kernel-потоков
     * (никогда не идущих в ring3) эта установка безвредна и просто не
     * используется процессором. */
    tss_set_rsp0(next->kstack_top);
    syscall_set_kstack(next->kstack_top);

    /* Возвращаем RSP следующей задачи — на него переключится сам стек
     * (не данные) в naked-стабе irq0_timer_isr, после чего оттуда же
     * будут выполнены pop регистров и iretq. */
    return next->ctx.rsp;
}

/* -------------------------------------------------------------------------
 * sched_tick — обработчик таймерного прерывания
 *
 * frame_rsp — RSP на момент входа (см. irq0_timer_isr в InterruptControl.c).
 * Возвращает RSP, на который стаб должен переключиться перед pop+iretq:
 * либо тот же frame_rsp (без переключения), либо ctx.rsp следующей задачи.
 * ------------------------------------------------------------------------- */
uint64_t sched_tick(uint64_t frame_rsp) {
    if (!g_sched.ready) return frame_rsp;

    /* Курсор (PNG + альфа) и "тикающие" часы — рисуются вне блокировки
     * планировщика, чтобы не задерживать переключение контекста. */
    extern void ui_tick(void);
    ui_tick();

    /* USB HID (клавиатура/мышь, Boot Protocol) — опрашивается через
     * control-transfer (GET_REPORT), а не через interrupt endpoint (см.
     * шапку Resources/USBHID.c), поэтому это АКТИВНЫЙ опрос по таймеру,
     * а не пассивное ожидание прерывания. Как и с
     * ui_flip_userspace_windows (см. ui_extra.c) — не каждый тик, иначе
     * лишние control-транзакции на пустом месте съедают то же время
     * планировщика, которое мы отвоевали в прошлый раз. Раз в 3 тика
     * (~30мс при 100Гц) — задержка ввода с клавиатуры/мыши не заметна
     * человеку, а нагрузка на HCD в 3 раза меньше, чем при опросе каждый
     * тик. */
    extern void usb_hid_poll_all(void);
    static uint32_t s_usb_hid_tick = 0;
    if ((s_usb_hid_tick++ % 3) == 0) {
        /* ВРЕМЕННАЯ диагностика — убрать после отладки (см. такую же
         * пару в Framebuffer.c: ui_desktop_handle_mouse, "DRAG"). Печатаем
         * только "медленные" опросы (>= 0.5мс), чтобы не утопить лог. */
        uint64_t t0 = rdtsc();
        usb_hid_poll_all();
        uint64_t ns = tsc_to_ns(rdtsc() - t0);
        if (ns >= 500000ULL)
            DBG_VAL("SC", "usb_hid_poll_all SLOW, ns", ns);
    }

    spinlock_acquire(&g_sched.lock);

    task_t *cur = g_sched.current;

    /* Проверяем: исчерпан ли квант? */
    uint64_t delta_tsc = rdtsc() - g_sched.slice_start_tsc;
    uint64_t delta_ns  = tsc_to_ns(delta_tsc);

    bool need_switch = (delta_ns >= SCHED_TIMESLICE_NS);

    /* Также переключаемся если есть задача с меньшим vruntime */
    if (!need_switch && g_sched.rq.size > 0 && cur != g_sched.idle) {
        task_t *best = g_sched.rq.data[0];
        /* Если лучшая задача отстала более чем на один квант — вытесняем */
        uint64_t lag = (cur->vruntime > best->vruntime)
                     ? (cur->vruntime - best->vruntime) : 0;
        if (lag > SCHED_TIMESLICE_NS)
            need_switch = true;
    }

    /* Всегда переключаемся если текущая — idle, а в очереди есть задачи */
    if (cur == g_sched.idle && g_sched.rq.size > 0)
        need_switch = true;

    uint64_t resume_rsp = frame_rsp;
    if (need_switch)
        resume_rsp = context_switch(frame_rsp);

    spinlock_release(&g_sched.lock);
    return resume_rsp;
}

/* -------------------------------------------------------------------------
 * sched_yield — добровольная передача CPU
 * ------------------------------------------------------------------------- */
void sched_yield(void) {
    /* Принудительно обнуляем slice_start чтобы сразу сработал switch */
    spinlock_acquire(&g_sched.lock);
    g_sched.slice_start_tsc = 0;
    spinlock_release(&g_sched.lock);
    /* Прерывание таймера само переключит контекст при следующем тике.
     *
     * КРИТИЧНО: мы попали сюда из syscall_entry_stub (main.c), а вход
     * через инструкцию SYSCALL маскирует IF аппаратно (MSR_SFMASK =
     * (1<<9)) — флаги восстанавливаются только в sysretq, ДО которого
     * мы ещё не дошли. Если выполнить голый "hlt" здесь, IF всё ещё
     * равен 0 — таймерное прерывание физически не может прийти, CPU
     * останавливается НАВСЕГДА (ровно то зависание "намертво", которое
     * видно в логе: последняя строка — syscall nr=0x18=SYS_SCHED_YIELD,
     * дальше тишина, ни одного тика). "sti; hlt" — атомарная пара на
     * x86: между их выполнением прерывание гарантированно не может
     * "проскочить" мимо hlt, так что гонки здесь нет. */
    __asm__ volatile ("sti\n\thlt" ::: "memory");
}

/* -------------------------------------------------------------------------
 * sched_exit — завершение текущей задачи
 * ------------------------------------------------------------------------- */
void sched_exit(void) {
    spinlock_acquire(&g_sched.lock);

    task_t *t = g_sched.current;
    t->state = TASK_ZOMBIE;

    /* Убираем из списка всех задач */
    if (t->prev_all) t->prev_all->next_all = t->next_all;
    if (t->next_all) t->next_all->prev_all = t->prev_all;
    if (g_sched.all_head == t) g_sched.all_head = t->next_all;
    g_sched.task_count--;

    DBG_VAL("SC", "task exit tid", t->tid);

    /* Принудительно переключаемся на следующую задачу */
    g_sched.slice_start_tsc = 0;
    spinlock_release(&g_sched.lock);

    /* Ждём пока таймер нас переключит — мы уже ZOMBIE, в очередь не вернёмся.
     * Тот же баг с маскированием IF на входе в syscall, что и в
     * sched_yield() выше — sti обязателен перед hlt, иначе зависаем
     * навсегда вместо ожидания следующего тика. */
    while (1) __asm__ volatile ("sti\n\thlt" ::: "memory");
}

/* -------------------------------------------------------------------------
 * sched_sleep_ns
 * ------------------------------------------------------------------------- */
void sched_sleep_ns(uint64_t ns) {
    spinlock_acquire(&g_sched.lock);
    task_t *t = g_sched.current;
    t->state = TASK_SLEEPING;
    /* TODO: добавить в список таймеров для автопробуждения */
    /* Пока просто добровольно уходим — простейшая реализация */
    (void)ns;
    g_sched.slice_start_tsc = 0;
    spinlock_release(&g_sched.lock);
    /* См. комментарий в sched_yield() — вход через syscall маскирует
     * IF, без sti тут тоже будет вечный hlt. */
    while (t->state == TASK_SLEEPING)
        __asm__ volatile ("sti\n\thlt" ::: "memory");
}

/* -------------------------------------------------------------------------
 * sched_wake
 * ------------------------------------------------------------------------- */
void sched_wake(uint32_t tid) {
    spinlock_acquire(&g_sched.lock);
    task_t *t = g_sched.all_head;
    while (t) {
        if (t->tid == tid && t->state == TASK_SLEEPING) {
            t->state = TASK_RUNNABLE;
            /* Получает min_vruntime чтобы не уйти в долг за время сна */
            update_min_vruntime();
            if (t->vruntime < g_sched.min_vruntime)
                t->vruntime = g_sched.min_vruntime;
            rq_push(&g_sched.rq, t);
            break;
        }
        t = t->next_all;
    }
    spinlock_release(&g_sched.lock);
}

/* -------------------------------------------------------------------------
 * sched_current / sched_find / sched_task_count
 * ------------------------------------------------------------------------- */
task_t *sched_current(void) { return g_sched.current; }

/* sched_current_pml4_phys — для MemoryControl.c::vmm_map_user(), чтобы
 * SYS_MMAP/SYS_SHM_AT/sys_get_framebuffer (Syscall.c) маппили память в
 * адресное пространство ИМЕННО вызвавшего процесса — теперь, когда у
 * каждого процесса своя PML4 (см. MemoryControl.c), а не общая на всё
 * ядро. 0, если текущей задачи нет или это kernel-поток без своего
 * pml4_phys (тогда MemoryControl.c сам подставит kernel_pml4). */
uint64_t sched_current_pml4_phys(void) {
    return g_sched.current ? g_sched.current->pml4_phys : 0;
}

task_t *sched_find(uint32_t tid) {
    task_t *t = g_sched.all_head;
    while (t) {
        if (t->tid == tid) return t;
        t = t->next_all;
    }
    return NULL;
}

/* sched_task_alive — true, если tid существует и ещё не ZOMBIE.
 * Отдельная bool-функция (а не sched_find + разбор task_t снаружи),
 * чтобы вызывать её из файлов, которым не нужен (и не должен быть
 * виден) полный layout task_t — например из Framebuffer.c, чтобы не
 * запускать calc.elf повторно, пока предыдущий экземпляр ещё жив
 * (несколько одновременных ring3-процессов — источник гонок при
 * спавне/переключении контекста, которые лечить куда сложнее, чем
 * просто не создавать эту ситуацию). */
bool sched_task_alive(uint32_t tid) {
    task_t *t = sched_find(tid);
    return t && t->state != TASK_ZOMBIE;
}

uint32_t sched_task_count(void) { return g_sched.task_count; }