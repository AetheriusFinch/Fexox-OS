/* Fexox OS Project, EULA GPLv2 */
 


typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef long long          i64;
typedef unsigned long      size_t_;
#define NULL ((void *)0)

 


static inline i64 syscall5(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    i64 ret;
    register u64 r10 __asm__("r10") = a4;
    register u64 r8  __asm__("r8")  = a5;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}
#define SYSCALL0(nr)                   syscall5(nr, 0,0,0,0,0)
#define SYSCALL1(nr,a)                 syscall5(nr, (u64)(a),0,0,0,0)
#define SYSCALL2(nr,a,b)               syscall5(nr, (u64)(a),(u64)(b),0,0,0)
#define SYSCALL3(nr,a,b,c)             syscall5(nr, (u64)(a),(u64)(b),(u64)(c),0,0)
#define SYSCALL4(nr,a,b,c,d)           syscall5(nr, (u64)(a),(u64)(b),(u64)(c),(u64)(d),0)
#define SYSCALL5(nr,a,b,c,d,e)         syscall5(nr, (u64)(a),(u64)(b),(u64)(c),(u64)(d),(u64)(e))

 
#define SYS_WRITE          1
#define SYS_EXIT           60
#define SYS_NANOSLEEP       35
#define SYS_WIN_CREATE     600
#define SYS_WIN_CLOSE      601
#define SYS_WIN_GET_RECT   603
#define SYS_INPUT_POLL     604
#define SYS_KBD_READ_CHAR  605
#define SYS_GET_FRAMEBUFFER 500
#define SYS_NET_SOCKET     700
#define SYS_NET_CONNECT    701
#define SYS_NET_SEND       702
#define SYS_NET_RECV       703
#define SYS_NET_CLOSE      704
#define SYS_NET_RESOLVE    705
#define SC_NET_SOCK_STREAM 1

 


typedef struct { i64 tv_sec, tv_nsec; } sc_timespec_t;
typedef struct { int x, y; unsigned w, h, redraw_gen; } sc_win_rect_t;
typedef struct { int x, y; u8 buttons; } sc_input_t;
typedef struct {
    u64 phys_addr, virt_addr;
    u32 width, height, pitch, bpp;
    u8  mode;
    u8  r_shift, g_shift, b_shift;
} sc_framebuffer_t;

static void sys_exit(int code) { SYSCALL1(SYS_EXIT, code); for (;;) { } }
static i64  sys_write(int fd, const void *buf, size_t_ n) { return SYSCALL3(SYS_WRITE, fd, buf, n); }
static void sys_sleep_ms(u32 ms) {
    sc_timespec_t ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (i64)(ms % 1000) * 1000000;
    SYSCALL2(SYS_NANOSLEEP, &ts, 0);
}
 




static int  sys_win_create(const char *title, u32 w, u32 h) {
    u64 tl = 0; while (title[tl]) tl++;        
    return (int)SYSCALL4(SYS_WIN_CREATE, title, tl, w, h);
}
static void sys_win_close(int win_id) { SYSCALL1(SYS_WIN_CLOSE, win_id); }
static i64  sys_win_get_rect(int win_id, sc_win_rect_t *out) { return SYSCALL2(SYS_WIN_GET_RECT, win_id, out); }
static i64  sys_input_poll(sc_input_t *out) { return SYSCALL1(SYS_INPUT_POLL, out); }
static i64  sys_kbd_read_char(char *out) { return SYSCALL1(SYS_KBD_READ_CHAR, out); }
static i64  sys_get_framebuffer(sc_framebuffer_t *out) { return SYSCALL1(SYS_GET_FRAMEBUFFER, out); }
static i64  sys_net_socket(int type) { return SYSCALL1(SYS_NET_SOCKET, type); }
static i64  sys_net_connect(int sock, u32 ip, u16 port, u32 timeout_ms) { return SYSCALL4(SYS_NET_CONNECT, sock, ip, port, timeout_ms); }
static i64  sys_net_send(int sock, const void *buf, size_t_ len, u32 timeout_ms) { return SYSCALL4(SYS_NET_SEND, sock, buf, len, timeout_ms); }
static i64  sys_net_recv(int sock, void *buf, size_t_ len, u32 timeout_ms) { return SYSCALL4(SYS_NET_RECV, sock, buf, len, timeout_ms); }
static void sys_net_close(int sock) { SYSCALL1(SYS_NET_CLOSE, sock); }
static i64  sys_net_resolve(const char *name, u32 *out_ip, u32 timeout_ms) { return SYSCALL3(SYS_NET_RESOLVE, name, out_ip, timeout_ms); }

 


 








void *memset(void *d, int c, size_t_ n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, size_t_ n) { u8 *dd = (u8 *)d; const u8 *ss = (const u8 *)s; while (n--) *dd++ = *ss++; return d; }
void *memmove(void *d, const void *s, size_t_ n) {
    u8 *dd = (u8 *)d; const u8 *ss = (const u8 *)s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
static void  *u_memcpy(void *d, const void *s, size_t_ n) { return memcpy(d, s, n); }
static size_t_ u_strlen(const char *s) { size_t_ n = 0; while (s[n]) n++; return n; }
static int    u_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
static void   u_strcpy(char *d, const char *s) { while ((*d++ = *s++)) { } }
static int    u_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int    u_strnicmp(const char *a, const char *b, size_t_ n) {
    while (n--) { int ca = u_tolower((u8)*a), cb = u_tolower((u8)*b); if (ca != cb) return ca - cb; if (!*a) return 0; a++; b++; }
    return 0;
}

 


#include "font8x8.h"
#define GLYPH_W 8
#define GLYPH_H 8

 
#include "webcore.h"
#include "webjs.h"

 


static sc_framebuffer_t g_fb;

static void put_pixel(int x, int y, u8 r, u8 g, u8 b) {
    if (x < 0 || y < 0 || (u32)x >= g_fb.width || (u32)y >= g_fb.height) return;
    u32 native = ((u32)r << g_fb.r_shift) | ((u32)g << g_fb.g_shift) | ((u32)b << g_fb.b_shift);
    u8 *p = (u8 *)(size_t_)g_fb.virt_addr + (size_t_)y * g_fb.pitch + (size_t_)x * (g_fb.bpp / 8);
    if (g_fb.bpp == 32) *(u32 *)p = native;
    else if (g_fb.bpp == 24) { p[0] = (u8)native; p[1] = (u8)(native >> 8); p[2] = (u8)(native >> 16); }
    else if (g_fb.bpp == 16) *(u16 *)p = (u16)native;
}

static void fill_rect(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put_pixel(x + i, y + j, r, g, b);
}

static void draw_rect_border(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int i = 0; i < w; i++) { put_pixel(x + i, y, r, g, b); put_pixel(x + i, y + h - 1, r, g, b); }
    for (int j = 0; j < h; j++) { put_pixel(x, y + j, r, g, b); put_pixel(x + w - 1, y + j, r, g, b); }
}

