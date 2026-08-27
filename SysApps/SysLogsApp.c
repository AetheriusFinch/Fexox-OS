/*
 * SysLogsApp.c — приложение "System Logs" для FEXOS
 *
 * Простое userspace-приложение (тот же паттерн, что calc.c): открывает
 * своё окно через SYS_WIN_CREATE, получает framebuffer через
 * SYS_GET_FRAMEBUFFER, и рисует пиксели прямо в него сам — без всякой
 * связи с ядром, кроме системных вызовов. Никакого отдельного
 * "ring buffer" системных логов в ядре пока нет, поэтому окно
 * показывает:
 *   - несколько статичных строк о стадиях загрузки (для вида/контекста);
 *   - реальные живые данные, которые можно получить через syscall'ы
 *     (pid процесса, режим/разрешение framebuffer'а, "тик" — чтобы
 *     было видно, что окно живое);
 *   - содержимое файла /system.log, если он есть на диске (по строке
 *     на файловую строку, до первого перевода строки) — так что если
 *     когда-нибудь ядро/другие программы начнут дописывать туда
 *     реальные логи, это окно их сразу покажет без переделок.
 *
 * Свой собственный маленький шрифт 5x7 (см. ниже) — никаких внешних
 * зависимостей, никакого libc (freestanding, статическая линковка,
 * см. Compile.sh: CFLAGS_USER/LDFLAGS_USER).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../Syscall.h"

/* =========================================================================
 * "Сырые" системные вызовы — напрямую через инструкцию SYSCALL.
 * Соглашение (см. Syscall.h): rax=nr, rdi=a1, rsi=a2, rdx=a3, r10=a4,
 * r8=a5 — как обычный Linux x86_64 syscall ABI (r10 вместо rcx, т.к.
 * rcx затирается самой инструкцией syscall).
 * ========================================================================= */
