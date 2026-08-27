/*
 * ui_extra.c — курсор из PNG (с альфа-каналом) + "тикающие" часы
 *
 * Зависимости: Framebuffer (fb_get_pixel/fb_put_pixel/fb_flip/ui_redraw_clock),
 * png.c (декодер PNG), VFS (vfs_open/vfs_read/vfs_close), klibc (kmalloc/kfree).
 */
#include <stdint.h>
#include <stddef.h>
#include "Framebuffer.h"
#include "png.h"
#include "ui_extra.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

extern int     vfs_open(const char *path, int flags);
extern int     vfs_close(int fd);
extern int64_t vfs_read(int fd, void *buf, uint64_t size);

/* --- из PS2Mouse.c --- */
extern uint8_t ps2_mouse_buttons(void);
/* USB HID мышь (Boot Protocol, см. Resources/USBHID.c) двигает курсор
 * сама (через ui_set_mouse_pos, вызывается из usb_hid_poll_all — см.
 * хук в Scheduler.c), а кнопки объединяются здесь с PS2 через OR — с
 * точки зрения ui_desktop_handle_mouse/sc_input_poll разницы между
 * PS2- и USB-мышью нет, оба источника пишут в один и тот же битовый
 * формат (bit0=LMB,bit1=RMB,bit2=MMB). */
extern uint8_t usb_hid_mouse_buttons(void);

#ifndef UI_O_RDONLY
#define UI_O_RDONLY 0
#endif

/* =========================================================================
 * Частота таймерного тика (см. interrupt_init(..., timer_freq_hz) —
 * по умолчанию 100 Гц). Используется только для определения "раз в секунду".
 * ========================================================================= */
/* Часы теперь перерисовываются по факту смены секунды в RTC (см.
 * ui_clock_tick() в Framebuffer.c), а не по счётчику тиков — см.
 * комментарий у ui_tick() ниже. */

/* Ограничение размера курсора — стандартные размеры 16x16/24x24/32x32 */
#define CURSOR_MAX_W        32
#define CURSOR_MAX_H        32

/* Максимальный размер файла cursor.png, который мы готовы прочитать.
 * 256 КБ — достаточно для любого разумного курсора (32x32 RGBA PNG). */
#define CURSOR_FILE_MAXSZ   (256 * 1024)

/* =========================================================================
 * Состояние
 * ========================================================================= */
static png_image_t g_cursor;         /* g_cursor.pixels == NULL -> fallback */
static int         g_cursor_loaded = 0;

static int32_t g_mouse_x = 100;
static int32_t g_mouse_y = 100;

/* Сохранённый фон под курсором (для восстановления перед перерисовкой) */
static fb_color_t g_saved_bg[CURSOR_MAX_W * CURSOR_MAX_H];
static int32_t    g_saved_x = -1, g_saved_y = -1;
static uint32_t   g_saved_w = 0,  g_saved_h = 0;
static int        g_saved_valid = 0;

/* =========================================================================
 * Загрузка cursor.png через VFS
 * ========================================================================= */