static void draw_char(int x, int y, char ch, u8 r, u8 g, u8 b) {
    u8 c = (u8)ch;
    if (c < 0x20 || c > 0x7E) c = '?';
    const u8 *rows = g_font8x8[c - 0x20];
    for (int row = 0; row < GLYPH_H; row++) {
        u8 bits = rows[row];
        for (int col = 0; col < GLYPH_W; col++)
            if ((bits >> col) & 1) put_pixel(x + col, y + row, r, g, b);
    }
}

static void draw_string(int x, int y, const char *s, u8 r, u8 g, u8 b) {
    int cx = x;
    for (; *s; s++) {
        if (*s == '\n') { break; }         
        draw_char(cx, y, *s, r, g, b);
        cx += GLYPH_W;
    }
}

static void draw_string_n(int x, int y, const char *s, int n, u8 r, u8 g, u8 b) {
    int cx = x;
    for (int i = 0; i < n && s[i]; i++) { draw_char(cx, y, s[i], r, g, b); cx += GLYPH_W; }
}

 



#define MAX_HOST 128
#define MAX_PATH 512

typedef struct { char host[MAX_HOST]; char path[MAX_PATH]; u16 port; } url_t;

static int parse_url(const char *url, url_t *out) {
    const char *p = url;
    while (*p == ' ') p++;
    if (u_strnicmp(p, "http://", 7) == 0) p += 7;
    else if (u_strnicmp(p, "https://", 8) == 0) return -1;    

    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < MAX_HOST - 1) out->host[hi++] = *p++;
    out->host[hi] = 0;
    if (hi == 0) return -1;

    out->port = 80;
    if (*p == ':') {
        p++;
        u32 port = 0;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (u32)(*p - '0'); p++; }
        if (port == 0 || port > 0xFFFF) return -1;
        out->port = (u16)port;
    }

    int pi = 0;
    if (*p == '\0') { out->path[pi++] = '/'; }
    else while (*p && pi < MAX_PATH - 1) out->path[pi++] = *p++;
    out->path[pi] = 0;
    return 0;
}

 



#define PAGE_BUF_SIZE (256u * 1024u)
static char g_page_raw[PAGE_BUF_SIZE];       
static u32  g_page_raw_len;

#define STATUS_MAX 96
static char g_status[STATUS_MAX];            

 




static int  g_loading = 0;
static int  g_progress = 0;
static int  g_fetch_partial = 0;      
static void render(int hover_back, int hover_fwd, int hover_up, int hover_dn);
static const char *g_prog_prefix = "";      
static int g_css_ok = 0, g_css_skip = 0;
static void ui_progress(const char *status, int pct) {
    { int o = 0; const char *q = g_prog_prefix; while (*q && o < STATUS_MAX - 1) g_status[o++] = *q++;
      q = status; while (*q && o < STATUS_MAX - 1) g_status[o++] = *q++; g_status[o] = 0; }
    g_progress = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    g_loading = 1;
    render(0, 0, 0, 0);
}
static int fetch_fail(int sock, const char *msg) {
    if (sock >= 0) sys_net_close(sock);
    g_loading = 0;
    u_strcpy(g_status, msg);
    return -1;
}

 
static int http_fetch(const url_t *u) {
    g_page_raw_len = 0;
    g_fetch_partial = 0;
    ui_progress("Resolving host...", 5);

     
    u32 ip = 0;
    i64 r = -1;
    for (int attempt = 0; attempt < 5; attempt++) {
        r = sys_net_resolve(u->host, &ip, 1000);
        if (r == 0) break;
        ui_progress("Resolving host (retrying)...", 5 + (attempt + 1) * 4);
    }
    if (r != 0) return fetch_fail(-1, "DNS lookup failed");
    ui_progress("Host found", 25);

    int sock = (int)sys_net_socket(SC_NET_SOCK_STREAM);
    if (sock < 0) return fetch_fail(-1, "No socket (network down?)");

     

    ui_progress("Connecting...", 30);
    r = -1;
    for (int i = 0; i < 30; i++) {                           
        r = sys_net_connect(sock, ip, u->port, 500);
        if (r == 0) break;
        if (r != -110  ) break;           
        ui_progress("Connecting (waiting for server)...", 30 + (i + 1) * 24 / 30);
    }
    if (r != 0) return fetch_fail(sock, r == -110 ? "Connection timed out" : "Connection failed");
    ui_progress("Connected", 55);

     
    static char req[MAX_PATH + MAX_HOST + 128];
    char *w = req;
    const char *p1 = "GET "; while (*p1) *w++ = *p1++;
    const char *pp = u->path; while (*pp) *w++ = *pp++;
    const char *p2 = " HTTP/1.0\r\nHost: "; while (*p2) *w++ = *p2++;
    const char *hh = u->host; while (*hh) *w++ = *hh++;
    const char *p3 = "\r\nUser-Agent: Fexox OS x86_64\r\nConnection: close\r\n\r\n"; while (*p3) *w++ = *p3++;
    *w = 0;
    size_t_ reqlen = (size_t_)(w - req);

    ui_progress("Sending request...", 60);
    size_t_ sent = 0;
    while (sent < reqlen) {
        i64 n = sys_net_send(sock, req + sent, reqlen - sent, 5000);
        if (n <= 0) return fetch_fail(sock, "Send failed");
        sent += (size_t_)n;
    }

    ui_progress("Waiting for response...", 65);
    for (;;) {
        if (g_page_raw_len >= PAGE_BUF_SIZE - 4096) break;    
        i64 n = sys_net_recv(sock, g_page_raw + g_page_raw_len, PAGE_BUF_SIZE - g_page_raw_len, 8000);
        if (n < 0) {                                           
            g_fetch_partial = (int)(-n);                       
            sys_net_close(sock);
            g_loading = 0;
            u_strcpy(g_status, "Receive error");
            return g_page_raw_len ? 0 : -1;
        }
        if (n == 0) break;                                     
        g_page_raw_len += (u32)n;
         
        u32 got = g_page_raw_len > 65536u ? 65536u : g_page_raw_len;
        ui_progress("Receiving data...", 65 + (int)(got * 30u / 65536u));
    }
    sys_net_close(sock);
    if (g_page_raw_len == 0) return fetch_fail(-1, "Empty response");
    g_loading = 0;
    u_strcpy(g_status, "OK");
    return 0;
}

 