static inline long raw_syscall(long nr, long a1, long a2, long a3,
                                long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline void sys_exit(int code) {
    raw_syscall(SYS_EXIT, code, 0, 0, 0, 0);
    for (;;) { /* не должны сюда попасть */ }
}

static inline long sys_sched_yield(void) {
    return raw_syscall(SYS_SCHED_YIELD, 0, 0, 0, 0, 0);
}

static inline long sys_open(const char *path, long flags) {
    return raw_syscall(SYS_OPEN, (long)(uintptr_t)path, flags, 0, 0, 0);
}

static inline long sys_read(long fd, void *buf, long count) {
    return raw_syscall(SYS_READ, fd, (long)(uintptr_t)buf, count, 0, 0);
}

static inline long sys_close(long fd) {
    return raw_syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
}

static inline long sys_getpid(void) {
    return raw_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

static inline long sys_get_framebuffer(sc_framebuffer_t *fb) {
    return raw_syscall(SYS_GET_FRAMEBUFFER, (long)(uintptr_t)fb, 0, 0, 0, 0);
}

static inline long sys_win_create(const char *title, unsigned long len,
                                   unsigned long w, unsigned long h) {
    return raw_syscall(SYS_WIN_CREATE, (long)(uintptr_t)title, (long)len,
                        (long)w, (long)h, 0);
}

static inline long sys_win_get_rect(long win_id, sc_win_rect_t *out) {
    return raw_syscall(SYS_WIN_GET_RECT, win_id, (long)(uintptr_t)out, 0, 0, 0);
}

/* =========================================================================
 * Мини-libc: своя, т.к. ядерная klibc.c в userspace-цель не линкуется
 * (см. Compile.sh — калькулятор и SysLogs собираются отдельно, без
 *klibc.o). Только то, что реально нужно ниже.
 * ========================================================================= */
static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char *dst, const char *src, unsigned long maxlen) {
    unsigned long i = 0;
    while (i + 1 < maxlen && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* uint -> decimal string (без libc). Возвращает длину. */
static unsigned long uint_to_str(unsigned long v, char *out) {
    char tmp[24];
    int n = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v > 0) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return (unsigned long)n;
}

/* =========================================================================
 * Мини-шрифт 5x7 (свой, без внешних зависимостей) — покрывает A-Z,
 * 0-9, пробел и базовую пунктуацию. Строчные буквы рисуются как
 * заглавные (для простого лог-вывода этого достаточно). Символы, для
 * которых нет глифа, рисуются как пробел.
 * Формат: 7 байт на символ, в каждом байте биты 4..0 = пиксели строки
 * слева направо (бит4 = самый левый пиксель).
 * ========================================================================= */
typedef struct { char c; uint8_t rows[7]; } glyph_t;

static const glyph_t FONT[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
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
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C', {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
    {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F}},
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x01,0x01,0x01,0x01,0x01,0x11,0x0E}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x15,0x13,0x11,0x11}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
    {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
    {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
    {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
    {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {':', {0x00,0x04,0x00,0x00,0x04,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x04,0x00}},
    {',', {0x00,0x00,0x00,0x00,0x00,0x04,0x08}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
    {'/', {0x01,0x02,0x02,0x04,0x08,0x08,0x10}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
    {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[', {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
    {']', {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'%', {0x19,0x1A,0x04,0x04,0x0B,0x13,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'?', {0x0E,0x11,0x02,0x04,0x04,0x00,0x04}},
    {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}},
    {'>', {0x10,0x08,0x04,0x02,0x04,0x08,0x10}},
    {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
};
#define FONT_N (sizeof(FONT) / sizeof(FONT[0]))

static const glyph_t *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (unsigned long i = 0; i < FONT_N; i++)
        if (FONT[i].c == c) return &FONT[i];
    return &FONT[0]; /* неизвестный символ -> пробел */
}

/* =========================================================================
 * Рисование пикселей прямо в mapped framebuffer (см. sc_get_framebuffer:
 * fb.virt_addr — уже готовый юзерский указатель, fb.pitch — байт на
 * строку, r/g/b_shift — как собрать нативный 32-битный пиксель).
 * ========================================================================= */
static sc_framebuffer_t g_fb;
static uint8_t         *g_fb_base;
static uint32_t         g_bpp_bytes;

/* Область отсечения — устанавливается перед каждой перерисовкой в
 * границы СОДЕРЖИМОГО нашего окна (см. sys_win_get_rect), чтобы
 * случайно длинная строка лога не нарисовала пиксели по чужим окнам/
 * рабочему столу за пределами нашего окна. */
static int32_t  g_clip_x0, g_clip_y0, g_clip_x1, g_clip_y1;

static void set_clip(const sc_win_rect_t *r) {
    g_clip_x0 = r->x;
    g_clip_y0 = r->y;
    g_clip_x1 = r->x + (int32_t)r->w;
    g_clip_y1 = r->y + (int32_t)r->h;
}

static inline uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << g_fb.r_shift) |
           ((uint32_t)g << g_fb.g_shift) |
           ((uint32_t)b << g_fb.b_shift);
}

static void put_pixel(int32_t x, int32_t y, uint32_t native) {
    if (x < 0 || y < 0 || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return;
    if (x < g_clip_x0 || x >= g_clip_x1 || y < g_clip_y0 || y >= g_clip_y1)
        return;
    uint8_t *p = g_fb_base + (size_t)y * g_fb.pitch + (size_t)x * g_bpp_bytes;
    *(volatile uint32_t *)p = native;
}

static void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t native) {
    for (uint32_t row = 0; row < h; row++)
        for (uint32_t col = 0; col < w; col++)
            put_pixel(x + (int32_t)col, y + (int32_t)row, native);
}

#define GLYPH_SCALE   2u
#define GLYPH_W       (5u * GLYPH_SCALE)
#define GLYPH_H       (7u * GLYPH_SCALE)
#define GLYPH_ADVANCE (GLYPH_W + 2u)
#define LINE_HEIGHT   (GLYPH_H + 4u)

static void draw_char(int32_t x, int32_t y, char c, uint32_t fg) {
    const glyph_t *g = glyph_for(c);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g->rows[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1u << (4 - col))) {
                fill_rect(x + (int32_t)(col * GLYPH_SCALE),
                          y + (int32_t)(row * GLYPH_SCALE),
                          GLYPH_SCALE, GLYPH_SCALE, fg);
            }
        }
    }
}

static void draw_string(int32_t x, int32_t y, const char *s, uint32_t fg) {
    int32_t cx = x;
    while (*s) {
        draw_char(cx, y, *s, fg);
        cx += (int32_t)GLYPH_ADVANCE;
        s++;
    }
}

/* =========================================================================
 * Логика приложения
 * ========================================================================= */
#define WIN_W   480u
#define WIN_H   340u
#define PAD     10

#define COL_BG      0x00141414U   /* тёмный фон окна */
#define COL_HEADER  0x00202020U
#define COL_TEXT    0x00D0D0D0U   /* обычный лог */
#define COL_OK      0x0060D060U   /* зелёный — "успешные" строки */
#define COL_WARN    0x00D0C050U   /* жёлтый  — предупреждения */
#define COL_DIM     0x00707070U   /* тусклый — заголовок/разделитель */

#define MAX_LINES   16
#define LINE_LEN    56

static char g_lines[MAX_LINES][LINE_LEN];
static uint32_t g_line_color[MAX_LINES];
static int g_line_count = 0;

static void add_line(const char *text, uint32_t color) {
    if (g_line_count >= MAX_LINES) return;
    str_copy(g_lines[g_line_count], text, LINE_LEN);
    g_line_color[g_line_count] = color;
    g_line_count++;
}

/* Читает /system.log (если есть) и добавляет его содержимое построчно,
 * с префиксом "[LOG] ". Если файла нет — просто ничего не делает,
 * никакой ошибки пользователю не показываем (это опциональный источник). */
static void load_system_log(void) {
    long fd = sys_open("/system.log", 0 /* O_RDONLY */);
    if (fd < 0) return;

    char buf[1024];
    long n = sys_read(fd, buf, (long)sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char line[LINE_LEN];
    unsigned long li = 0;
    for (long i = 0; i < n && g_line_count < MAX_LINES; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            if (li > 0) {
                line[li] = '\0';
                char full[LINE_LEN];
                str_copy(full, "[LOG] ", LINE_LEN);
                unsigned long base = str_len(full);
                unsigned long j = 0;
                while (base + j < LINE_LEN - 1 && line[j]) { full[base + j] = line[j]; j++; }
                full[base + j] = '\0';
                add_line(full, COL_TEXT);
                li = 0;
            }
        } else if (li < sizeof(line) - 1) {
            line[li++] = c;
        }
    }
}

static void build_lines(int pid) {
    g_line_count = 0;
    add_line("[BOOT] FEXOS KERNEL START", COL_OK);
    add_line("[MM]   MEMORY MANAGER OK", COL_OK);
    add_line("[VFS]  FAT32 MOUNTED", COL_OK);
    add_line("[SCHED] SCHEDULER READY", COL_OK);
    add_line("[UI]   WINDOW MANAGER READY", COL_OK);
    add_line("[NET]  NOT AVAILABLE", COL_WARN);

    char line[LINE_LEN];
    char num[24];
    str_copy(line, "[APP]  SYSLOGS STARTED, PID=", LINE_LEN);
    uint_to_str((unsigned long)pid, num);
    unsigned long base = str_len(line);
    unsigned long j = 0;
    while (base + j < LINE_LEN - 1 && num[j]) { line[base + j] = num[j]; j++; }
    line[base + j] = '\0';
    add_line(line, COL_OK);

    str_copy(line, "[FB]   ", LINE_LEN);
    base = str_len(line);
    if (g_fb.mode == 1) {
        const char *mode = "LINEAR ";
        j = 0;
        while (base + j < LINE_LEN - 1 && mode[j]) { line[base + j] = mode[j]; j++; }
        base += j;
        uint_to_str(g_fb.width, num);
        j = 0;
        while (base + j < LINE_LEN - 1 && num[j]) { line[base + j] = num[j]; j++; }
        base += j;
        if (base < LINE_LEN - 1) line[base++] = 'X';
        uint_to_str(g_fb.height, num);
        j = 0;
        while (base + j < LINE_LEN - 1 && num[j]) { line[base + j] = num[j]; j++; }
        base += j;
        line[base] = '\0';
    } else {
        str_copy(line + base, "VGA TEXT", LINE_LEN - base);
    }
    add_line(line, COL_TEXT);

    load_system_log();
}

/* Полная перерисовка тела окна: тёмный фон + шапка + все строки. */
static void full_redraw(const sc_win_rect_t *r) {
    set_clip(r);
    fill_rect(r->x, r->y, r->w, r->h, COL_BG);
    fill_rect(r->x, r->y, r->w, LINE_HEIGHT + 4, COL_HEADER);
    draw_string(r->x + PAD, r->y + 6, "SYSTEM LOGS", COL_TEXT);

    int32_t y = r->y + (int32_t)LINE_HEIGHT + 10;
    for (int i = 0; i < g_line_count; i++) {
        if (y + (int32_t)LINE_HEIGHT > r->y + (int32_t)r->h - PAD) break;
        draw_string(r->x + PAD, y, g_lines[i], g_line_color[i]);
        y += (int32_t)LINE_HEIGHT;
    }
}

/* Перерисовать только последнюю строку (heartbeat) — дёшево, без
 * перерисовки всего окна на каждом тике. */
static void redraw_heartbeat(const sc_win_rect_t *r, const char *text) {
    set_clip(r);
    int32_t y = r->y + (int32_t)LINE_HEIGHT + 10 +
                (int32_t)((g_line_count > 0 ? g_line_count - 1 : 0) * LINE_HEIGHT);
    if (y + (int32_t)LINE_HEIGHT > r->y + (int32_t)r->h - PAD) return;
    fill_rect(r->x + PAD, y, r->w - 2u * PAD, LINE_HEIGHT, COL_BG);
    draw_string(r->x + PAD, y, text, COL_DIM);
}

void _start(void) {
    if (sys_get_framebuffer(&g_fb) != 0 || g_fb.mode != 1) {
        /* Нет линейного framebuffer'а — GUI-окно рисовать некуда. */
        sys_exit(1);
    }
    g_fb_base   = (uint8_t *)(uintptr_t)g_fb.virt_addr;
    g_bpp_bytes = g_fb.bpp / 8;
    if (g_bpp_bytes == 0) g_bpp_bytes = 4;

    /* ВАЖНО (тот же приём, что и в calc.c): sys_win_get_rect() отдаёт
     * область СОДЕРЖИМОГО окна — запрошенную высоту МИНУС заголовок
     * (UI_WIN_TITLEBAR_H = 28, см. Framebuffer.c: ui_win_get_rect). Весь
     * layout ниже (full_redraw/LINE_HEIGHT и т.п.) считает в предположении,
     * что содержимого ровно WIN_H пикселей — поэтому запрашиваем окно на
     * 28px выше, иначе последние строки логов обрезаются снизу. */
    #define WIN_TITLEBAR_H 28u
    const char title[] = "System Logs";
    long win_id = sys_win_create(title, str_len(title), WIN_W, WIN_H + WIN_TITLEBAR_H);
    if (win_id < 0) sys_exit(1);

    int pid = (int)sys_getpid();
    build_lines(pid);

    /* Держим последнюю строку как "живой" heartbeat, чтобы было видно,
     * что окно не зависло. */
    if (g_line_count < MAX_LINES) {
        add_line("[TICK] N=0", COL_DIM);
    }

    sc_win_rect_t rect;
    if (sys_win_get_rect(win_id, &rect) != 0) sys_exit(0);
    full_redraw(&rect);

    unsigned long tick = 0;
    for (;;) {
        /* Окно закрыли крестиком (kернel сам обрабатывает клик по
         * крестику и закрывает окно) — sys_win_get_rect вернёт ошибку,
         * значит наша работа сделана. */
        if (sys_win_get_rect(win_id, &rect) != 0) {
            sys_exit(0);
        }

        /* Простая кооперативная пауза — нет отдельного sleep-syscall'а,
         * поэтому просто отдаём процессор много раз подряд. */
        for (int i = 0; i < 400000; i++) sys_sched_yield();

        tick++;
        char text[LINE_LEN];
        char num[24];
        str_copy(text, "[TICK] N=", LINE_LEN);
        uint_to_str(tick, num);
        unsigned long base = str_len(text);
        unsigned long j = 0;
        while (base + j < LINE_LEN - 1 && num[j]) { text[base + j] = num[j]; j++; }
        text[base + j] = '\0';
        redraw_heartbeat(&rect, text);
    }
}