static int load_cursor_png(void) {
    /* Пробуем оба варианта пути: с ведущим слешем и без */
    DBG_MSG("UI", "cursor: trying UI/cursor.png");
    int fd = vfs_open("UI/cursor.png", UI_O_RDONLY);
    if (fd < 0) {
        DBG_MSG("UI", "cursor: trying /UI/cursor.png");
        fd = vfs_open("/UI/cursor.png", UI_O_RDONLY);
    }
    if (fd < 0) {
        DBG_MSG("UI", "cursor.png not found, using fallback arrow");
        return -1;
    }
    DBG_VAL("UI", "cursor: fd", (uint64_t)fd);

    uint8_t *buf = (uint8_t *)kmalloc(CURSOR_FILE_MAXSZ);
    if (!buf) {
        DBG_MSG("UI", "cursor: kmalloc failed");
        vfs_close(fd);
        return -1;
    }

    size_t total = 0;
    for (;;) {
        int64_t got = vfs_read(fd, buf + total, CURSOR_FILE_MAXSZ - total);
        if (got <= 0) break;
        total += (size_t)got;
        if (total >= CURSOR_FILE_MAXSZ) break;
    }
    vfs_close(fd);

    DBG_VAL("UI", "cursor: bytes read", (uint64_t)total);

    if (total == 0) {
        DBG_MSG("UI", "cursor: empty file");
        kfree(buf);
        return -1;
    }

    /* Быстрая проверка PNG-сигнатуры перед декодированием */
    if (total < 8 || buf[0] != 0x89 || buf[1] != 'P' ||
        buf[2] != 'N' || buf[3] != 'G') {
        DBG_MSG("UI", "cursor: not a PNG (bad magic)");
        kfree(buf);
        return -1;
    }

    int rc = png_decode(buf, total, &g_cursor);
    kfree(buf);

    if (rc != PNG_OK) {
        DBG_VAL("UI", "cursor.png decode failed, rc", (uint64_t)(int64_t)rc);
        return -1;
    }
    if (g_cursor.width > CURSOR_MAX_W || g_cursor.height > CURSOR_MAX_H) {
        DBG_VAL("UI", "cursor.png too large, w", g_cursor.width);
        DBG_VAL("UI", "cursor.png too large, h", g_cursor.height);
        DBG_MSG("UI", "cursor.png max is 32x32, using fallback arrow");
        kfree(g_cursor.pixels);
        g_cursor.pixels = NULL;
        return -1;
    }

    DBG_VAL("UI", "cursor.png loaded OK, w", g_cursor.width);
    DBG_VAL("UI", "cursor.png loaded OK, h", g_cursor.height);
    return 0;
}

int ui_extra_init(void) {
    g_cursor.pixels = NULL;
    if (load_cursor_png() == 0) g_cursor_loaded = 1;
    /* Если PNG не загружен — будет использоваться встроенная стрелка
     * (см. draw_fallback_arrow ниже). ui_extra_init всегда "успешен",
     * чтобы вызывающий код не падал из-за отсутствия файла. */
    return 0;
}

void ui_set_mouse_pos(int32_t x, int32_t y) {
    g_mouse_x = x;
    g_mouse_y = y;
}

/* ui_get_mouse_pos — текущая позиция курсора в экранных координатах.
 * Нужна sc_input_poll() в Syscall.c: userspace-приложение (например
 * калькулятор) само переводит абсолютные координаты в относительные
 * своему окну (см. sys_win_get_rect), и само делает edge-detection
 * клика (сравнивая с предыдущим опросом), поэтому здесь достаточно
 * отдать сырые x/y без какой-либо дополнительной логики. */
void ui_get_mouse_pos(int32_t *out_x, int32_t *out_y) {
    if (out_x) *out_x = g_mouse_x;
    if (out_y) *out_y = g_mouse_y;
}

/* =========================================================================
 * Встроенная стрелка-курсор (если cursor.png отсутствует/не декодируется).
 * 12x19, классическая Win-стрелка: '#' = чёрный контур, '.' = белая заливка,
 * пробел = прозрачный.
 * ========================================================================= */
static const char g_fallback_arrow[19][12] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.....##### ",
    "#...#       ",
    "#..##       ",
    "#.#.#       ",
    "#. #.#      ",
    "#   #.#     ",
    "    #.#     ",
    "     #      ",
    "            ",
};
#define FALLBACK_W 12
#define FALLBACK_H 19

/* =========================================================================
 * Сохранение/восстановление фона под курсором
 * ========================================================================= */
static void cursor_save_bg(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            g_saved_bg[row * w + col] = fb_get_pixel(x + (int32_t)col, y + (int32_t)row);
        }
    }
    g_saved_x = x; g_saved_y = y;
    g_saved_w = w; g_saved_h = h;
    g_saved_valid = 1;
}