static const u8 *find_body_start(const u8 *raw, u32 len, u32 *header_len_out) {
    for (u32 i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            *header_len_out = i + 4;
            return raw + i + 4;
        }
    }
     
    for (u32 i = 0; i + 1 < len; i++) {
        if (raw[i] == '\n' && raw[i + 1] == '\n') { *header_len_out = i + 2; return raw + i + 2; }
    }
    *header_len_out = 0;
    return raw;                           
}

static int hdr_status_code(const u8 *raw, u32 len) {
    u32 i = 0;
    while (i < len && raw[i] != ' ') i++;
    i++;
    int c = 0;
    for (int k = 0; k < 3; k++, i++) { if (i >= len || raw[i] < '0' || raw[i] > '9') return 0; c = c * 10 + (raw[i] - '0'); }
    return c;
}
 
static int hdr_find(const u8 *raw, u32 hlen, const char *name, char *out, int cap) {
    int nl = (int)u_strlen(name);
    for (u32 i = 0; i + (u32)nl + 1 < hlen; i++) {
        if ((i == 0 || raw[i - 1] == '\n') && u_strnicmp((const char *)raw + i, name, (size_t_)nl) == 0 && raw[i + nl] == ':') {
            u32 j = i + (u32)nl + 1;
            while (j < hlen && (raw[j] == ' ' || raw[j] == '\t')) j++;
            int o = 0;
            while (j < hlen && raw[j] != '\r' && raw[j] != '\n' && o < cap - 1) out[o++] = (char)raw[j++];
            out[o] = 0;
            return 1;
        }
    }
    out[0] = 0;
    return 0;
}
static int u_contains_ci(const char *hay, const char *needle) {
    int nl = (int)u_strlen(needle);
    for (int i = 0; hay[i]; i++) if (u_strnicmp(hay + i, needle, (size_t_)nl) == 0) return 1;
    return 0;
}

 


#define TOPBAR_H       34
#define BTN_W          44
#define BTN_GAP        6
#define CONTENT_PAD_X  10
#define CONTENT_PAD_Y  8
#define LINE_H         12
#define SCROLLBTN_W    22

#define MAX_URL        700
#define MAX_HISTORY    32

static char g_addr_text[MAX_URL] = "http://";
static int  g_addr_len = 7;
static int  g_addr_focused = 1;    

static char g_history[MAX_HISTORY][MAX_URL];
static int  g_history_count = 0;
static int  g_history_index = -1;

static int  g_scroll_px = 0;               
static int  g_page_loaded = 0;
static char g_cur_url[MAX_URL];            

static int  g_win_id = -1;
static int  g_content_w, g_content_h;    

static u8   g_prev_buttons = 0;

 

static int win_x, win_y;
static int back_x, back_y, back_w, back_h;
static int fwd_x, fwd_y, fwd_w, fwd_h;
static int go_x, go_y, go_w, go_h;
static int g_hover_go = 0;
static int addr_x, addr_y, addr_w, addr_h;
static int content_x, content_y, content_w, content_h;
static int scrollup_x, scrollup_y, scrolldn_x, scrolldn_y, scrollbtn_h;

static void layout(void) {
    back_x = win_x + CONTENT_PAD_X; back_y = win_y + 4; back_w = BTN_W; back_h = TOPBAR_H - 8;
    fwd_x = win_x + g_content_w - CONTENT_PAD_X - BTN_W; fwd_y = back_y; fwd_w = BTN_W; fwd_h = back_h;
    go_w = BTN_W; go_h = back_h; go_y = back_y;
    go_x = fwd_x - BTN_GAP - go_w;                      
    addr_x = back_x + back_w + BTN_GAP;
    addr_y = back_y;
    addr_w = go_x - BTN_GAP - addr_x;
    if (addr_w < 20) addr_w = 20;
    addr_h = back_h;

    content_x = win_x;
    content_y = win_y + TOPBAR_H;
    content_w = g_content_w - SCROLLBTN_W;
    content_h = g_content_h - TOPBAR_H;
    if (content_h < 0) content_h = 0;

    scrollup_x = win_x + g_content_w - SCROLLBTN_W;
    scrollup_y = content_y;
    scrollbtn_h = content_h / 2;
    scrolldn_x = scrollup_x;
    scrolldn_y = content_y + scrollbtn_h;
}

static int view_h(void) {
    int h = content_h > 0 ? content_h : 0;
    if (g_loading || g_status[0]) h -= LINE_H + 2;         
    return h > 0 ? h : 0;
}
static void clamp_scroll(void) {
    int mx = wc_doc_h - view_h();
    if (mx < 0) mx = 0;
    if (g_scroll_px > mx) g_scroll_px = mx;
    if (g_scroll_px < 0) g_scroll_px = 0;
}

 


