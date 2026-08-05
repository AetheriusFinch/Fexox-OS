/*
 * calc.c — простейший калькулятор для FEXOS, НАСТОЯЩЕЕ userspace
 * ELF64-приложение (не встроенное в ядро, в отличие от "My Computer").
 *
 * Собирается ОТДЕЛЬНО от ядра (см. Compile.sh, секция "Калькулятор"),
 * компонуется как статический ET_EXEC без libc/crt0 — только то, что
 * есть в этом файле. Общается с ядром исключительно через syscall'ы
 * (см. Syscall.h): создаёт своё окно, получает framebuffer и область
 * содержимого окна, опрашивает мышь и сам рисует все пиксели —
 * ядро ничего не знает про арифметику/кнопки калькулятора.
 *
 * Возможности: цифры 0-9, +, -, =, C (сброс), результат сверху.
 * Никакой библиотеки — свой syscall-wrapper, свой мини-шрифт (только
 * цифры/несколько символов, блочный 5x7), своя целочисленная
 * арифметика (int64_t, без чисел с плавающей точкой).
 */

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Номера syscall'ов и структуры — продублировано из Syscall.h, т.к. у
 * userspace-приложений нет доступа к заголовкам ядра (freestanding,
 * отдельная цель компиляции — см. Compile.sh). Держать в синхроне
 * вручную с Syscall.h при любых изменениях ABI.
 * ========================================================================= */
#define SYS_EXIT          60
#define SYS_SCHED_YIELD   24
#define SYS_GET_FRAMEBUFFER 500
#define SYS_WIN_CREATE    600
#define SYS_WIN_CLOSE     601
#define SYS_WIN_GET_RECT  603
#define SYS_INPUT_POLL    604

typedef struct {
    uint64_t phys_addr;
    uint64_t virt_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint8_t  mode;
    uint8_t  r_shift, g_shift, b_shift;
} sc_framebuffer_t;

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
} sc_win_rect_t;

typedef struct {
    int32_t x, y;
    uint8_t buttons;
} sc_input_t;

/* =========================================================================
 * syscall wrapper — соглашение из Syscall.h: rax=nr, rdi,rsi,rdx,r10,r8,r9
 * = аргументы, возврат в rax (<0 — errno отрицательный).
 * ========================================================================= */
static inline int64_t sys_call(int64_t nr, int64_t a1, int64_t a2,
                               int64_t a3, int64_t a4, int64_t a5) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static void sys_exit(int code) {
    sys_call(SYS_EXIT, code, 0, 0, 0, 0);
    for (;;) { }
}
static void sys_yield(void) { sys_call(SYS_SCHED_YIELD, 0, 0, 0, 0, 0); }

static int64_t sys_win_create(const char *title, uint32_t len, uint32_t w, uint32_t h) {
    return sys_call(SYS_WIN_CREATE, (int64_t)(uintptr_t)title, (int64_t)len, (int64_t)w, (int64_t)h, 0);
}
static int64_t sys_win_get_rect(int win_id, sc_win_rect_t *out) {
    return sys_call(SYS_WIN_GET_RECT, win_id, (int64_t)(uintptr_t)out, 0, 0, 0);
}
static int64_t sys_get_framebuffer(sc_framebuffer_t *out) {
    return sys_call(SYS_GET_FRAMEBUFFER, (int64_t)(uintptr_t)out, 0, 0, 0, 0);
}
static int64_t sys_input_poll(sc_input_t *out) {
    return sys_call(SYS_INPUT_POLL, (int64_t)(uintptr_t)out, 0, 0, 0, 0);
}

/* =========================================================================
 * Мини-libc: без настоящей libc в freestanding-окружении.
 * ========================================================================= */
static uint64_t u64_strlen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }

/* =========================================================================
 * Framebuffer: прямая запись пикселей в замапленный задний буфер ядра
 * (см. sys_get_framebuffer — virt_addr уже настоящий userspace-указатель,
 * реально замапленный через vmm_map_user на стороне ядра).
 * ========================================================================= */
static sc_framebuffer_t g_fb;
static sc_win_rect_t    g_rect;   /* область содержимого НАШЕГО окна */

static inline void put_px(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) {
    /* x/y приходят в ЛОКАЛЬНЫХ координатах содержимого окна (0..WIN_W,
     * 0..WIN_H) — так же, как их использует hit-test кликов (lx/ly в
     * calc_main). Чтобы реально попасть в область окна на экране, нужно
     * прибавить его текущее положение (g_rect.x/y), иначе всё рисуется
     * в углу общего framebuffer'а, а не внутри окна. */
    int32_t ax = x + g_rect.x;
    int32_t ay = y + g_rect.y;
    if (ax < 0 || ay < 0 || (uint32_t)ax >= g_fb.width || (uint32_t)ay >= g_fb.height) return;
    uint32_t native = ((uint32_t)r << g_fb.r_shift) |
                      ((uint32_t)g << g_fb.g_shift) |
                      ((uint32_t)b << g_fb.b_shift);
    uint8_t *dst = (uint8_t *)(uintptr_t)g_fb.virt_addr +
                   (uint64_t)ay * g_fb.pitch + (uint64_t)ax * (g_fb.bpp / 8u);
    /* bpp у GOP всегда 32 в этом ядре (см. Framebuffer.c) — пишем как
     * 4 байта, порядок диктуют сдвиги выше, а не порядок записи байт. */
    *(volatile uint32_t *)dst = native;
}