static void cursor_restore_bg(void) {
    if (!g_saved_valid) return;
    for (uint32_t row = 0; row < g_saved_h; row++) {
        for (uint32_t col = 0; col < g_saved_w; col++) {
            fb_put_pixel(g_saved_x + (int32_t)col, g_saved_y + (int32_t)row,
                         g_saved_bg[row * g_saved_w + col]);
        }
    }
    g_saved_valid = 0;
}

/* Простое альфа-смешение: result = src*a/255 + dst*(255-a)/255 */
static inline uint8_t blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    return (uint8_t)(((uint32_t)src * a + (uint32_t)dst * (255 - a)) / 255);
}

/* =========================================================================
 * Отрисовка курсора в позиции (x, y) — top-left угол изображения.
 * Сначала сохраняет фон под новой позицией, затем рисует курсор
 * с альфа-блендингом (PNG) или встроенную стрелку (fallback).
 * ========================================================================= */
static void cursor_draw(int32_t x, int32_t y) {
    if (g_cursor_loaded && g_cursor.pixels) {
        uint32_t w = g_cursor.width, h = g_cursor.height;
        cursor_save_bg(x, y, w, h);

        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++) {
                const uint8_t *px = g_cursor.pixels + (((size_t)row * w + col) * 4);
                uint8_t r = px[0], g = px[1], b = px[2], a = px[3];
                if (a == 0) continue; /* полностью прозрачный — оставляем фон */

                int32_t sx = x + (int32_t)col;
                int32_t sy = y + (int32_t)row;

                if (a == 255) {
                    fb_put_pixel(sx, sy, fb_rgb(r, g, b));
                } else {
                    fb_color_t bg = fb_get_pixel(sx, sy);
                    uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
                    uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
                    uint8_t bb = (uint8_t)(bg & 0xFF);
                    fb_color_t mixed = fb_rgb(blend_chan(r, br, a),
                                               blend_chan(g, bgc, a),
                                               blend_chan(b, bb, a));
                    fb_put_pixel(sx, sy, mixed);
                }
            }
        }
    } else {
        /* Fallback: встроенная стрелка из примитивов, без блендинга */
        cursor_save_bg(x, y, FALLBACK_W, FALLBACK_H);
        for (int row = 0; row < FALLBACK_H; row++) {
            for (int col = 0; col < FALLBACK_W; col++) {
                char c = g_fallback_arrow[row][col];
                if (c == '#') fb_put_pixel(x + col, y + row, FB_BLACK);
                else if (c == '.') fb_put_pixel(x + col, y + row, FB_WHITE);
                /* пробел — оставляем фон как есть */
            }
        }
    }
}

/* =========================================================================
 * ui_tick — вызывать из таймерного прерывания (sched_tick).
 *
 *   - каждый тик: восстановить фон под старым курсором, нарисовать курсор
 *     в новой позиции (g_mouse_x/g_mouse_y, обновляется ui_set_mouse_pos)
 *   - раз в секунду (UI_TICK_HZ тиков): перерисовать часы
 * ========================================================================= */