static void history_push(const char *url) {
     
    g_history_index++;
    g_history_count = g_history_index;
    if (g_history_count >= MAX_HISTORY) {
        for (int i = 1; i < MAX_HISTORY; i++) { char *d = g_history[i - 1]; const char *sr = g_history[i]; while ((*d++ = *sr++)) { } }
        g_history_index--; g_history_count--;
    }
    u_strcpy(g_history[g_history_index], url);
    g_history_count = g_history_index + 1;
}

 
static void str_put(char *d, int cap, int *o, const char *s) { while (*s && *o < cap - 1) d[(*o)++] = *s++; d[*o] = 0; }
static void resolve_url(const char *base, const char *href, char *out, int cap) {
    int o = 0; out[0] = 0;
    while (*href == ' ') href++;
    if (u_strnicmp(href, "http://", 7) == 0 || u_strnicmp(href, "https://", 8) == 0) { str_put(out, cap, &o, href); return; }
    const char *b = base;
    while (*b == ' ') b++;
    if (u_strnicmp(b, "http://", 7) == 0) b += 7;
    char origin[MAX_URL]; int ol = 0;
    str_put(origin, MAX_URL, &ol, "http://");
    while (*b && *b != '/' && ol < MAX_URL - 1) origin[ol++] = *b++;
    origin[ol] = 0;
    if (href[0] == '/' && href[1] == '/') { str_put(out, cap, &o, "http:"); str_put(out, cap, &o, href); return; }
    if (href[0] == '/') { str_put(out, cap, &o, origin); str_put(out, cap, &o, href); return; }
     
    int last = -1;
    for (int i = 0; b[i] && b[i] != '?' && b[i] != '#'; i++) if (b[i] == '/') last = i;
    str_put(out, cap, &o, origin);
    if (last >= 0) { for (int i = 0; i <= last && o < cap - 1; i++) out[o++] = b[i]; out[o] = 0; }
    else str_put(out, cap, &o, "/");
    while (href[0] == '.' && href[1] == '/') href += 2;
    str_put(out, cap, &o, href);
}

 
static void u_append(char *d, int cap, const char *s) { int l = (int)u_strlen(d); while (*s && l < cap - 1) d[l++] = *s++; d[l] = 0; }
static void u_append_num(char *d, int cap, int n) { char t[12]; int tl = 0; if (n <= 0) t[tl++] = '0'; while (n > 0 && tl < 10) { t[tl++] = (char)('0' + n % 10); n /= 10; } char b[12]; int bl = 0; while (tl > 0) b[bl++] = t[--tl]; b[bl] = 0; u_append(d, cap, b); }
static void set_load_status(void) {
    g_status[0] = 0;
    if (wj_status[0]) { u_append(g_status, STATUS_MAX, wj_status); }                 
    else if (wc_title.len) {
        for (u32 i = 0; i < wc_title.len && i < 44; i++) { char c = WCS(wc_title)[i]; char t[2]; t[0] = (c < 0x20) ? ' ' : c; t[1] = 0; u_append(g_status, STATUS_MAX, t); }
    } else u_append(g_status, STATUS_MAX, "OK");
    if (g_fetch_partial) {
        u_append(g_status, STATUS_MAX, g_fetch_partial == 11 ? " (partial)" : g_fetch_partial == 104 ? " (reset)" : " (partial)");
    }
    if (g_css_ok || g_css_skip || wj_scripts_run || wj_scripts_skipped) {
        u_append(g_status, STATUS_MAX, "  [CSS "); u_append_num(g_status, STATUS_MAX, g_css_ok); u_append(g_status, STATUS_MAX, "/"); u_append_num(g_status, STATUS_MAX, g_css_ok + g_css_skip);
        u_append(g_status, STATUS_MAX, "  JS "); u_append_num(g_status, STATUS_MAX, wj_scripts_run); u_append(g_status, STATUS_MAX, "/"); u_append_num(g_status, STATUS_MAX, wj_scripts_run + wj_scripts_skipped);
        if (wj_script_errors) { u_append(g_status, STATUS_MAX, " err "); u_append_num(g_status, STATUS_MAX, wj_script_errors); }
        u_append(g_status, STATUS_MAX, "]");
        if (wj_script_errors && wj_err_text[0]) {                                   
            u_append(g_status, STATUS_MAX, " ");
            int l = (int)u_strlen(g_status); const char *e = wj_err_text + 14;        
            while (*e && l < STATUS_MAX - 1) { g_status[l++] = *e++; } g_status[l] = 0;
        }
    }
}

 
static void page_finish(void) {
    wc_prepare_and_layout(content_w);
    g_scroll_px = 0;
    g_page_loaded = 1;
    g_loading = 0;
}
 

static int fetch_resource(const char *abs_url, const u8 **body, u32 *blen) {
    char url[MAX_URL];
    u_strcpy(url, abs_url);
    for (int hop = 0; hop < 3; hop++) {
        url_t u;
        if (parse_url(url, &u) != 0) return -2;                           
        int rc = http_fetch(&u);
        if (rc != 0 && g_page_raw_len == 0) return -1;
        u32 hl = 0;
        const u8 *b = find_body_start((const u8 *)g_page_raw, g_page_raw_len, &hl);
        if (!hl) return -4;
        int code = hdr_status_code((const u8 *)g_page_raw, hl);
        if (code >= 300 && code < 400) {
            char loc[MAX_URL], next[MAX_URL];
            if (!hdr_find((const u8 *)g_page_raw, hl, "Location", loc, MAX_URL) || !loc[0]) return -3;
            resolve_url(url, loc, next, MAX_URL);
            u_strcpy(url, next);
            continue;
        }
        if (code != 200) return -3;
        *body = b; *blen = g_page_raw_len - hl;
        return 0;
    }
    return -5;
}

 