static void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                      uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            put_px(x + (int32_t)col, y + (int32_t)row, r, g, b);
}

static void draw_rect_outline(int32_t x, int32_t y, uint32_t w, uint32_t h,
                              uint8_t r, uint8_t g, uint8_t b) {
    fill_rect(x, y, w, 2, r, g, b);
    fill_rect(x, y + (int32_t)h - 2, w, 2, r, g, b);
    fill_rect(x, y, 2, h, r, g, b);
    fill_rect(x + (int32_t)w - 2, y, 2, h, r, g, b);
}

/* =========================================================================
 * Мини-шрифт: блочный 5 (ширина) x 7 (высота), 1 бит = 1 пиксель,
 * только то, что нужно калькулятору: 0-9, +, -, =, C, пробел, минус
 * отдельно от "C" (используем как индикатор ошибки).
 * Каждая строка глифа — 5 младших бит байта (бит4=левый пиксель).
 * ========================================================================= */
typedef struct { char c; uint8_t rows[7]; } glyph_t;

static const glyph_t FONT[] = {
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
    {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
    {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'E', {0x1F,0x10,0x1E,0x10,0x10,0x10,0x1F}},  /* "Error" индикатор */
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {' ', {0,0,0,0,0,0,0}},
};
#define FONT_N ((int)(sizeof(FONT) / sizeof(FONT[0])))

static const glyph_t *find_glyph(char c) {
    for (int i = 0; i < FONT_N; i++)
        if (FONT[i].c == c) return &FONT[i];
    return &FONT[FONT_N - 1]; /* пробел по умолчанию */
}

static void draw_char(int32_t x, int32_t y, char c, uint8_t scale,
                      uint8_t r, uint8_t g, uint8_t b) {
    const glyph_t *gl = find_glyph(c);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = gl->rows[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, r, g, b);
            }
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char *s, uint8_t scale,
                      uint8_t r, uint8_t g, uint8_t b) {
    int32_t cx = x;
    while (*s) {
        draw_char(cx, y, *s, scale, r, g, b);
        cx += (int32_t)(6 * scale);
        s++;
    }
}

static uint32_t text_width(const char *s, uint8_t scale) {
    return (uint32_t)(u64_strlen(s) * 6u * scale);
}

/* =========================================================================
 * Число -> строка (без libc), int64_t, десятичное, со знаком.
 * ========================================================================= */
static int i64_to_str(int64_t v, char *out, int out_sz) {
    char tmp[24];
    int n = 0;
    bool neg = v < 0;
    uint64_t uv = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;

    if (uv == 0) tmp[n++] = '0';
    while (uv > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    if (neg && n < (int)sizeof(tmp)) tmp[n++] = '-';

    int len = 0;
    while (n > 0 && len < out_sz - 1) out[len++] = tmp[--n];
    out[len] = '\0';
    return len;
}

/* =========================================================================
 * Раскладка кнопок: 4 колонки x 4 строки, дисплей результата сверху.
 * ========================================================================= */
#define WIN_W   220u
#define WIN_H   300u
#define DISP_H   50u
#define BTN_GAP   6
#define GRID_COLS 4
#define GRID_ROWS 4

typedef struct { char label; bool active; } btn_def_t;

static const btn_def_t BTN_GRID[GRID_ROWS][GRID_COLS] = {
    {{'7',true}, {'8',true}, {'9',true}, {'+',true}},
    {{'4',true}, {'5',true}, {'6',true}, {'-',true}},
    {{'1',true}, {'2',true}, {'3',true}, {'=',true}},
    {{'C',true}, {'0',true}, {' ',false},{' ',false}},
};

static void btn_rect(int col, int row, int32_t *bx, int32_t *by, uint32_t *bw, uint32_t *bh) {
    uint32_t grid_w = WIN_W - 2u * BTN_GAP;
    uint32_t grid_h = WIN_H - DISP_H - (GRID_ROWS + 1) * BTN_GAP;
    uint32_t cw = (grid_w - (GRID_COLS - 1) * BTN_GAP) / GRID_COLS;
    uint32_t ch = grid_h / GRID_ROWS;

    *bx = BTN_GAP + col * (int32_t)(cw + BTN_GAP);
    *by = (int32_t)DISP_H + BTN_GAP + row * (int32_t)(ch + BTN_GAP);
    *bw = cw;
    *bh = ch;
}

/* =========================================================================
 * Состояние калькулятора — простая лево-направо арифметика без
 * приоритета операций (как в любом "бытовом" калькуляторе).
 * ========================================================================= */
static int64_t g_acc = 0;      /* накопитель */
static int64_t g_cur = 0;      /* текущее набираемое число */
static char    g_pending_op = 0;
static bool    g_typing = false;
static bool    g_error = false;

#define CUR_MAX  99999999LL   /* ограничение набора, чтобы не переполнить дисплей */

static int64_t apply_op(int64_t a, char op, int64_t b) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        default:  return b;
    }
}