void ui_tick(void) {
    if (fb_get_mode() != FB_MODE_LINEAR) return;

    /* ВАЖНО: сначала стираем курсор с его СТАРОЙ позиции (возвращаем
     * реальный фон рабочего стола), и только ПОТОМ трогаем
     * Menu/иконки/окна fxapp. Раньше было наоборот, и это давало
     * характерный баг: пока курсор ещё не стёрт, его спрайт физически
     * лежит поверх кадра — и когда окно fxapp в этот момент
     * сохраняет/восстанавливает свой фон (при перетаскивании/закрытии),
     * оно захватывает пиксели курсора как "фон". Позже cursor_restore_bg
     * накладывал уже устаревший сохранённый кусок обратно поверх
     * свежеперерисованного окна — и на столе оставались "хвосты"
     * (кусочки синего заголовка/красной кнопки close). Стирая курсор
     * ДО перерисовки окна, мы гарантируем, что сохранение/восстановление
     * фона окна всегда работает с чистым содержимым рабочего стола. */

    /* Область старого курсора (то, что cursor_restore_bg() сейчас вернёт
     * на стол) — запоминаем ДО восстановления, чтобы включить в дирти-
     * прямоугольник ниже. */
    int32_t old_x = g_saved_x, old_y = g_saved_y;
    uint32_t old_w = g_saved_w, old_h = g_saved_h;
    int had_old = g_saved_valid;

    cursor_restore_bg();

    /* Клик/зажатая кнопка — потенциально меняют что угодно на столе
     * (иконки, окна, меню, перетаскивание), поэтому такие тики
     * по-прежнему обходятся полным fb_flip() ниже, а не частичным. */
    uint8_t buttons = ps2_mouse_buttons() | usb_hid_mouse_buttons();
    bool lmb_down = (buttons & 1) != 0;

    /* ВАЖНО: приложения (например calc.c) перерисовывают себя
     * КООПЕРАТИВНО — когда их поток в очередной раз получит время
     * планировщика и заметит, что окно подвинули. Раньше full-flip
     * шёл только пока кнопка мыши физически зажата (big_change =
     * buttons&1). Первый же тик ПОСЛЕ отпускания кнопки уже не делал
     * fb_flip() вообще — а именно в этот момент back buffer чаще
     * всего ещё не успел получить свежую перерисовку от приложения
     * (это гонка планировщика). В итоге на экране "застревал" пустой
     * кадр, и он лишь постепенно "проявлялся" маленькими кусочками —
     * через частичные flip'ы вслед за курсором. Держим полный flip ещё
     * несколько тиков после отпускания, чтобы дать приложению реально
     * успеть перерисоваться и попасть в кадр. */
    static int s_release_grace = 0;
    static bool s_prev_lmb_down = false;
    if (s_prev_lmb_down && !lmb_down) s_release_grace = 3;
    s_prev_lmb_down = lmb_down;

    /* Клик/зажатая кнопка — потенциально меняют что угодно на столе
     * (иконки, окна, меню, перетаскивание), поэтому такие тики
     * по-прежнему обходятся полным fb_flip() ниже, а не частичным. */
    int big_change = lmb_down || (s_release_grace > 0);
    if (s_release_grace > 0) s_release_grace--;

    ui_desktop_handle_mouse(g_mouse_x, g_mouse_y, buttons);

    cursor_draw(g_mouse_x, g_mouse_y);

    /* Новая область курсора — cursor_draw() выше уже обновил
     * g_saved_x/y/w/h через свой собственный cursor_save_bg(). */
    int32_t new_x = g_saved_x, new_y = g_saved_y;
    uint32_t new_w = g_saved_w, new_h = g_saved_h;

    int clock_drawn = ui_clock_tick();

    /* ГЛАВНЫЙ ФИКС (см. подробности в fb_flip_rect() в Framebuffer.c):
     * раньше тут был безусловный fb_flip() — полное копирование кадра
     * КАЖДЫЙ таймерный тик. Под QEMU без аппаратного ускорения это
     * стоило ~1 секунды за тик и не оставляло процессору времени вообще
     * ни на что другое — включая только что созданный поток
     * калькулятора. Теперь в подавляющем большинстве тиков (просто
     * движение мыши, ничего не нажато) переносим в VRAM только реально
     * изменившийся маленький прямоугольник — курсор ± область часов раз
     * в секунду. Полный кадр гоняем ТОЛЬКО когда что-то могло измениться
     * по-крупному (клик/drag). */
    if (big_change) {
        fb_flip();
        /* ui_flip_userspace_windows() НЕ вызываем здесь — fb_flip() выше
         * и так уже скопировал ВЕСЬ экран (включая тела всех userspace-
         * окон) в VRAM. Раньше он всё равно вызывался безусловно в конце
         * функции (см. ниже) — при каждом клике/drag'е это удваивало
         * объём копирования именно в тот момент, когда цена лишнего
         * fb_flip_rect() ощутимее всего (тик и так уже занят дорогим
         * полным fb_flip()). */
    } else {
        /* Объединяем старую и новую область курсора в один прямоугольник */
        int32_t ux = old_x, uy = old_y, uw2 = (int32_t)old_w, uh2 = (int32_t)old_h;
        if (had_old) {
            int32_t ox2 = old_x + (int32_t)old_w, oy2 = old_y + (int32_t)old_h;
            int32_t nx2 = new_x + (int32_t)new_w, ny2 = new_y + (int32_t)new_h;
            int32_t left   = old_x < new_x ? old_x : new_x;
            int32_t top    = old_y < new_y ? old_y : new_y;
            int32_t right  = ox2 > nx2 ? ox2 : nx2;
            int32_t bottom = oy2 > ny2 ? oy2 : ny2;
            ux = left; uy = top; uw2 = right - left; uh2 = bottom - top;
        } else {
            ux = new_x; uy = new_y; uw2 = (int32_t)new_w; uh2 = (int32_t)new_h;
        }
        if (uw2 > 0 && uh2 > 0)
            fb_flip_rect(ux, uy, (uint32_t)uw2, (uint32_t)uh2);

        if (clock_drawn)
            fb_flip_clock_area();

        /* Окна независимых userspace-процессов (calc.c и т.п.) рисуют
         * себя асинхронно в back-buffer и не сигналят ядру о перерисовке
         * — без этого их видно на экране только там, где случайно прошёл
         * курсор или совпал полный fb_flip() (см. подробный комментарий у
         * ui_flip_userspace_windows() в Framebuffer.c).
         *
         * ВАЖНО (баг "калькулятор загружается медленно и не жмутся
         * кнопки"): раньше это вызывалось АБСОЛЮТНО КАЖДЫЙ тик, безусловно
         * — включая тики, где в окне гарантированно ничего не менялось
         * (пользователь просто двигает мышью по столу). Предполагалось,
         * что копия размером с окно, а не с экран, дёшева всегда — но под
         * TCG QEMU (см. комментарий у fb_flip_rect про ~1с на полный
         * кадр) даже одно окно калькулятора (~220x330x4 байта ≈ 290 КБ)
         * стоит порядка десятков миллисекунд — а таймер тикает раз в
         * ~10мс. Итог был виден в логе планировщика: ui_tick() регулярно
         * занимал больше самого периода таймера, следующий тик
         * просрочивался ещё до входа в задачу, и cooperative-поток
         * калькулятора почти не получал реального времени на CPU — отсюда
         * и "долго грузится", и "клики почти не долетают" (окно всё равно
         * рано или поздно долистывалось курсором/кликом, но крайне редко
         * и с большой задержкой).
         *
         * Раз в тик человеческий глаз всё равно не отличит задержку
         * порядка нескольких десятков миллисекунд, поэтому на простых
         * тиках (мышь просто двигается, ничего не нажато) переносим окна
         * userspace-процессов в VRAM не чаще, чем раз в
         * USERSPACE_WIN_FLIP_EVERY тиков — снижает установившуюся
         * нагрузку в разы, а видимая задержка перерисовки калькулятора
         * (после того как его поток реально получит квант и дорисует
         * кнопку) остаётся в пределах десятков миллисекунд, не заметных
         * при обычном использовании. Во время клика/drag'а (big_change,
         * ветка выше) она всё так же переносится КАЖДЫЙ тик через полный
         * fb_flip() — задержки там нет. */
        #define USERSPACE_WIN_FLIP_EVERY 4u
        static uint32_t s_win_flip_counter = 0;
        if ((s_win_flip_counter++ % USERSPACE_WIN_FLIP_EVERY) == 0)
            ui_flip_userspace_windows();
    }
}