static void load_subresources(void) {
    g_css_ok = g_css_skip = 0;
    int total = wc_node_count;
    int css_n = 0, js_n = 0;
    for (int i = 1; i < total; i++) {
        wc_node *nd = &wc_nodes[i];
        if (nd->type != 1) continue;
        int is_css = (nd->tag == TG_LINK && (nd->flags & 0x40) && !(nd->flags & 0x80) && nd->href.len);
        int is_js  = (nd->tag == TG_SCRIPT && nd->aux.len && !(nd->flags & 0x20));
        if (!is_css && !is_js) continue;
        if (is_css) { if (css_n >= 6) { g_css_skip++; continue; } css_n++; }
        else        { if (js_n >= 4) continue; js_n++; }
        char abs[MAX_URL];
        resolve_url(g_cur_url, is_css ? WCS(nd->href) : WCS(nd->aux), abs, MAX_URL);
        g_prog_prefix = is_css ? "CSS: " : "JS: ";
        const u8 *body = NULL; u32 blen = 0;
        int rc = fetch_resource(abs, &body, &blen);
        if (rc != 0 || blen == 0) { if (is_css) g_css_skip++; continue; }
        if (is_css) {
            if (blen > 120000) { blen = 120000; while (blen > 0 && body[blen - 1] != '}') blen--; }    
            if (wc_pool_len + blen + 64 > WC_POOL_SIZE) { g_css_skip++; continue; }
            int st = wc_new_node(1, TG_STYLE, TF_RAW), tx = wc_new_node(2, 0, 0);
            if (st < 0 || tx < 0) { g_css_skip++; continue; }
            wc_nodes[tx].text = wc_put((const char *)body, blen);
            wc_append_child(st, tx);
            wc_insert_after(i, st);
            g_css_ok++;
        } else {
            if (blen > 60000) continue;                                   
            int tx = wc_new_node(2, 0, 0);
            if (tx < 0) continue;
            wc_nodes[tx].text = wc_put((const char *)body, blen);
            wc_append_child(i, tx);
            nd->aux.off = nd->aux.len = 0;                                
        }
    }
    g_prog_prefix = "";
    g_loading = 0;
}

static void page_load_html(const u8 *html, u32 len) {
    wc_parse_document(html, len);
    load_subresources();                  
    u_strcpy(wj_page_url, g_cur_url);     
    wj_run_page();                        
    wj_dirty = 0;
    page_finish();
    set_load_status();
}
static void page_load_text(const char *txt, u32 n) {
    g_css_ok = g_css_skip = 0;
    static const char *skel = "<pre></pre>";
    wc_parse_document((const u8 *)skel, 11);
    wj_reset();
    int pre = wc_find_tag_node(TG_PRE);
    if (pre >= 0) wc_set_text(pre, txt, n);
    page_finish();
}
static void show_message(const char *msg) {
    u_strcpy(g_status, msg);
    page_load_text(msg, (u32)u_strlen(msg));
    u_strcpy(g_status, msg);
}
static void relayout_dom_changed(void) {
    wj_dirty = 0;
    wc_prepare_and_layout(content_w);
    clamp_scroll();
    if (wj_status[0]) { g_status[0] = 0; u_append(g_status, STATUS_MAX, wj_status); }
}

static void navigate(const char *url0) {
    char url[MAX_URL];
    u_strcpy(url, url0);
    u_strcpy(g_status, "");
    for (int hop = 0; hop < 5; hop++) {
        url_t u;
        if (parse_url(url, &u) != 0) {
            static char m[MAX_URL + 64]; m[0] = 0;
            u_append(m, (int)sizeof(m), "Cannot open: only http:// URLs are supported (");
            u_append(m, (int)sizeof(m), url); u_append(m, (int)sizeof(m), ")");
            g_loading = 0; g_page_raw_len = 0;
            show_message(m);
            return;
        }
        int rc = http_fetch(&u);
        if (rc != 0 && g_page_raw_len == 0) { char m[STATUS_MAX]; u_strcpy(m, g_status); g_loading = 0; show_message(m); return; }

        u32 hdr_len = 0;
        const u8 *body = find_body_start((const u8 *)g_page_raw, g_page_raw_len, &hdr_len);
        u32 body_len = g_page_raw_len > hdr_len ? g_page_raw_len - hdr_len : 0;
        int code = hdr_len ? hdr_status_code((const u8 *)g_page_raw, hdr_len) : 0;

        if (code >= 300 && code < 400 && hdr_len) {                       
            char loc[MAX_URL];
            if (hdr_find((const u8 *)g_page_raw, hdr_len, "Location", loc, MAX_URL) && loc[0]) {
                char next[MAX_URL];
                resolve_url(url, loc, next, MAX_URL);
                u_strcpy(url, next);
                u_strcpy(g_addr_text, url); g_addr_len = (int)u_strlen(g_addr_text);
                if (g_history_index >= 0) u_strcpy(g_history[g_history_index], url);
                continue;
            }
        }
        u_strcpy(g_cur_url, url);
        g_loading = 0;
        if (hdr_len == 0 && g_page_raw_len > 0) {                           
            static char m[160]; m[0] = 0;
            u_append(m, (int)sizeof(m), "Incomplete response: connection stalled after ");
            u_append_num(m, (int)sizeof(m), (int)g_page_raw_len);
            u_append(m, (int)sizeof(m), " bytes (no end of HTTP headers)");
            show_message(m);
            return;
        }

        char ctype[96]; ctype[0] = 0;
        if (hdr_len) hdr_find((const u8 *)g_page_raw, hdr_len, "Content-Type", ctype, (int)sizeof(ctype));
        if (body_len == 0) { show_message(g_status[0] ? g_status : "Empty response"); return; }
        int is_html = ctype[0] ? u_contains_ci(ctype, "html") : 0;
        int is_text = ctype[0] ? (u_contains_ci(ctype, "text/") && !is_html) : 0;
        if (!ctype[0]) {                                                 
            for (u32 i = 0; i < body_len && i < 512; i++) if (body[i] == '<') { is_html = 1; break; }
            if (!is_html) is_text = 1;
        }
        if (is_html) page_load_html(body, body_len);
        else if (is_text) { page_load_text((const char *)body, body_len); u_strcpy(g_status, "OK (plain text)"); }
        else { char m[160]; m[0] = 0; u_append(m, 160, "Unsupported content type: "); u_append(m, 160, ctype); show_message(m); }
        return;
    }
    g_loading = 0;
    show_message("Too many redirects");
}