static void calc_reset(void) {
    g_acc = 0; g_cur = 0; g_pending_op = 0; g_typing = false; g_error = false;
}

static void calc_press(char label) {
    if (g_error && label != 'C') return;   /* после ошибки — только сброс */

    if (label >= '0' && label <= '9') {
        if (!g_typing) { g_cur = 0; g_typing = true; }
        if (g_cur <= (CUR_MAX - (label - '0')) / 10)
            g_cur = g_cur * 10 + (label - '0');
        return;
    }

    switch (label) {
        case 'C':
            calc_reset();
            return;
        case '+':
        case '-':
            g_acc = g_pending_op ? apply_op(g_acc, g_pending_op, g_cur)
                                  : (g_typing ? g_cur : g_acc);
            g_pending_op = label;
            g_typing = false;
            return;
        case '=':
            g_acc = g_pending_op ? apply_op(g_acc, g_pending_op, g_cur)
                                  : (g_typing ? g_cur : g_acc);
            g_pending_op = 0;
            g_typing = false;
            return;
        default:
            return;
    }
}

static int64_t calc_display_value(void) {
    return g_typing ? g_cur : g_acc;
}

/* =========================================================================
 * Отрисовка
 * ========================================================================= */
static void draw_display(void) {
    fill_rect(0, 0, WIN_W, DISP_H, 20, 20, 24);
    draw_rect_outline(0, 0, WIN_W, DISP_H, 60, 60, 68);

    char buf[24];
    if (g_error) {
        buf[0] = 'E'; buf[1] = '\0';
    } else {
        i64_to_str(calc_display_value(), buf, sizeof(buf));
    }

    uint8_t scale = 4;
    uint32_t tw = text_width(buf, scale);
    int32_t tx = (int32_t)WIN_W - (int32_t)tw - 14;
    if (tx < 6) tx = 6;
    int32_t ty = (int32_t)(DISP_H - 7u * scale) / 2;
    draw_text(tx, ty, buf, scale, 235, 235, 235);
}

static void draw_button(int col, int row, bool pressed) {
    const btn_def_t *b = &BTN_GRID[row][col];
    if (!b->active) return;

    int32_t bx, by; uint32_t bw, bh;
    btn_rect(col, row, &bx, &by, &bw, &bh);

    bool is_op = (b->label == '+' || b->label == '-' || b->label == '=');
    bool is_c  = (b->label == 'C');

    uint8_t br, bg, bb;
    if (pressed)      { br = 90;  bg = 140; bb = 220; }
    else if (is_op)    { br = 60;  bg = 90;  bb = 160; }
    else if (is_c)     { br = 160; bg = 60;  bb = 60;  }
    else                { br = 55;  bg = 55;  bb = 60;  }

    fill_rect(bx, by, bw, bh, br, bg, bb);
    draw_rect_outline(bx, by, bw, bh, 20, 20, 24);

    uint8_t scale = 3;
    char s[2] = { b->label, '\0' };
    uint32_t tw = text_width(s, scale);
    int32_t tx = bx + (int32_t)((bw - tw) / 2u);
    int32_t ty = by + (int32_t)((bh - 7u * scale) / 2u);
    draw_text(tx, ty, s, scale, 240, 240, 240);
}

static void draw_all(int pressed_col, int pressed_row) {
    draw_display();
    for (int row = 0; row < GRID_ROWS; row++)
        for (int col = 0; col < GRID_COLS; col++)
            draw_button(col, row, (col == pressed_col && row == pressed_row));
}

/* =========================================================================
 * main
 * ========================================================================= */
/* НЕ static и с __attribute__((used)): единственный вызов — из "сырого"
 * asm-блока ниже (__asm__(...) вне функции), который компилятор не
 * умеет разбирать как ссылку на символ. При -O0 static-функция без
 * видимых С-вызовов всё равно может быть не сгенерирована в объектный
 * файл вовсе (см. предупреждение "unused function") — тогда линкер не
 * находит calc_main и падает с "undefined symbol". Оба атрибута вместе
 * гарантируют, что символ реально попадёт в build/calc.o. */