static void go_to_address_bar(void) {
    char url[MAX_URL];
    int n = g_addr_len; if (n >= MAX_URL) n = MAX_URL - 1;
    u_memcpy(url, g_addr_text, (size_t_)n); url[n] = 0;
    history_push(url);
    navigate(url);
}

static void go_back(void) {
    if (g_history_index <= 0) return;
    g_history_index--;
    u_strcpy(g_addr_text, g_history[g_history_index]);
    g_addr_len = (int)u_strlen(g_addr_text);
    navigate(g_addr_text);
}

static void go_forward(void) {
    if (g_history_index + 1 >= g_history_count) return;
    g_history_index++;
    u_strcpy(g_addr_text, g_history[g_history_index]);
    g_addr_len = (int)u_strlen(g_addr_text);
    navigate(g_addr_text);
}

 


#define COL_BG_R      250
#define COL_BG_G      250
#define COL_BG_B      250
#define COL_TOPBAR_R  210
#define COL_TOPBAR_G  212
#define COL_TOPBAR_B  218
#define COL_BTN_R     225
#define COL_BTN_G     227
#define COL_BTN_B     232
#define COL_BTN_HOVER_R 200
#define COL_BTN_HOVER_G 205
#define COL_BTN_HOVER_B 215
#define COL_BORDER_R  120
#define COL_BORDER_G  122
#define COL_BORDER_B  128
#define COL_TEXT_R    20
#define COL_TEXT_G    20
#define COL_TEXT_B    24
#define COL_ADDR_R    255
#define COL_ADDR_G    255
#define COL_ADDR_B    255
#define COL_ADDR_FOCUS_R 255
#define COL_ADDR_FOCUS_G 255
#define COL_ADDR_FOCUS_B 240

 
static int g_clip_x0, g_clip_y0, g_clip_x1, g_clip_y1;
static void crect(int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    int x0 = x < g_clip_x0 ? g_clip_x0 : x, y0 = y < g_clip_y0 ? g_clip_y0 : y;
    int x1 = (x + w) > g_clip_x1 ? g_clip_x1 : (x + w), y1 = (y + h) > g_clip_y1 ? g_clip_y1 : (y + h);
    if (x1 <= x0 || y1 <= y0) return;
    fill_rect(x0, y0, x1 - x0, y1 - y0, r, g, b);
}
static void cglyph(int x, int y, u8 ch, int scale, u8 r, u8 g, u8 b) {
    if (ch < 0x20 || ch > 0x7E) ch = '?';
    if (x + 8 * scale <= g_clip_x0 || x >= g_clip_x1 || y + 8 * scale <= g_clip_y0 || y >= g_clip_y1) return;
    const u8 *rows = g_font8x8[ch - 0x20];
    for (int row = 0; row < 8; row++) {
        u8 bits = rows[row];
        if (!bits) continue;
        for (int col = 0; col < 8; col++) {
            if (!((bits >> col) & 1)) continue;
            if (scale == 1) {
                int px = x + col, py = y + row;
                if (px >= g_clip_x0 && px < g_clip_x1 && py >= g_clip_y0 && py < g_clip_y1) put_pixel(px, py, r, g, b);
            } else crect(x + col * scale, y + row * scale, scale, scale, r, g, b);
        }
    }
}
static void draw_page(void) {
    int vx = content_x, vy = content_y, vw = content_w, vh = view_h();
    g_clip_x0 = vx; g_clip_y0 = vy; g_clip_x1 = vx + vw; g_clip_y1 = vy + vh;
    clamp_scroll();
    int sc = g_scroll_px;
    fill_rect(vx, vy, vw, vh, wc_page_bg.r, wc_page_bg.g, wc_page_bg.b);
    for (int i = 0; i < wc_box_count; i++) {
        wc_box *bx = &wc_boxes[i];
        if (bx->y1 <= sc || bx->y0 >= sc + vh) continue;
        int bx0 = vx + bx->x0, by0 = vy + bx->y0 - sc, bw_ = bx->x1 - bx->x0, bh_ = bx->y1 - bx->y0;
        if (bx->has_bg) crect(bx0, by0, bw_, bh_, bx->bg[0], bx->bg[1], bx->bg[2]);
        if (bx->bw[0]) crect(bx0, by0, bw_, bx->bw[0], bx->bc[0], bx->bc[1], bx->bc[2]);                               
        if (bx->bw[2]) crect(bx0, by0 + bh_ - bx->bw[2], bw_, bx->bw[2], bx->bc[0], bx->bc[1], bx->bc[2]);             
        if (bx->bw[3]) crect(bx0, by0, bx->bw[3], bh_, bx->bc[0], bx->bc[1], bx->bc[2]);                               
        if (bx->bw[1]) crect(bx0 + bw_ - bx->bw[1], by0, bx->bw[1], bh_, bx->bc[0], bx->bc[1], bx->bc[2]);             
    }
    for (int i = 0; i < wc_line_count; i++) {
        wc_line *ln = &wc_lines[i];
        if (ln->y + ln->h <= sc) continue;
        if (ln->y >= sc + vh) break;
        int ly = vy + ln->y - sc;
        if (ln->kind == 1) { crect(vx + ln->x0, ly + 4, ln->x1 - ln->x0, 1, 140, 140, 146); crect(vx + ln->x0, ly + 5, ln->x1 - ln->x0, 1, 235, 235, 240); continue; }
        for (u32 si = 0; si < ln->seg_count; si++) {
            wc_seg *sg = &wc_segs[ln->seg_start + si];
            int cw = 8 * sg->scale, w = sg->len * cw;
            int sx = vx + sg->x;
            if (sx >= g_clip_x1 || sx + w <= g_clip_x0) continue;
            int sy = ly + ln->h - 8 * sg->scale - 2;
            if (sg->has_bg) crect(sx, ly, w, ln->h, sg->bg[0], sg->bg[1], sg->bg[2]);
            const char *t = wc_ltext + sg->toff;
            for (u32 k = 0; k < sg->len; k++) {
                int gx = sx + (int)k * cw;
                if (gx + cw <= g_clip_x0) continue;
                if (gx >= g_clip_x1) break;
                if (t[k] == ' ') continue;
                cglyph(gx, sy, (u8)t[k], sg->scale, sg->fg[0], sg->fg[1], sg->fg[2]);
                if (sg->flags & WCF_BOLD) cglyph(gx + 1, sy, (u8)t[k], sg->scale, sg->fg[0], sg->fg[1], sg->fg[2]);
            }
            if (sg->flags & WCF_UL) crect(sx, sy + 8 * sg->scale, w, 1, sg->fg[0], sg->fg[1], sg->fg[2]);
            if (sg->flags & WCF_STRIKE) crect(sx, sy + 4 * sg->scale, w, 1, sg->fg[0], sg->fg[1], sg->fg[2]);
        }
    }
    g_clip_x0 = g_clip_y0 = 0; g_clip_x1 = (int)g_fb.width; g_clip_y1 = (int)g_fb.height;
}

static void draw_button(int x, int y, int w, int h, const char *label, int hovered) {
    if (hovered) fill_rect(x, y, w, h, COL_BTN_HOVER_R, COL_BTN_HOVER_G, COL_BTN_HOVER_B);
    else         fill_rect(x, y, w, h, COL_BTN_R, COL_BTN_G, COL_BTN_B);
    draw_rect_border(x, y, w, h, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
    int tw = (int)u_strlen(label) * GLYPH_W;
    draw_string(x + (w - tw) / 2, y + (h - GLYPH_H) / 2, label, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
}

static int point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void render(int hover_back, int hover_fwd, int hover_up, int hover_dn) {
    fill_rect(win_x, win_y, g_content_w, g_content_h, COL_BG_R, COL_BG_G, COL_BG_B);
    fill_rect(win_x, win_y, g_content_w, TOPBAR_H, COL_TOPBAR_R, COL_TOPBAR_G, COL_TOPBAR_B);

    draw_button(back_x, back_y, back_w, back_h, "<Back", hover_back);
    draw_button(fwd_x, fwd_y, fwd_w, fwd_h, "Fwd>", hover_fwd);
    draw_button(go_x, go_y, go_w, go_h, "Go", g_hover_go);

    fill_rect(addr_x, addr_y, addr_w, addr_h,
             g_addr_focused ? COL_ADDR_FOCUS_R : COL_ADDR_R,
             g_addr_focused ? COL_ADDR_FOCUS_G : COL_ADDR_G,
             g_addr_focused ? COL_ADDR_FOCUS_B : COL_ADDR_B);
    draw_rect_border(addr_x, addr_y, addr_w, addr_h, COL_BORDER_R, COL_BORDER_G, COL_BORDER_B);
    {
        int max_chars = (addr_w - 8) / GLYPH_W;
        int show_from = 0;
        if (g_addr_len > max_chars) show_from = g_addr_len - max_chars;
        draw_string_n(addr_x + 4, addr_y + (addr_h - GLYPH_H) / 2, g_addr_text + show_from, g_addr_len - show_from,
                     COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
        if (g_addr_focused) {
            int cx = addr_x + 4 + (g_addr_len - show_from) * GLYPH_W;
            fill_rect(cx, addr_y + 3, 2, addr_h - 6, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
        }
    }

     
    if (!g_page_loaded) {
        static const char *hint = "Type an address above and press Enter or click Go. (http:// only)";
        draw_string(content_x + CONTENT_PAD_X, content_y + CONTENT_PAD_Y, hint, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
    } else {
        draw_page();
    }

     
    draw_button(scrollup_x, scrollup_y, SCROLLBTN_W, scrollbtn_h, "^", hover_up);
    draw_button(scrolldn_x, scrolldn_y, SCROLLBTN_W, content_h - scrollbtn_h, "v", hover_dn);

     
    if (g_loading || g_status[0]) {
        int sy = content_y + content_h - LINE_H - 2;
        if (sy > content_y) {
            fill_rect(content_x, sy, content_w, LINE_H + 2, COL_TOPBAR_R, COL_TOPBAR_G, COL_TOPBAR_B);
            if (g_loading) {                                   
                int bw = (content_w * g_progress) / 100;
                fill_rect(content_x, sy, bw, LINE_H + 2, 140, 200, 140);
            }
            char line[STATUS_MAX + 16];
            char *w = line;
            if (g_loading) {                                   
                int p = g_progress;
                if (p >= 100) { *w++ = '1'; *w++ = '0'; *w++ = '0'; }
                else { if (p >= 10) *w++ = (char)('0' + p / 10); *w++ = (char)('0' + p % 10); }
                *w++ = '%'; *w++ = ' '; *w++ = '-'; *w++ = ' ';
            }
            const char *sp = g_status;
            while (*sp) *w++ = *sp++;
            *w = 0;
            draw_string(content_x + CONTENT_PAD_X, sy + 1, line, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
        }
    }
}

 


static void addr_backspace(void) { if (g_addr_len > 0) g_addr_len--; g_addr_text[g_addr_len] = 0; }
static void addr_putc(char c) {
    if (g_addr_len < MAX_URL - 1) { g_addr_text[g_addr_len++] = c; g_addr_text[g_addr_len] = 0; }
}

static int handle_keyboard(void) {
    int changed = 0;    
    char ch;
    while (sys_kbd_read_char(&ch) == 1) {
         



        if (!g_addr_focused) {
            if ((u8)ch >= 0x20 && (u8)ch < 0x7F) g_addr_focused = 1;
            else continue;
        }
        if (ch == '\n' || ch == '\r') {
            g_addr_focused = 0;
            go_to_address_bar();
            changed = 1;
        } else if (ch == '\b') {
            addr_backspace();
            changed = 1;
        } else if (ch == '\t') {
             
        } else if ((u8)ch >= 0x20 && (u8)ch < 0x7F) {
            addr_putc(ch);
            changed = 1;
        }
    }
    return changed;
}

static void handle_mouse(int *hover_back, int *hover_fwd, int *hover_up, int *hover_dn, int *need_redraw) {
    sc_input_t in;
    if (sys_input_poll(&in) != 0) return;

    g_hover_go  = point_in(in.x, in.y, go_x, go_y, go_w, go_h);
    *hover_back = point_in(in.x, in.y, back_x, back_y, back_w, back_h);
    *hover_fwd  = point_in(in.x, in.y, fwd_x, fwd_y, fwd_w, fwd_h);
    *hover_up   = point_in(in.x, in.y, scrollup_x, scrollup_y, SCROLLBTN_W, scrollbtn_h);
    *hover_dn   = point_in(in.x, in.y, scrolldn_x, scrolldn_y, SCROLLBTN_W, content_h - scrollbtn_h);

    int lmb_now = (in.buttons & 1) != 0;
    int lmb_edge = lmb_now && !(g_prev_buttons & 1);    
    g_prev_buttons = in.buttons;

    if (!lmb_edge) return;

    if (g_hover_go) { g_addr_focused = 0; go_to_address_bar(); *need_redraw = 1; return; }
    if (*hover_back) { go_back(); *need_redraw = 1; return; }
    if (*hover_fwd)  { go_forward(); *need_redraw = 1; return; }
    if (point_in(in.x, in.y, addr_x, addr_y, addr_w, addr_h)) {
        if (!g_addr_focused) { g_addr_focused = 1; *need_redraw = 1; }
        return;
    }
    if (g_addr_focused) { g_addr_focused = 0; *need_redraw = 1; }

    int step = view_h() - 40; if (step < 40) step = 40;
    if (*hover_up) { g_scroll_px -= step; clamp_scroll(); *need_redraw = 1; return; }
    if (*hover_dn) { g_scroll_px += step; clamp_scroll(); *need_redraw = 1; return; }

     
    if (g_page_loaded && point_in(in.x, in.y, content_x, content_y, content_w, view_h())) {
        int node = wc_hit(in.x - content_x, in.y - content_y + g_scroll_px);
        if (node >= 0) {
            int ran = wj_click(node);
            if (wj_dirty) relayout_dom_changed();
            else if (ran && wj_status[0]) { g_status[0] = 0; u_append(g_status, STATUS_MAX, wj_status); }
            int ln = wc_link_of(node);
            if (ln >= 0) {
                char href[MAX_URL];
                u32 hl = wc_nodes[ln].href.len; if (hl >= MAX_URL) hl = MAX_URL - 1;
                u_memcpy(href, WCS(wc_nodes[ln].href), hl); href[hl] = 0;
                if (href[0] && href[0] != '#' && u_strnicmp(href, "javascript:", 11) != 0 && u_strnicmp(href, "mailto:", 7) != 0) {
                    char target[MAX_URL];
                    resolve_url(g_cur_url, href, target, MAX_URL);
                    u_strcpy(g_addr_text, target); g_addr_len = (int)u_strlen(g_addr_text);
                    history_push(target);
                    navigate(target);
                }
            }
            *need_redraw = 1;
        }
    }
}

 


static int app_main(void) {
    if (sys_get_framebuffer(&g_fb) != 0 || g_fb.mode != 1  ) {
        static const char m[] = "Browser: no linear framebuffer available\n";
        sys_write(2, m, sizeof(m) - 1);
        return 1;
    }

     


    u32 win_w = 900, win_h = 620;
    if (win_w + 40 > g_fb.width)        win_w = g_fb.width > 240 ? g_fb.width - 40 : g_fb.width;
    if (win_h + 38 + 20 > g_fb.height)  win_h = g_fb.height > 240 ? g_fb.height - 38 - 20 : g_fb.height;
    g_win_id = sys_win_create("Browser", win_w, win_h);
    if (g_win_id < 0) {
        static const char m[] = "Browser: sys_win_create failed\n";
        sys_write(2, m, sizeof(m) - 1);
        return 1;
    }

    u32 last_redraw_gen = 0xFFFFFFFFu;
    int first = 1;

    for (;;) {
        sc_win_rect_t rect;
        i64 rr = sys_win_get_rect(g_win_id, &rect);
        if (rr != 0) break;                  

        int geom_changed = first || rect.x != win_x || rect.y != win_y ||
                           (int)rect.w != g_content_w || (int)rect.h != g_content_h;
        int redraw_gen_changed = rect.redraw_gen != last_redraw_gen;

        win_x = rect.x; win_y = rect.y;
        g_content_w = (int)rect.w; g_content_h = (int)rect.h;
        layout();

        if (geom_changed && g_page_loaded) { wc_prepare_and_layout(content_w); clamp_scroll(); }

        int need_redraw = first || geom_changed || redraw_gen_changed;
        last_redraw_gen = rect.redraw_gen;
        first = 0;

         


        if (handle_keyboard()) need_redraw = 1;
        int hb = 0, hf = 0, hu = 0, hd = 0;
        int mouse_redraw = 0;
        handle_mouse(&hb, &hf, &hu, &hd, &mouse_redraw);
        if (mouse_redraw) need_redraw = 1;

        static int last_hb = -1, last_hf = -1, last_hu = -1, last_hd = -1, last_hg = -1;
        if (hb != last_hb || hf != last_hf || hu != last_hu || hd != last_hd || g_hover_go != last_hg) need_redraw = 1;
        last_hb = hb; last_hf = hf; last_hu = hu; last_hd = hd; last_hg = g_hover_go;

        if (need_redraw) render(hb, hf, hu, hd);

        sys_sleep_ms(15);
    }

    sys_win_close(g_win_id);
    return 0;
}

void _start(void) {
    __asm__ volatile ("and $-16, %rsp");    
    int code = app_main();
    sys_exit(code);
}