__attribute__((used))
void calc_main(void) {
    const char title[] = "Calculator";
    /* Ядро возвращает через sys_win_get_rect() область СОДЕРЖИМОГО окна —
     * т.е. запрошенную высоту МИНУС заголовок (UI_WIN_TITLEBAR_H=28,
     * см. Framebuffer.c). Весь layout ниже (btn_rect/draw_display и т.д.)
     * считает в предположении, что содержимого ровно WIN_H пикселей —
     * поэтому запрашиваем окно на 28px выше, иначе нижние ряды кнопок
     * рисуются за физической нижней границей окна. */
    #define WIN_TITLEBAR_H 28u
    int64_t win_id = sys_win_create(title, (uint32_t)u64_strlen(title), WIN_W, WIN_H + WIN_TITLEBAR_H);
    if (win_id < 0) sys_exit(1);

    if (sys_win_get_rect((int)win_id, &g_rect) < 0) sys_exit(1);
    if (sys_get_framebuffer(&g_fb) < 0) sys_exit(1);

    calc_reset();
    draw_all(-1, -1);

    sc_input_t in;
    bool prev_lmb = false;
    int  active_col = -1;

    /* Позиция окна на момент последней отрисовки — нужна, чтобы заметить
     * перетаскивание. Ядро (Framebuffer.c: ui_win_move_ex) при КАЖДОМ
     * тике drag'а само вызывает ui_win_draw() и закрашивает всё тело
     * окна фоном — оно ничего не знает про то, что мы туда нарисовали,
     * и никакого "expose"-события userspace не получает. Единственный
     * способ не остаться с пустым окном после любого перетаскивания —
     * самим следить за g_rect (который и так перечитываем каждую
     * итерацию) и перерисовываться заново, как только он изменился. */
    int32_t last_rx = g_rect.x, last_ry = g_rect.y;

    for (;;) {
        /* Окно могли подвинуть перетаскиванием за заголовок — координаты
         * содержимого нужно перечитывать, иначе после drag'а рисование
         * "уедет" от реального положения окна на экране. Дёшево (просто
         * чтение полей структуры окна ядром), можно каждую итерацию. */
        if (sys_win_get_rect((int)win_id, &g_rect) < 0) {
            /* Окно закрыли (крестиком) — win_id стал невалиден. Выходим. */
            sys_exit(0);
        }

        if (g_rect.x != last_rx || g_rect.y != last_ry) {
            /* Окно физически передвинули — ядро уже стёрло его тело в
             * задний буфер (см. ui_win_move_ex/ui_win_draw), рисуем
             * себя заново поверх новой пустой области. */
            last_rx = g_rect.x;
            last_ry = g_rect.y;
            draw_all(-1, -1);
        }

        if (sys_input_poll(&in) < 0) { sys_yield(); continue; }

        int32_t lx = in.x - g_rect.x;
        int32_t ly = in.y - g_rect.y;
        bool inside = lx >= 0 && ly >= 0 && (uint32_t)lx < g_rect.w && (uint32_t)ly < g_rect.h;
        bool lmb = (in.buttons & 0x01) != 0;
        bool click_edge = lmb && !prev_lmb;   /* переход "не нажато -> нажато" */

        if (inside && click_edge) {
            for (int row = 0; row < GRID_ROWS && active_col < 0; row++) {
                for (int col = 0; col < GRID_COLS; col++) {
                    if (!BTN_GRID[row][col].active) continue;
                    int32_t bx, by; uint32_t bw, bh;
                    btn_rect(col, row, &bx, &by, &bw, &bh);
                    if (lx >= bx && ly >= by && lx < bx + (int32_t)bw && ly < by + (int32_t)bh) {
                        active_col = col;
                        calc_press(BTN_GRID[row][col].label);
                        draw_all(col, row);
                        break;
                    }
                }
            }
        } else if (!lmb && active_col >= 0) {
            /* Отпустили кнопку мыши — снимаем подсветку нажатой кнопки. */
            draw_all(-1, -1);
            active_col = -1;
        }

        prev_lmb = lmb;
        sys_yield();
    }
}

/* =========================================================================
 * Точка входа. ELF64.c грузит образ и прыгает на e_entry без argv/envp
 * (см. ELF64.h: "текущая реализация не передаёт argv/envp"). Никакого
 * crt0 нет — стек уже готов (elf64_load выставляет stack_top), выравнивание
 * по 16 байт для System V ABI гарантирует загрузчик.
 * ========================================================================= */
__asm__(
    ".global _start\n"
    "_start:\n"
    "    call calc_main\n"
    "    mov $60, %rax\n"   /* SYS_EXIT на случай если calc_main вернётся */
    "    xor %rdi, %rdi\n"
    "    syscall\n"
    "1:  jmp 1b\n"
);
