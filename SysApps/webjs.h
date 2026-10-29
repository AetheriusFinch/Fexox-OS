/* Fexox OS Project, EULA GPLv2 */
 


#ifndef WEBJS_H
#define WEBJS_H

#define JS_ARENA_SIZE   (96u * 1024u)
#define JS_MAX_OBJS     900
#define JS_MAX_PROPS    3600
#define JS_MAX_ELEMS    9000
#define JS_MAX_ARGS     8
#define JS_MAX_DEPTH    16
#define JS_MAX_STEPS    400000
#define JS_MAX_TIMERS   16

typedef struct { u8 t; i64 v; } jv;
enum { JT_UNDEF = 0, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ };
enum { JK_OBJ = 1, JK_ARR, JK_FUNC, JK_NATIVE, JK_ENV, JK_DOM, JK_STYLE, JK_DOC, JK_WIN, JK_MATH, JK_CONSOLE, JK_CLASSLIST, JK_DATASET };
typedef struct {
    u8 kind, flags;
    int props, env;
    u32 aux, aux2, cap;
    const char *pos, *end;
} jobj;
typedef struct { u32 key; jv val; int next; } jprop;

static char  js_arena[JS_ARENA_SIZE];
static u32   js_arena_len;
static jobj  js_objs[JS_MAX_OBJS];     static int js_obj_count;
static jprop js_props[JS_MAX_PROPS];   static int js_prop_count;
static jv    js_elems[JS_MAX_ELEMS];   static u32 js_elem_count;
static int   js_dom_cache[WC_MAX_NODES];
static int   js_click_fn[WC_MAX_NODES];      

 
enum { TK_EOF = 0, TK_NUM, TK_STR, TK_ID, TK_TPL, TK_P };
enum { OP_EQ = 256, OP_NE, OP_SEQ, OP_SNE, OP_LE, OP_GE, OP_AND, OP_OR, OP_INC, OP_DEC,
       OP_ADDEQ, OP_SUBEQ, OP_MULEQ, OP_DIVEQ, OP_MODEQ, OP_ARROW, OP_SHL, OP_SHR, OP_NULLISH };
typedef struct { int type, op; const char *s; u32 len; i64 num; } jtok;
static jtok        J_tok;
static const char *J_p, *J_end;
static int         J_env, J_genv, J_flow, J_depth, J_steps;
static jv          J_ret;
static char        J_errmsg[160];
static int         J_err;
static int         J_script_node = -1;        
static int         J_write_cursor = -1;
static int         J_this_node = -1;
static u32         J_rand_state = 2463534242u;
int wj_dirty;                                  
char wj_status[200];                           
char wj_err_text[200];                         
char wj_page_url[320];                         
int wj_script_errors, wj_scripts_run, wj_scripts_skipped;

static jv J_undef(void) { jv v; v.t = JT_UNDEF; v.v = 0; return v; }
static jv J_null(void)  { jv v; v.t = JT_NULL; v.v = 0; return v; }
static jv J_bool(int b) { jv v; v.t = JT_BOOL; v.v = b ? 1 : 0; return v; }
static jv J_num(i64 n)  { jv v; v.t = JT_NUM; v.v = n; return v; }
static jv J_objv(int o) { jv v; v.t = JT_OBJ; v.v = o; return v; }

static void J_error(const char *msg) {
    if (J_err) return;
    J_err = 1;
    u32 i = 0; while (msg[i] && i < sizeof(J_errmsg) - 1) { J_errmsg[i] = msg[i]; i++; } J_errmsg[i] = 0;
}

 
static u32 J_str_alloc(const char *s, u32 n) {
    if (js_arena_len + n + 6 > JS_ARENA_SIZE) { J_error("out of memory (strings)"); return 0; }
    u32 off = js_arena_len;
    js_arena[off] = (char)(n & 255); js_arena[off + 1] = (char)((n >> 8) & 255);
    js_arena[off + 2] = (char)((n >> 16) & 255); js_arena[off + 3] = (char)((n >> 24) & 255);
    for (u32 i = 0; i < n; i++) js_arena[off + 4 + i] = s[i];
    js_arena[off + 4 + n] = 0;
    js_arena_len += n + 5;
    return off;
}
static u32 J_slen_off(u32 off) {
    return (u32)(u8)js_arena[off] | ((u32)(u8)js_arena[off + 1] << 8) | ((u32)(u8)js_arena[off + 2] << 16) | ((u32)(u8)js_arena[off + 3] << 24);
}
#define JSP(x) (js_arena + (u32)(x).v + 4)
#define JSL(x) J_slen_off((u32)(x).v)
static jv J_mkstr(const char *s, u32 n) { jv v; v.t = JT_STR; v.v = (i64)J_str_alloc(s, n); return v; }
static jv J_mkcstr(const char *s) { return J_mkstr(s, wc_slen(s)); }

static int J_memeq(const char *a, const char *b, u32 n) { for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }

 
static int J_new(int kind) {
    if (js_obj_count >= JS_MAX_OBJS) { J_error("out of memory (objects)"); return 0; }
    int i = js_obj_count++;
    jobj *o = &js_objs[i];
    o->kind = (u8)kind; o->flags = 0; o->props = -1; o->env = -1; o->aux = o->aux2 = o->cap = 0; o->pos = o->end = NULL;
    return i;
}
static int J_prop_find(int obj, const char *k, u32 kl) {
    for (int p = js_objs[obj].props; p >= 0; p = js_props[p].next) {
        u32 off = js_props[p].key;
        if (J_slen_off(off) == kl && J_memeq(js_arena + off + 4, k, kl)) return p;
    }
    return -1;
}
static void J_prop_set(int obj, const char *k, u32 kl, jv val) {
    int p = J_prop_find(obj, k, kl);
    if (p >= 0) { js_props[p].val = val; return; }
    if (js_prop_count >= JS_MAX_PROPS) { J_error("out of memory (properties)"); return; }
    p = js_prop_count++;
    js_props[p].key = J_str_alloc(k, kl);
    js_props[p].val = val;
    js_props[p].next = js_objs[obj].props;
    js_objs[obj].props = p;
}
static void J_prop_setc(int obj, const char *k, jv val) { J_prop_set(obj, k, wc_slen(k), val); }

 
static void J_arr_push(int arr, jv v) {
    jobj *o = &js_objs[arr];
    if (o->aux2 >= o->cap) {
        u32 ncap = o->cap ? o->cap * 2 : 4;
        if (js_elem_count + ncap > JS_MAX_ELEMS) { J_error("out of memory (arrays)"); return; }
        u32 nstart = js_elem_count; js_elem_count += ncap;
        for (u32 i = 0; i < o->aux2; i++) js_elems[nstart + i] = js_elems[o->aux + i];
        o->aux = nstart; o->cap = ncap;
    }
    js_elems[o->aux + o->aux2++] = v;
}

 
static int J_truthy(jv v) {
    switch (v.t) { case JT_UNDEF: case JT_NULL: return 0; case JT_BOOL: case JT_NUM: return v.v != 0; case JT_STR: return JSL(v) > 0; default: return 1; }
}
static u32 J_i64_str(i64 n, char *out) {
    char t[24]; int tl = 0; int neg = n < 0; u64 u = neg ? (u64)(-(n + 1)) + 1 : (u64)n; u32 o = 0;
    if (u == 0) t[tl++] = '0';
    while (u > 0) { t[tl++] = (char)('0' + (int)(u % 10)); u /= 10; }
    if (neg) out[o++] = '-';
    while (tl > 0) out[o++] = t[--tl];
    return o;
}
static jv J_tostr(jv v);
static u32 J_arr_join(int arr, const char *sep, u32 sl, char *out, u32 cap);
static jv J_tostr(jv v) {
    char b[32];
    switch (v.t) {
    case JT_UNDEF: return J_mkcstr("undefined");
    case JT_NULL:  return J_mkcstr("null");
    case JT_BOOL:  return J_mkcstr(v.v ? "true" : "false");
    case JT_NUM:   { u32 n = J_i64_str(v.v, b); return J_mkstr(b, n); }
    case JT_STR:   return v;
    default: {
        jobj *o = &js_objs[v.v];
        if (o->kind == JK_ARR) { char tmp[1024]; u32 n = J_arr_join((int)v.v, ",", 1, tmp, sizeof(tmp)); return J_mkstr(tmp, n); }
        if (o->kind == JK_FUNC || o->kind == JK_NATIVE) return J_mkcstr("function");
        if (o->kind == JK_DOM) return J_mkcstr("[object HTMLElement]");
        return J_mkcstr("[object Object]");
    }
    }
}
static i64 J_tonum(jv v) {
    switch (v.t) {
    case JT_BOOL: case JT_NUM: return v.v;
    case JT_STR: {
        const char *s = JSP(v); u32 n = JSL(v), i = 0; i64 r = 0; int neg = 0;
        while (i < n && wc_isspace(s[i])) i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
        if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { i += 2; while (i < n && wc_hexv(s[i]) >= 0) { r = r * 16 + wc_hexv(s[i]); i++; } return neg ? -r : r; }
        while (i < n && wc_isdigit(s[i])) { r = r * 10 + (s[i] - '0'); i++; }
        return neg ? -r : r;
    }
    default: return 0;
    }
}
static u32 J_arr_join(int arr, const char *sep, u32 sl, char *out, u32 cap) {
    u32 o = 0; jobj *a = &js_objs[arr];
    for (u32 i = 0; i < a->aux2; i++) {
        if (i) for (u32 k = 0; k < sl && o < cap; k++) out[o++] = sep[k];
        jv e = js_elems[a->aux + i];
        if (e.t == JT_UNDEF || e.t == JT_NULL) continue;
        jv s = J_tostr(e);
        for (u32 k = 0; k < JSL(s) && o < cap; k++) out[o++] = JSP(s)[k];
    }
    return o;
}
static int J_strict_eq(jv a, jv b) {
    if (a.t != b.t) return 0;
    if (a.t == JT_STR) return JSL(a) == JSL(b) && J_memeq(JSP(a), JSP(b), JSL(a));
    if (a.t == JT_UNDEF || a.t == JT_NULL) return 1;
    return a.v == b.v;
}
static int J_loose_eq(jv a, jv b) {
    if (a.t == b.t) return J_strict_eq(a, b);
    if ((a.t == JT_UNDEF || a.t == JT_NULL) && (b.t == JT_UNDEF || b.t == JT_NULL)) return 1;
    if ((a.t == JT_NUM || a.t == JT_BOOL || a.t == JT_STR) && (b.t == JT_NUM || b.t == JT_BOOL || b.t == JT_STR)) return J_tonum(a) == J_tonum(b);
    return 0;
}

 
static void J_next(void) {
    const char *p = J_p;
    for (;;) {
        while (p < J_end && wc_isspace((u8)*p)) p++;
        if (p + 1 < J_end && p[0] == '/' && p[1] == '/') { while (p < J_end && *p != '\n') p++; continue; }
        if (p + 1 < J_end && p[0] == '/' && p[1] == '*') { p += 2; while (p + 1 < J_end && !(p[0] == '*' && p[1] == '/')) p++; p = (p + 1 < J_end) ? p + 2 : J_end; continue; }
        if (p + 3 < J_end && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') { while (p < J_end && *p != '\n') p++; continue; }
        break;
    }
    J_tok.s = p; J_tok.len = 0; J_tok.op = 0; J_tok.num = 0;
    if (p >= J_end) { J_tok.type = TK_EOF; J_p = p; return; }
    char c = *p;
    if (wc_isalpha(c) || c == '_' || c == '$') {
        const char *q = p;
        while (q < J_end && (wc_isalpha(*q) || wc_isdigit(*q) || *q == '_' || *q == '$')) q++;
        J_tok.type = TK_ID; J_tok.len = (u32)(q - p); J_p = q; return;
    }
    if (wc_isdigit(c) || (c == '.' && p + 1 < J_end && wc_isdigit(p[1]))) {
        const char *q = p; i64 n = 0;
        if (c == '0' && p + 1 < J_end && (p[1] == 'x' || p[1] == 'X')) { q += 2; while (q < J_end && wc_hexv(*q) >= 0) { n = n * 16 + wc_hexv(*q); q++; } }
        else {
            while (q < J_end && wc_isdigit(*q)) { n = n * 10 + (*q - '0'); q++; }
            if (q < J_end && *q == '.') { q++; while (q < J_end && wc_isdigit(*q)) q++; }           
            if (q < J_end && (*q == 'e' || *q == 'E') && q + 1 < J_end && (wc_isdigit(q[1]) || q[1] == '-' || q[1] == '+')) {
                q++; int en = 0, eneg = 0; if (*q == '-' || *q == '+') { eneg = *q == '-'; q++; }
                while (q < J_end && wc_isdigit(*q)) { en = en * 10 + (*q - '0'); q++; }
                if (!eneg) while (en-- > 0 && n < 100000000000000LL) n *= 10; else while (en-- > 0) n /= 10;
            }
        }
        J_tok.type = TK_NUM; J_tok.num = n; J_tok.len = (u32)(q - p); J_p = q; return;
    }
    if (c == '"' || c == '\'') {
        const char *q = p + 1;
        while (q < J_end && *q != c) { if (*q == '\\' && q + 1 < J_end) q++; q++; }
        J_tok.type = TK_STR; J_tok.s = p + 1; J_tok.len = (u32)(q - (p + 1)); J_p = (q < J_end) ? q + 1 : q; return;
    }
    if (c == '`') { J_tok.type = TK_TPL; J_p = p + 1; return; }
     
    J_tok.type = TK_P; J_tok.op = c; J_p = p + 1;
    char c1 = (p + 1 < J_end) ? p[1] : 0, c2 = (p + 2 < J_end) ? p[2] : 0;
    #define J_OP2(a, b, code) if (c == a && c1 == b) { J_tok.op = code; J_p = p + 2; return; }
    if (c == '=' && c1 == '=' && c2 == '=') { J_tok.op = OP_SEQ; J_p = p + 3; return; }
    if (c == '!' && c1 == '=' && c2 == '=') { J_tok.op = OP_SNE; J_p = p + 3; return; }
    J_OP2('=', '=', OP_EQ) J_OP2('!', '=', OP_NE) J_OP2('<', '=', OP_LE) J_OP2('>', '=', OP_GE)
    J_OP2('&', '&', OP_AND) J_OP2('|', '|', OP_OR) J_OP2('+', '+', OP_INC) J_OP2('-', '-', OP_DEC)
    J_OP2('+', '=', OP_ADDEQ) J_OP2('-', '=', OP_SUBEQ) J_OP2('*', '=', OP_MULEQ) J_OP2('/', '=', OP_DIVEQ)
    J_OP2('%', '=', OP_MODEQ) J_OP2('=', '>', OP_ARROW) J_OP2('<', '<', OP_SHL) J_OP2('>', '>', OP_SHR) J_OP2('?', '?', OP_NULLISH)
    #undef J_OP2
}
static int J_isp(int op) { return J_tok.type == TK_P && J_tok.op == op; }
static int J_isid(const char *kw) { return J_tok.type == TK_ID && J_tok.len == wc_slen(kw) && J_memeq(J_tok.s, kw, J_tok.len); }
static void J_expect(int op) {
    if (!J_isp(op)) { char m[48] = "SyntaxError: expected '"; u32 l = wc_slen(m); m[l++] = (char)(op < 256 ? op : '?'); m[l++] = '\''; m[l] = 0; J_error(m); return; }
    J_next();
}
typedef struct { const char *p; jtok tok; } jstate;
static jstate J_save(void) { jstate s; s.p = J_p; s.tok = J_tok; return s; }
static void J_restore(jstate s) { J_p = s.p; J_tok = s.tok; }

 
static jv  J_assign(int exec);
static void J_stmt(int exec);
static void J_block(int exec);
static jv  J_call_value(jv f, jv thisv, jv *args, int n);
static jv  J_get_member(jv obj, const char *k, u32 kl, int has_idx, i64 idx);
static void J_set_member(jv obj, const char *k, u32 kl, int has_idx, i64 idx, jv val);
static jv  J_call_method(jv recv, const char *name, u32 nl, jv *args, int n);

 
static int J_env_new(int parent) { int e = J_new(JK_ENV); js_objs[e].env = parent; return e; }
static int J_lookup(const char *nm, u32 nl, int *out_env) {
    for (int e = J_env; e >= 0; e = js_objs[e].env) {
        int p = J_prop_find(e, nm, nl);
        if (p >= 0) { if (out_env) *out_env = e; return p; }
    }
    return -1;
}

typedef struct {
    int valid, kind;                
    int prop; const char *name; u32 nlen;
    jv obj; const char *key; u32 klen; int has_idx; i64 idx;
} jref;

static jv J_get_var(const char *nm, u32 nl) {
    int p = J_lookup(nm, nl, NULL);
    if (p < 0) {
        static char m[80]; u32 o = 0; const char *pre = "ReferenceError: "; while (*pre) m[o++] = *pre++;
        for (u32 i = 0; i < nl && o < 60; i++) m[o++] = nm[i]; const char *suf = " is not defined"; while (*suf) m[o++] = *suf++; m[o] = 0;
        J_error(m); return J_undef();
    }
    return js_props[p].val;
}

static void J_assign_ref(jref *r, jv val) {
    if (!r->valid) { J_error("SyntaxError: invalid assignment target"); return; }
    if (r->kind == 1) {
        int p = J_lookup(r->name, r->nlen, NULL);
        if (p >= 0) js_props[p].val = val; else J_prop_set(J_genv, r->name, r->nlen, val);
    } else J_set_member(r->obj, r->key, r->klen, r->has_idx, r->idx, val);
}
static jv J_read_ref(jref *r) {
    if (r->kind == 1) return J_get_var(r->name, r->nlen);
    return J_get_member(r->obj, r->key, r->klen, r->has_idx, r->idx);
}

 
static jv J_decode_str(const char *s, u32 n) {
    static char buf[4096]; u32 o = 0;
    for (u32 i = 0; i < n && o < sizeof(buf) - 4; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            i++; char e = s[i];
            if (e == 'n') buf[o++] = '\n'; else if (e == 't') buf[o++] = '\t'; else if (e == 'r') buf[o++] = '\r';
            else if (e == 'x' && i + 2 < n) { int a = wc_hexv(s[i + 1]), b = wc_hexv(s[i + 2]); if (a >= 0 && b >= 0) { int cc = a * 16 + b; buf[o++] = (cc >= 32 && cc < 127) ? (char)cc : '?'; i += 2; } }
            else if (e == 'u' && i + 4 < n) { u32 cc = 0; for (int k = 1; k <= 4; k++) { int h = wc_hexv(s[i + k]); cc = cc * 16 + (u32)(h < 0 ? 0 : h); } buf[o++] = (cc >= 32 && cc < 127) ? (char)cc : '?'; i += 4; }
            else buf[o++] = e;
        } else buf[o++] = c;
    }
    return J_mkstr(buf, o);
}

 
static void J_skip_params(void) {
    int depth = 0;
    if (J_isp('(')) {
        do { if (J_isp('(')) depth++; else if (J_isp(')')) depth--; if (J_tok.type == TK_EOF) return; J_next(); } while (depth > 0);
    } else if (J_tok.type == TK_ID) J_next();
}
static jv J_make_func(const char *pos, int arrow) {
    int f = J_new(JK_FUNC);
    if (J_err) return J_undef();
    js_objs[f].pos = pos; js_objs[f].end = J_end; js_objs[f].env = J_env; js_objs[f].flags = (u8)(arrow ? 1 : 0);
    return J_objv(f);
}

static jv J_tpl(int exec) {
     
    char buf[1536]; u32 o = 0;        
    const char *q = J_p;
    while (q < J_end && *q != '`') {
        if (q[0] == '\\' && q + 1 < J_end) {
            q++; if (*q == 'n') buf[o++] = '\n'; else if (*q == 't') buf[o++] = '\t'; else buf[o++] = *q; q++; continue;
        }
        if (q[0] == '$' && q + 1 < J_end && q[1] == '{') {
            J_p = q + 2; J_next();
            jv v = J_assign(exec);
            if (J_err) return J_undef();
            if (!J_isp('}')) { J_error("SyntaxError: bad template"); return J_undef(); }
            q = J_tok.s + 1;
            if (exec) { jv s = J_tostr(v); for (u32 k = 0; k < JSL(s) && o < sizeof(buf) - 2; k++) buf[o++] = JSP(s)[k]; }
            continue;
        }
        if (o < sizeof(buf) - 2) buf[o++] = *q;
        q++;
    }
    J_p = (q < J_end) ? q + 1 : q;
    J_next();
    return exec ? J_mkstr(buf, o) : J_undef();
}

 
static int J_args(int exec, jv *args, int *n) {
    *n = 0;
    J_expect('(');
    while (!J_err && !J_isp(')') && J_tok.type != TK_EOF) {
        jv v = J_assign(exec);
        if (*n < JS_MAX_ARGS) args[(*n)++] = v;
        if (J_isp(',')) J_next(); else break;
    }
    J_expect(')');
    return 0;
}

static jv J_primary(int exec, jref *ref) {
    ref->valid = 0;
    if (J_err) return J_undef();
    switch (J_tok.type) {
    case TK_NUM: { jv v = J_num(J_tok.num); J_next(); return v; }
    case TK_STR: { jv v = exec ? J_decode_str(J_tok.s, J_tok.len) : J_undef(); J_next(); return v; }
    case TK_TPL: return J_tpl(exec);
    case TK_ID: {
        const char *nm = J_tok.s; u32 nl = J_tok.len;
        if (J_isid("true")) { J_next(); return J_bool(1); }
        if (J_isid("false")) { J_next(); return J_bool(0); }
        if (J_isid("null")) { J_next(); return J_null(); }
        if (J_isid("undefined")) { J_next(); return J_undef(); }
        if (J_isid("function")) {
            J_next();
            if (J_tok.type == TK_ID) J_next();               
            const char *pos = J_tok.s;
            J_skip_params();
            jv f = exec ? J_make_func(pos, 0) : J_undef();
            if (J_isp('{')) J_block(0);
            return f;
        }
         
        { jstate st = J_save(); J_next();
          if (J_isp(OP_ARROW)) {
              J_restore(st);
              const char *pos = J_tok.s; J_next();             
              J_next();                                       
              jv f = exec ? J_make_func(pos, 1) : J_undef();
              if (J_isp('{')) J_block(0); else J_assign(0);
              return f;
          }
          J_restore(st); }
        J_next();
        ref->valid = 1; ref->kind = 1; ref->name = nm; ref->nlen = nl;
        if (!exec) return J_undef();
        return J_get_var(nm, nl);
    }
    case TK_P:
        if (J_isp('(')) {
             
            jstate st = J_save();
            const char *pos = J_tok.s;
            J_skip_params();
            if (J_isp(OP_ARROW)) {
                J_next();
                jv f = exec ? J_make_func(pos, 1) : J_undef();
                if (J_isp('{')) J_block(0); else J_assign(0);
                return f;
            }
            J_restore(st);
            J_next();
            jv v = J_assign(exec);
            J_expect(')');
            return v;
        }
        if (J_isp('[')) {
            J_next();
            int arr = exec ? J_new(JK_ARR) : 0;
            while (!J_err && !J_isp(']') && J_tok.type != TK_EOF) {
                jv v = J_assign(exec);
                if (exec) J_arr_push(arr, v);
                if (J_isp(',')) J_next(); else break;
            }
            J_expect(']');
            return exec ? J_objv(arr) : J_undef();
        }
        if (J_isp('{')) {
            J_next();
            int o = exec ? J_new(JK_OBJ) : 0;
            while (!J_err && !J_isp('}') && J_tok.type != TK_EOF) {
                const char *k = J_tok.s; u32 kl = J_tok.len; int isstr = (J_tok.type == TK_STR);
                if (J_tok.type != TK_ID && J_tok.type != TK_STR && J_tok.type != TK_NUM) { J_error("SyntaxError: bad object literal"); break; }
                jv kv = J_undef();
                if (isstr) kv = J_decode_str(k, kl);
                J_next();
                jv val;
                if (J_isp(':')) { J_next(); val = J_assign(exec); }
                else { val = exec ? J_get_var(k, kl) : J_undef(); }           
                if (exec) { if (isstr) J_prop_set(o, JSP(kv), JSL(kv), val); else J_prop_set(o, k, kl, val); }
                if (J_isp(',')) J_next(); else break;
            }
            J_expect('}');
            return exec ? J_objv(o) : J_undef();
        }
        break;
    default: break;
    }
    J_error("SyntaxError: unexpected token");
    return J_undef();
}

static jv J_postfix(int exec, jref *ref) {
    jv v = J_primary(exec, ref);
    while (!J_err) {
        if (J_isp('.')) {
            J_next();
            if (J_tok.type != TK_ID) { J_error("SyntaxError: expected property name"); break; }
            const char *k = J_tok.s; u32 kl = J_tok.len; J_next();
            if (J_isp('(')) {
                jv args[JS_MAX_ARGS]; int n;
                J_args(exec, args, &n);
                if (exec && !J_err) v = J_call_method(v, k, kl, args, n); else v = J_undef();
                ref->valid = 0;
            } else {
                jv obj = v;
                ref->valid = 1; ref->kind = 2; ref->obj = obj; ref->key = k; ref->klen = kl; ref->has_idx = 0;
                v = exec ? J_get_member(obj, k, kl, 0, 0) : J_undef();
            }
        } else if (J_isp('[')) {
            J_next();
            jv iv = J_assign(exec);
            J_expect(']');
            jv obj = v;
            ref->valid = 1; ref->kind = 2; ref->obj = obj;
            if (exec && !J_err) {
                if (iv.t == JT_NUM) { ref->has_idx = 1; ref->idx = iv.v; ref->key = ""; ref->klen = 0; }
                else { jv s = J_tostr(iv); ref->has_idx = 0; ref->key = JSP(s); ref->klen = JSL(s); }
                v = J_get_member(obj, ref->key, ref->klen, ref->has_idx, ref->idx);
            } else v = J_undef();
        } else if (J_isp('(')) {
            jv args[JS_MAX_ARGS]; int n;
            J_args(exec, args, &n);
            if (exec && !J_err) v = J_call_value(v, J_undef(), args, n); else v = J_undef();
            ref->valid = 0;
        } else break;
    }
    if (!J_err && (J_isp(OP_INC) || J_isp(OP_DEC))) {
        int inc = J_isp(OP_INC);
        J_next();
        if (exec && ref->valid) { jv old = v; J_assign_ref(ref, J_num(J_tonum(old) + (inc ? 1 : -1))); v = J_num(J_tonum(old)); }
        ref->valid = 0;
    }
    return v;
}

static jv J_unary(int exec, jref *ref) {
    if (J_err) return J_undef();
    if (J_tok.type == TK_P) {
        if (J_isp('!')) { J_next(); jv v = J_unary(exec, ref); ref->valid = 0; return J_bool(!J_truthy(v)); }
        if (J_isp('-')) { J_next(); jv v = J_unary(exec, ref); ref->valid = 0; return J_num(-J_tonum(v)); }
        if (J_isp('+')) { J_next(); jv v = J_unary(exec, ref); ref->valid = 0; return J_num(J_tonum(v)); }
        if (J_isp('~')) { J_next(); jv v = J_unary(exec, ref); ref->valid = 0; return J_num(~J_tonum(v)); }
        if (J_isp(OP_INC) || J_isp(OP_DEC)) {
            int inc = J_isp(OP_INC); J_next();
            jv v = J_unary(exec, ref);
            if (exec && ref->valid) { v = J_num(J_tonum(v) + (inc ? 1 : -1)); J_assign_ref(ref, v); }
            ref->valid = 0; return v;
        }
    } else if (J_tok.type == TK_ID) {
        if (J_isid("typeof")) {
            J_next();
             
            jv v;
            if (J_tok.type == TK_ID && exec && J_lookup(J_tok.s, J_tok.len, NULL) < 0 && !J_isid("true") && !J_isid("false") && !J_isid("null") && !J_isid("undefined") && !J_isid("function")) {
                J_next(); v = J_undef();
            } else v = J_unary(exec, ref);
            ref->valid = 0;
            const char *t = "undefined";
            switch (v.t) {
            case JT_NULL: t = "object"; break; case JT_BOOL: t = "boolean"; break; case JT_NUM: t = "number"; break; case JT_STR: t = "string"; break;
            case JT_OBJ: t = (js_objs[v.v].kind == JK_FUNC || js_objs[v.v].kind == JK_NATIVE) ? "function" : "object"; break;
            default: break;
            }
            return exec ? J_mkcstr(t) : J_undef();
        }
        if (J_isid("void")) { J_next(); J_unary(exec, ref); ref->valid = 0; return J_undef(); }
    }
    return J_postfix(exec, ref);
}

static int J_binprec(void) {
    if (J_tok.type != TK_P) return 0;
    switch (J_tok.op) {
    case OP_OR: case OP_NULLISH: return 1; case OP_AND: return 2; case '|': return 3; case '^': return 4; case '&': return 5;
    case OP_EQ: case OP_NE: case OP_SEQ: case OP_SNE: return 6;
    case '<': case '>': case OP_LE: case OP_GE: return 7;
    case OP_SHL: case OP_SHR: return 8;
    case '+': case '-': return 9;
    case '*': case '/': case '%': return 10;
    default: return 0;
    }
}
static jv J_binary(int minp, int exec, jref *ref) {
    jv l = J_unary(exec, ref);
    for (;;) {
        int pr = J_binprec();
        if (pr == 0 || pr < minp || J_err) break;
        int op = J_tok.op;
        J_next();
        ref->valid = 0;
        jref dummy;
        if (op == OP_AND || op == OP_OR || op == OP_NULLISH) {
            int need = (op == OP_AND) ? J_truthy(l) : (op == OP_OR ? !J_truthy(l) : (l.t == JT_UNDEF || l.t == JT_NULL));
            jv r = J_binary(pr + 1, exec && need, &dummy);
            if (need) l = r;
            continue;
        }
        jv r = J_binary(pr + 1, exec, &dummy);
        if (!exec || J_err) continue;
        switch (op) {
        case '+':
            if (l.t == JT_STR || r.t == JT_STR || l.t == JT_OBJ || r.t == JT_OBJ) {
                jv a = J_tostr(l), b = J_tostr(r);
                u32 al = JSL(a), bl = JSL(b);
                static char buf[8192];
                if (al + bl >= sizeof(buf)) { J_error("string too long"); break; }
                for (u32 i = 0; i < al; i++) buf[i] = JSP(a)[i];
                for (u32 i = 0; i < bl; i++) buf[al + i] = JSP(b)[i];
                l = J_mkstr(buf, al + bl);
            } else l = J_num(J_tonum(l) + J_tonum(r));
            break;
        case '-': l = J_num(J_tonum(l) - J_tonum(r)); break;
        case '*': l = J_num(J_tonum(l) * J_tonum(r)); break;
        case '/': { i64 d = J_tonum(r); l = J_num(d ? J_tonum(l) / d : 0); break; }
        case '%': { i64 d = J_tonum(r); l = J_num(d ? J_tonum(l) % d : 0); break; }
        case '&': l = J_num(J_tonum(l) & J_tonum(r)); break;
        case '|': l = J_num(J_tonum(l) | J_tonum(r)); break;
        case '^': l = J_num(J_tonum(l) ^ J_tonum(r)); break;
        case OP_SHL: l = J_num(J_tonum(l) << (J_tonum(r) & 31)); break;
        case OP_SHR: l = J_num(J_tonum(l) >> (J_tonum(r) & 31)); break;
        case OP_EQ: l = J_bool(J_loose_eq(l, r)); break;
        case OP_NE: l = J_bool(!J_loose_eq(l, r)); break;
        case OP_SEQ: l = J_bool(J_strict_eq(l, r)); break;
        case OP_SNE: l = J_bool(!J_strict_eq(l, r)); break;
        case '<': case '>': case OP_LE: case OP_GE: {
            int cmp;
            if (l.t == JT_STR && r.t == JT_STR) {
                u32 a = JSL(l), b = JSL(r), m = a < b ? a : b; cmp = 0;
                for (u32 i = 0; i < m && !cmp; i++) cmp = (u8)JSP(l)[i] - (u8)JSP(r)[i];
                if (!cmp) cmp = (a > b) - (a < b);
            } else { i64 a = J_tonum(l), b = J_tonum(r); cmp = (a > b) - (a < b); }
            l = J_bool(op == '<' ? cmp < 0 : op == '>' ? cmp > 0 : op == OP_LE ? cmp <= 0 : cmp >= 0);
            break; }
        default: break;
        }
    }
    return l;
}

static jv J_ternary(int exec, jref *ref) {
    jv c = J_binary(1, exec, ref);
    if (J_isp('?') && !J_err) {
        J_next(); ref->valid = 0;
        int t = exec && J_truthy(c);
        jref d; jv a = J_assign(exec && t); (void)d;
        J_expect(':');
        jv b = J_assign(exec && !t && exec);
        return t ? a : b;
    }
    return c;
}

static jv J_assign(int exec) {
    jref ref;
    ref.valid = 0;
    jv v = J_ternary(exec, &ref);
    if (J_err) return v;
    if (J_tok.type == TK_P && ref.valid) {
        int op = J_tok.op;
        if (op == '=' || op == OP_ADDEQ || op == OP_SUBEQ || op == OP_MULEQ || op == OP_DIVEQ || op == OP_MODEQ) {
            jref saved = ref;
            J_next();
            jv rhs = J_assign(exec);
            if (!exec || J_err) return J_undef();
            jv res = rhs;
            if (op != '=') {
                jv cur = J_read_ref(&saved);
                if (op == OP_ADDEQ) {
                    if (cur.t == JT_STR || rhs.t == JT_STR) {
                        jv a = J_tostr(cur), b = J_tostr(rhs); static char buf[8192];
                        u32 al = JSL(a), bl = JSL(b);
                        if (al + bl >= sizeof(buf)) { J_error("string too long"); return J_undef(); }
                        for (u32 i = 0; i < al; i++) buf[i] = JSP(a)[i];
                        for (u32 i = 0; i < bl; i++) buf[al + i] = JSP(b)[i];
                        res = J_mkstr(buf, al + bl);
                    } else res = J_num(J_tonum(cur) + J_tonum(rhs));
                } else if (op == OP_SUBEQ) res = J_num(J_tonum(cur) - J_tonum(rhs));
                else if (op == OP_MULEQ) res = J_num(J_tonum(cur) * J_tonum(rhs));
                else if (op == OP_DIVEQ) { i64 d = J_tonum(rhs); res = J_num(d ? J_tonum(cur) / d : 0); }
                else { i64 d = J_tonum(rhs); res = J_num(d ? J_tonum(cur) % d : 0); }
            }
            J_assign_ref(&saved, res);
            return res;
        }
    }
    return v;
}
static jv J_expr(int exec) {
    jv v = J_assign(exec);
    while (J_isp(',') && !J_err) { J_next(); v = J_assign(exec); }
    return v;
}

 
static void J_block(int exec) {
    J_expect('{');
    while (!J_err && !J_isp('}') && J_tok.type != TK_EOF) J_stmt(exec);
    J_expect('}');
}
static void J_define(const char *nm, u32 nl, jv v) { J_prop_set(J_env, nm, nl, v); }

static void J_stmt(int exec) {
    if (J_err) return;
    if (++J_steps > JS_MAX_STEPS) { J_error("script is taking too long (infinite loop?)"); return; }
    int run = exec && J_flow == 0;

    if (J_isp(';')) { J_next(); return; }
    if (J_isp('{')) { J_block(exec); return; }
    if (J_tok.type == TK_ID) {
        if (J_isid("var") || J_isid("let") || J_isid("const")) {
            J_next();
            for (;;) {
                if (J_tok.type != TK_ID) { J_error("SyntaxError: variable name expected"); return; }
                const char *nm = J_tok.s; u32 nl = J_tok.len; J_next();
                jv v = J_undef();
                if (J_isp('=')) { J_next(); v = J_assign(run); }
                if (run && !J_err) J_define(nm, nl, v);
                if (J_isp(',')) { J_next(); continue; }
                break;
            }
            if (J_isp(';')) J_next();
            return;
        }
        if (J_isid("function")) {
            J_next();
            if (J_tok.type != TK_ID) { J_error("SyntaxError: function name expected"); return; }
            const char *nm = J_tok.s; u32 nl = J_tok.len; J_next();
            const char *pos = J_tok.s;
            J_skip_params();
            jv f = run ? J_make_func(pos, 0) : J_undef();
            if (J_isp('{')) J_block(0); else J_error("SyntaxError: function body expected");
            if (run && !J_err) J_define(nm, nl, f);
            return;
        }
        if (J_isid("if")) {
            J_next(); J_expect('(');
            jv c = J_expr(run); J_expect(')');
            int t = run && J_truthy(c);
            J_stmt(t);
            if (J_isid("else")) { J_next(); J_stmt(run && !t); }
            return;
        }
        if (J_isid("while")) {
            J_next(); J_expect('(');
            jstate cond = J_save();
            for (;;) {
                J_restore(cond);
                jv c = J_expr(run); J_expect(')');
                int t = run && J_truthy(c) && !J_err;
                J_stmt(t);
                if (J_err) return;
                if (!t) break;
                if (J_flow == 1) { J_flow = 0; break; }
                if (J_flow == 2) J_flow = 0;
                if (J_flow == 3) break;
            }
            return;
        }
        if (J_isid("for")) {
            J_next(); J_expect('(');
             
            if (J_isid("var") || J_isid("let") || J_isid("const")) {
                J_next();
                if (J_tok.type == TK_ID) {
                    const char *nm = J_tok.s; u32 nl = J_tok.len; J_next();
                    if (J_isid("of")) {                           
                        J_next();
                        jv arr = J_assign(run); J_expect(')');
                        jstate body = J_save();
                        if (run && arr.t == JT_OBJ && js_objs[arr.v].kind == JK_ARR) {
                            for (u32 i = 0; i < js_objs[arr.v].aux2 && !J_err; i++) {
                                J_restore(body);
                                J_define(nm, nl, js_elems[js_objs[arr.v].aux + i]);
                                J_stmt(1);
                                if (J_flow == 1) { J_flow = 0; break; }
                                if (J_flow == 2) J_flow = 0;
                                if (J_flow == 3) break;
                            }
                            if (J_flow == 0 && !J_err) { J_restore(body); J_stmt(0); }
                            else if (!J_err) { J_restore(body); int fl = J_flow; J_flow = 0; J_stmt(0); J_flow = fl; }
                        } else { J_restore(body); J_stmt(0); }
                        return;
                    }
                    jv v = J_undef();
                    if (J_isp('=')) { J_next(); v = J_assign(run); }
                    if (run && !J_err) J_define(nm, nl, v);
                    while (J_isp(',') && !J_err) {
                        J_next();
                        if (J_tok.type != TK_ID) break;
                        const char *n2 = J_tok.s; u32 l2 = J_tok.len; J_next();
                        jv v2 = J_undef(); if (J_isp('=')) { J_next(); v2 = J_assign(run); }
                        if (run && !J_err) J_define(n2, l2, v2);
                    }
                }
            } else if (!J_isp(';')) J_expr(run);
            J_expect(';');
            jstate cond = J_save();
            int has_cond = !J_isp(';');
            if (has_cond) J_expr(0);
            J_expect(';');
            jstate incr = J_save();
            if (!J_isp(')')) J_expr(0);
            J_expect(')');
            jstate body = J_save();
            for (;;) {
                J_restore(cond);
                int t = run;
                if (has_cond) { jv c = J_expr(run); t = run && J_truthy(c) && !J_err; }
                J_restore(body);
                J_stmt(t);
                if (J_err) return;
                if (!t) break;
                if (J_flow == 1) { J_flow = 0; break; }
                if (J_flow == 3) break;
                if (J_flow == 2) J_flow = 0;
                jstate after = J_save();
                J_restore(incr);
                if (!J_isp(')')) J_expr(1);
                (void)after;
            }
            return;
        }
        if (J_isid("return")) {
            J_next();
            jv v = J_undef();
            if (!J_isp(';') && !J_isp('}') && J_tok.type != TK_EOF) v = J_expr(run);
            if (J_isp(';')) J_next();
            if (run) { J_ret = v; J_flow = 3; }
            return;
        }
        if (J_isid("break")) { J_next(); if (J_isp(';')) J_next(); if (run) J_flow = 1; return; }
        if (J_isid("continue")) { J_next(); if (J_isp(';')) J_next(); if (run) J_flow = 2; return; }
        if (J_isid("throw")) {
            J_next(); jv v = J_expr(run); if (J_isp(';')) J_next();
            if (run && !J_err) { jv s = J_tostr(v); static char m[160]; u32 n = JSL(s) > 150 ? 150 : JSL(s); for (u32 i = 0; i < n; i++) m[i] = JSP(s)[i]; m[n] = 0; J_error(m); }
            return;
        }
        if (J_isid("try")) {
            J_next();
            jstate tstart = J_save();
            J_block(run);
            int had_err = 0; char emsg[160]; emsg[0] = 0;
            if (J_err && run) {
                had_err = 1; for (u32 i = 0; i < sizeof(emsg); i++) { emsg[i] = J_errmsg[i]; if (!J_errmsg[i]) break; }
                J_err = 0; J_errmsg[0] = 0;
                J_restore(tstart); J_block(0);                 
            }
            if (J_isid("catch")) {
                J_next();
                const char *en = NULL; u32 el = 0;
                if (J_isp('(')) { J_next(); if (J_tok.type == TK_ID) { en = J_tok.s; el = J_tok.len; J_next(); } J_expect(')'); }
                if (had_err && en) J_define(en, el, J_mkcstr(emsg));
                J_block(had_err);
            }
            if (J_isid("finally")) { J_next(); int fl = J_flow; jv rv = J_ret; J_flow = 0; J_block(run); if (J_flow == 0) { J_flow = fl; J_ret = rv; } }
            return;
        }
    }
    jv v = J_expr(run); (void)v;
    if (J_isp(';')) J_next();
}

 
static jv J_call_func(jv f, jv thisv, jv *args, int n) {
    if (J_depth >= JS_MAX_DEPTH) { J_error("RangeError: too much recursion"); return J_undef(); }
    jobj *fo = &js_objs[f.v];
    jstate saved = J_save(); const char *sendp = J_end; int senv = J_env;
    J_depth++;
    J_end = fo->end; J_p = fo->pos;
    int env = J_env_new(fo->env);
    J_env = env;
    J_prop_setc(env, "this", thisv);
    J_next();
     
    int ai = 0;
    if (J_isp('(')) {
        J_next();
        while (!J_err && J_tok.type == TK_ID) {
            J_prop_set(env, J_tok.s, J_tok.len, ai < n ? args[ai] : J_undef()); ai++;
            J_next();
            if (J_isp('=')) { J_next(); J_assign(0); }         
            if (J_isp(',')) J_next(); else break;
        }
        J_expect(')');
    } else if (J_tok.type == TK_ID) {
        J_prop_set(env, J_tok.s, J_tok.len, n > 0 ? args[0] : J_undef());
        J_next();
    }
    jv result = J_undef();
    int saved_flow = J_flow; J_flow = 0;
    if (fo->flags & 1) {                                        
        if (J_isp(OP_ARROW)) J_next();
        if (J_isp('{')) { J_block(1); if (J_flow == 3) result = J_ret; }
        else result = J_assign(1);
    } else {
        if (J_isp('{')) { J_block(1); if (J_flow == 3) result = J_ret; }
    }
    J_flow = saved_flow;
    J_depth--;
    J_env = senv; J_end = sendp; J_restore(saved);
    return result;
}
static jv J_native(int id, jv *args, int n);
static jv J_call_value(jv f, jv thisv, jv *args, int n) {
    if (f.t == JT_OBJ) {
        jobj *o = &js_objs[f.v];
        if (o->kind == JK_FUNC) return J_call_func(f, thisv, args, n);
        if (o->kind == JK_NATIVE) return J_native((int)o->aux, args, n);
    }
    J_error("TypeError: value is not a function");
    return J_undef();
}

 
static jv J_dom_wrap(int node) {
    if (node < 0) return J_null();
    if (js_dom_cache[node]) return J_objv(js_dom_cache[node] - 1);
    int o = J_new(JK_DOM);
    if (J_err) return J_null();
    js_objs[o].aux = (u32)node;
    js_dom_cache[node] = o + 1;
    return J_objv(o);
}
static jv J_style_wrap(int node) {
    int o = J_new(JK_STYLE);
    js_objs[o].aux = (u32)node;
    return J_objv(o);
}
static void J_record_error(int visible);
static void J_status_set(const char *s, u32 n) {
    if (n > sizeof(wj_status) - 1) n = sizeof(wj_status) - 1;
    for (u32 i = 0; i < n; i++) wj_status[i] = s[i];
    wj_status[n] = 0;
}
static void J_record_error(int visible) {
    const char *pre = "Script error: "; u32 o = 0;
    while (pre[o] && o < sizeof(wj_err_text) - 1) { wj_err_text[o] = pre[o]; o++; }
    for (u32 i = 0; J_errmsg[i] && o < sizeof(wj_err_text) - 1; i++) wj_err_text[o++] = J_errmsg[i];
    wj_err_text[o] = 0;
    wj_script_errors++;
    if (visible) J_status_set(wj_err_text, o);
}
 
static u32 J_kebab(const char *k, u32 kl, char *out) {
    u32 o = 0;
    for (u32 i = 0; i < kl && o < 60; i++) {
        if (k[i] >= 'A' && k[i] <= 'Z') { out[o++] = '-'; out[o++] = (char)(k[i] + 32); }
        else out[o++] = k[i];
    }
    return o;
}
static void J_write_html(const char *html, u32 n) {
    int tmp = wc_new_node(1, TG_UNKNOWN, 0);
    if (tmp < 0) return;
    int saved = wc_open_n;
    wc_parse_fragment(tmp, (const u8 *)html, n);
    wc_open_n = saved;
    int cur = J_write_cursor;
    if (cur < 0) {                                               
        int body = wc_find_tag_node(TG_BODY);
        if (body < 0) body = 0;
        for (int c = wc_nodes[tmp].first; c >= 0;) { int nx = wc_nodes[c].next; wc_append_child(body, c); c = nx; }
    } else {
        for (int c = wc_nodes[tmp].first; c >= 0;) { int nx = wc_nodes[c].next; wc_insert_after(cur, c); cur = c; c = nx; }
        J_write_cursor = cur;
    }
    wj_dirty = 1;
}

static jv J_get_member(jv obj, const char *k, u32 kl, int has_idx, i64 idx) {
    if (obj.t == JT_STR) {
        if (!has_idx && kl == 6 && J_memeq(k, "length", 6)) return J_num(JSL(obj));
        if (has_idx && idx >= 0 && (u32)idx < JSL(obj)) return J_mkstr(JSP(obj) + idx, 1);
        return J_undef();
    }
    if (obj.t != JT_OBJ) {
        if (obj.t == JT_UNDEF || obj.t == JT_NULL) J_error("TypeError: cannot read property of undefined/null");
        return J_undef();
    }
    jobj *o = &js_objs[obj.v];
    switch (o->kind) {
    case JK_ARR:
        if (has_idx) return (idx >= 0 && (u32)idx < o->aux2) ? js_elems[o->aux + idx] : J_undef();
        if (kl == 6 && J_memeq(k, "length", 6)) return J_num(o->aux2);
        break;
    case JK_WIN: { int p = J_prop_find(J_genv, k, kl); return p >= 0 ? js_props[p].val : J_undef(); }
    case JK_MATH:
        if (kl == 2 && J_memeq(k, "PI", 2)) return J_num(3);
        if (kl == 1 && J_memeq(k, "E", 1)) return J_num(2);
        break;
    case JK_DOC:
        if (kl == 4 && J_memeq(k, "body", 4)) return J_dom_wrap(wc_find_tag_node(TG_BODY));
        if (kl == 15 && J_memeq(k, "documentElement", 15)) return J_dom_wrap(wc_find_tag_node(TG_HTML));
        if (kl == 5 && J_memeq(k, "title", 5)) return J_mkstr(WCS(wc_title), wc_title.len);
        if (kl == 6 && J_memeq(k, "cookie", 6)) return J_mkcstr("");
        if (kl == 8 && J_memeq(k, "referrer", 8)) return J_mkcstr("");
        if (kl == 10 && J_memeq(k, "readyState", 10)) return J_mkcstr("loading");       
        if (kl == 3 && J_memeq(k, "URL", 3)) return J_mkcstr(wj_page_url);
        if (kl == 8 && J_memeq(k, "location", 8)) { int p = J_prop_find(J_genv, "location", 8); return p >= 0 ? js_props[p].val : J_undef(); }
        break;
    case JK_DOM: {
        int node = (int)o->aux;
        if (kl == 9 && J_memeq(k, "innerHTML", 9)) { static char b[8192]; u32 n = wc_text_content(node, b, sizeof(b)); return J_mkstr(b, n); }
        if ((kl == 11 && J_memeq(k, "textContent", 11)) || (kl == 9 && J_memeq(k, "innerText", 9))) { static char b[8192]; u32 n = wc_text_content(node, b, sizeof(b)); return J_mkstr(b, n); }
        if (kl == 2 && J_memeq(k, "id", 2)) return J_mkstr(WCS(wc_nodes[node].id), wc_nodes[node].id.len);
        if (kl == 9 && J_memeq(k, "className", 9)) return J_mkstr(WCS(wc_nodes[node].cls), wc_nodes[node].cls.len);
        if (kl == 4 && J_memeq(k, "href", 4)) return J_mkstr(WCS(wc_nodes[node].href), wc_nodes[node].href.len);
        if (kl == 5 && J_memeq(k, "value", 5)) return J_mkstr(WCS(wc_nodes[node].aux), wc_nodes[node].aux.len);
        if (kl == 5 && J_memeq(k, "style", 5)) return J_style_wrap(node);
        if (kl == 9 && J_memeq(k, "classList", 9)) { int c = J_new(JK_CLASSLIST); js_objs[c].aux = (u32)node; return J_objv(c); }
        if (kl == 7 && J_memeq(k, "dataset", 7)) { int c = J_new(JK_DATASET); js_objs[c].aux = (u32)node; return J_objv(c); }
        if (kl == 7 && J_memeq(k, "tagName", 7)) return J_mkcstr("ELEMENT");
        if (kl == 10 && J_memeq(k, "parentNode", 10)) return J_dom_wrap(wc_nodes[node].parent > 0 ? wc_nodes[node].parent : -1);
        if (kl == 7 && J_memeq(k, "onclick", 7)) return js_click_fn[node] ? J_objv(js_click_fn[node] - 1) : J_null();
        break; }
    case JK_STYLE: return J_mkcstr("");
    default: break;
    }
    if (has_idx) { char b[24]; u32 n = J_i64_str(idx, b); int p = J_prop_find((int)obj.v, b, n); return p >= 0 ? js_props[p].val : J_undef(); }
    int p = J_prop_find((int)obj.v, k, kl);
    return p >= 0 ? js_props[p].val : J_undef();
}

static void J_set_member(jv obj, const char *k, u32 kl, int has_idx, i64 idx, jv val) {
    if (obj.t != JT_OBJ) { if (obj.t == JT_UNDEF || obj.t == JT_NULL) J_error("TypeError: cannot set property of undefined/null"); return; }
    jobj *o = &js_objs[obj.v];
    switch (o->kind) {
    case JK_ARR:
        if (has_idx) {
            if (idx < 0 || idx > 5000) return;
            while ((u32)idx >= o->aux2 && !J_err) J_arr_push((int)obj.v, J_undef());
            if (!J_err) js_elems[js_objs[obj.v].aux + idx] = val;
            return;
        }
        if (kl == 6 && J_memeq(k, "length", 6)) { i64 n = J_tonum(val); if (n >= 0 && (u32)n <= o->aux2) o->aux2 = (u32)n; return; }
        break;
    case JK_WIN: J_prop_set(J_genv, k, kl, val); return;
    case JK_DOC: if (kl == 5 && J_memeq(k, "title", 5)) { jv s = J_tostr(val); wc_title = wc_put(JSP(s), JSL(s)); } return;
    case JK_DOM: {
        int node = (int)o->aux;
        if (kl == 9 && J_memeq(k, "innerHTML", 9)) { jv s = J_tostr(val); wc_set_html(node, JSP(s), JSL(s)); wj_dirty = 1; return; }
        if ((kl == 11 && J_memeq(k, "textContent", 11)) || (kl == 9 && J_memeq(k, "innerText", 9))) { jv s = J_tostr(val); wc_set_text(node, JSP(s), JSL(s)); wj_dirty = 1; return; }
        if (kl == 2 && J_memeq(k, "id", 2)) { jv s = J_tostr(val); wc_nodes[node].id = wc_put(JSP(s), JSL(s)); wj_dirty = 1; return; }
        if (kl == 9 && J_memeq(k, "className", 9)) { jv s = J_tostr(val); wc_nodes[node].cls = wc_put(JSP(s), JSL(s)); wj_dirty = 1; return; }
        if (kl == 4 && J_memeq(k, "href", 4)) { jv s = J_tostr(val); wc_nodes[node].href = wc_put(JSP(s), JSL(s)); wj_dirty = 1; return; }
        if (kl == 5 && J_memeq(k, "value", 5)) { jv s = J_tostr(val); wc_nodes[node].aux = wc_put(JSP(s), JSL(s)); wj_dirty = 1; return; }
        if (kl == 7 && J_memeq(k, "onclick", 7)) { if (val.t == JT_OBJ) js_click_fn[node] = (int)val.v + 1; else js_click_fn[node] = 0; return; }
        return; }
    case JK_STYLE: {
        char kb[64]; u32 kbl = J_kebab(k, kl, kb);
        jv s = J_tostr(val);
        wc_style_set((int)o->aux, kb, kbl, JSP(s), JSL(s));
        wj_dirty = 1;
        return; }
    default: break;
    }
    if (has_idx) { char b[24]; u32 n = J_i64_str(idx, b); J_prop_set((int)obj.v, b, n, val); }
    else J_prop_set((int)obj.v, k, kl, val);
}

 
enum { N_ALERT = 1, N_PARSEINT, N_STRING, N_NUMBER, N_BOOLEAN, N_ISNAN, N_SETTIMEOUT, N_NOOP, N_NULL, N_MATCHMEDIA };
typedef struct { jv fn; i64 ms; } jtimer;
static jtimer J_timers[JS_MAX_TIMERS]; static int J_ntimers;
static jv J_dom_loaded_fn[JS_MAX_TIMERS]; static int J_ndom_loaded;

static jv J_native_val(int id) { int o = J_new(JK_NATIVE); js_objs[o].aux = (u32)id; return J_objv(o); }
static jv J_native(int id, jv *args, int n) {
    jv a0 = n > 0 ? args[0] : J_undef();
    switch (id) {
    case N_NULL: return J_null();
    case N_MATCHMEDIA: {                        
        int o = J_new(JK_OBJ); jv nf = J_native_val(N_NOOP);
        J_prop_setc(o, "matches", J_bool(0)); J_prop_setc(o, "media", J_tostr(a0));
        J_prop_setc(o, "addEventListener", nf); J_prop_setc(o, "removeEventListener", nf);
        J_prop_setc(o, "addListener", nf); J_prop_setc(o, "removeListener", nf);
        return J_objv(o); }
    case N_ALERT: { jv s = J_tostr(a0); J_status_set(JSP(s), JSL(s)); return J_undef(); }
    case N_PARSEINT: { if (a0.t == JT_NUM) return a0; jv s = J_tostr(a0); return J_num(J_tonum(s)); }
    case N_STRING: return J_tostr(a0);
    case N_NUMBER: return J_num(J_tonum(a0));
    case N_BOOLEAN: return J_bool(J_truthy(a0));
    case N_ISNAN: return J_bool(0);
    case N_SETTIMEOUT: {
        if (a0.t == JT_OBJ && J_ntimers < JS_MAX_TIMERS) { J_timers[J_ntimers].fn = a0; J_timers[J_ntimers].ms = n > 1 ? J_tonum(args[1]) : 0; J_ntimers++; }
        return J_num(J_ntimers);
    }
    default: return J_undef();
    }
}

static int J_is_name(const char *k, u32 kl, const char *lit) { return kl == wc_slen(lit) && J_memeq(k, lit, kl); }

static jv J_call_method(jv recv, const char *k, u32 kl, jv *args, int n) {
    jv a0 = n > 0 ? args[0] : J_undef(), a1 = n > 1 ? args[1] : J_undef();
     
    if (recv.t == JT_STR) {
        const char *s = JSP(recv); u32 sl = JSL(recv);
        if (J_is_name(k, kl, "charAt")) { i64 i = J_tonum(a0); return (i >= 0 && (u32)i < sl) ? J_mkstr(s + i, 1) : J_mkcstr(""); }
        if (J_is_name(k, kl, "charCodeAt")) { i64 i = J_tonum(a0); return (i >= 0 && (u32)i < sl) ? J_num((u8)s[i]) : J_num(0); }
        if (J_is_name(k, kl, "indexOf") || J_is_name(k, kl, "includes")) {
            jv t = J_tostr(a0); u32 tl = JSL(t); i64 found = -1;
            for (u32 i = 0; i + tl <= sl; i++) if (J_memeq(s + i, JSP(t), tl)) { found = i; break; }
            return J_is_name(k, kl, "indexOf") ? J_num(found) : J_bool(found >= 0);
        }
        if (J_is_name(k, kl, "startsWith")) { jv t = J_tostr(a0); return J_bool(JSL(t) <= sl && J_memeq(s, JSP(t), JSL(t))); }
        if (J_is_name(k, kl, "endsWith")) { jv t = J_tostr(a0); return J_bool(JSL(t) <= sl && J_memeq(s + sl - JSL(t), JSP(t), JSL(t))); }
        if (J_is_name(k, kl, "substring") || J_is_name(k, kl, "slice") || J_is_name(k, kl, "substr")) {
            i64 b = J_tonum(a0), e = (n > 1 && a1.t != JT_UNDEF) ? J_tonum(a1) : (i64)sl;
            int is_slice = J_is_name(k, kl, "slice");
            if (J_is_name(k, kl, "substr")) e = (n > 1) ? b + J_tonum(a1) : (i64)sl;
            if (is_slice) { if (b < 0) b += sl; if (e < 0) e += sl; }
            if (b < 0) b = 0; if (e < 0) e = 0; if (b > (i64)sl) b = sl; if (e > (i64)sl) e = sl;
            if (!is_slice && b > e) { i64 t = b; b = e; e = t; }
            if (e < b) e = b;
            return J_mkstr(s + b, (u32)(e - b));
        }
        if (J_is_name(k, kl, "toUpperCase") || J_is_name(k, kl, "toLowerCase")) {
            static char b[4096]; u32 m = sl > sizeof(b) ? sizeof(b) : sl; int up = J_is_name(k, kl, "toUpperCase");
            for (u32 i = 0; i < m; i++) { char c = s[i]; if (up && c >= 'a' && c <= 'z') c = (char)(c - 32); else if (!up && c >= 'A' && c <= 'Z') c = (char)(c + 32); b[i] = c; }
            return J_mkstr(b, m);
        }
        if (J_is_name(k, kl, "trim")) { u32 b = 0, e = sl; while (b < e && wc_isspace(s[b])) b++; while (e > b && wc_isspace(s[e - 1])) e--; return J_mkstr(s + b, e - b); }
        if (J_is_name(k, kl, "repeat")) { static char b[4096]; i64 c = J_tonum(a0); u32 o = 0; for (i64 r = 0; r < c && o + sl < sizeof(b); r++) for (u32 i = 0; i < sl; i++) b[o++] = s[i]; return J_mkstr(b, o); }
        if (J_is_name(k, kl, "replace")) {
            jv t = J_tostr(a0), r = J_tostr(a1); u32 tl = JSL(t); static char b[8192];
            for (u32 i = 0; i + tl <= sl && tl > 0; i++) if (J_memeq(s + i, JSP(t), tl)) {
                u32 o = 0; for (u32 q = 0; q < i && o < sizeof(b); q++) b[o++] = s[q];
                for (u32 q = 0; q < JSL(r) && o < sizeof(b); q++) b[o++] = JSP(r)[q];
                for (u32 q = i + tl; q < sl && o < sizeof(b); q++) b[o++] = s[q];
                return J_mkstr(b, o);
            }
            return recv;
        }
        if (J_is_name(k, kl, "split")) {
            int arr = J_new(JK_ARR); jv sep = J_tostr(a0); u32 sepl = JSL(sep); u32 st = 0;
            if (sepl == 0) { for (u32 i = 0; i < sl; i++) J_arr_push(arr, J_mkstr(s + i, 1)); return J_objv(arr); }
            for (u32 i = 0; i + sepl <= sl; ) { if (J_memeq(s + i, JSP(sep), sepl)) { J_arr_push(arr, J_mkstr(s + st, i - st)); i += sepl; st = i; } else i++; }
            J_arr_push(arr, J_mkstr(s + st, sl - st));
            return J_objv(arr);
        }
        if (J_is_name(k, kl, "toString") || J_is_name(k, kl, "valueOf")) return recv;
        J_error("TypeError: unknown string method");
        return J_undef();
    }
    if (recv.t == JT_NUM) { if (J_is_name(k, kl, "toString")) return J_tostr(recv); J_error("TypeError: unknown number method"); return J_undef(); }
    if (recv.t != JT_OBJ) { J_error("TypeError: cannot call method on undefined/null"); return J_undef(); }
    jobj *o = &js_objs[recv.v];
     
    if (o->kind == JK_ARR) {
        int ai = (int)recv.v;
        if (J_is_name(k, kl, "push")) { for (int i = 0; i < n; i++) J_arr_push(ai, args[i]); return J_num(js_objs[ai].aux2); }
        if (J_is_name(k, kl, "pop")) { if (o->aux2 == 0) return J_undef(); return js_elems[o->aux + --o->aux2]; }
        if (J_is_name(k, kl, "shift")) { if (o->aux2 == 0) return J_undef(); jv f = js_elems[o->aux]; for (u32 i = 1; i < o->aux2; i++) js_elems[o->aux + i - 1] = js_elems[o->aux + i]; o->aux2--; return f; }
        if (J_is_name(k, kl, "join")) { static char b[8192]; jv sp = n > 0 ? J_tostr(a0) : J_mkcstr(","); u32 l = J_arr_join(ai, JSP(sp), JSL(sp), b, sizeof(b)); return J_mkstr(b, l); }
        if (J_is_name(k, kl, "indexOf") || J_is_name(k, kl, "includes")) {
            i64 f = -1; for (u32 i = 0; i < o->aux2; i++) if (J_strict_eq(js_elems[o->aux + i], a0)) { f = i; break; }
            return J_is_name(k, kl, "indexOf") ? J_num(f) : J_bool(f >= 0);
        }
        if (J_is_name(k, kl, "slice")) {
            int na = J_new(JK_ARR); i64 b = J_tonum(a0), e = (n > 1 && a1.t != JT_UNDEF) ? J_tonum(a1) : (i64)o->aux2;
            if (b < 0) b += o->aux2; if (e < 0) e += o->aux2; if (b < 0) b = 0; if (e > (i64)o->aux2) e = o->aux2;
            for (i64 i = b; i < e; i++) J_arr_push(na, js_elems[js_objs[ai].aux + i]);
            return J_objv(na);
        }
        if (J_is_name(k, kl, "forEach") || J_is_name(k, kl, "map") || J_is_name(k, kl, "filter")) {
            int is_map = J_is_name(k, kl, "map"), is_filter = J_is_name(k, kl, "filter");
            int na = (is_map || is_filter) ? J_new(JK_ARR) : 0;
            u32 len = js_objs[ai].aux2;
            for (u32 i = 0; i < len && i < js_objs[ai].aux2 && !J_err; i++) {
                jv el = js_elems[js_objs[ai].aux + i];
                jv ca[2]; ca[0] = el; ca[1] = J_num(i);
                jv r = J_call_value(a0, J_undef(), ca, 2);
                if (is_map) J_arr_push(na, r);
                else if (is_filter && J_truthy(r)) J_arr_push(na, el);
            }
            return (is_map || is_filter) ? J_objv(na) : J_undef();
        }
        if (J_is_name(k, kl, "reverse")) { for (u32 i = 0; i < o->aux2 / 2; i++) { jv t = js_elems[o->aux + i]; js_elems[o->aux + i] = js_elems[o->aux + o->aux2 - 1 - i]; js_elems[o->aux + o->aux2 - 1 - i] = t; } return recv; }
        J_error("TypeError: unknown array method");
        return J_undef();
    }
     
    if (o->kind == JK_MATH) {
        i64 x = J_tonum(a0), y = J_tonum(a1);
        if (J_is_name(k, kl, "floor") || J_is_name(k, kl, "ceil") || J_is_name(k, kl, "round") || J_is_name(k, kl, "trunc")) return J_num(x);
        if (J_is_name(k, kl, "abs")) return J_num(x < 0 ? -x : x);
        if (J_is_name(k, kl, "sign")) return J_num(x > 0 ? 1 : x < 0 ? -1 : 0);
        if (J_is_name(k, kl, "max")) { i64 m = n ? J_tonum(args[0]) : 0; for (int i = 1; i < n; i++) if (J_tonum(args[i]) > m) m = J_tonum(args[i]); return J_num(m); }
        if (J_is_name(k, kl, "min")) { i64 m = n ? J_tonum(args[0]) : 0; for (int i = 1; i < n; i++) if (J_tonum(args[i]) < m) m = J_tonum(args[i]); return J_num(m); }
        if (J_is_name(k, kl, "pow")) { i64 r = 1; for (i64 i = 0; i < y && i < 62; i++) r *= x; return J_num(r); }
        if (J_is_name(k, kl, "sqrt")) { i64 r = 0; while ((r + 1) * (r + 1) <= x) r++; return J_num(r); }
        if (J_is_name(k, kl, "random")) { J_rand_state ^= J_rand_state << 13; J_rand_state ^= J_rand_state >> 17; J_rand_state ^= J_rand_state << 5; return J_num(J_rand_state % 2); }
        J_error("TypeError: unknown Math function"); return J_undef();
    }
    if (o->kind == JK_CONSOLE) {
        if (n > 0) { jv s = J_tostr(a0); J_status_set(JSP(s), JSL(s)); }
        return J_undef();
    }
    if (o->kind == JK_DOC) {
        if (J_is_name(k, kl, "write") || J_is_name(k, kl, "writeln")) {
            for (int i = 0; i < n; i++) { jv s = J_tostr(args[i]); J_write_html(JSP(s), JSL(s)); }
            return J_undef();
        }
        if (J_is_name(k, kl, "getElementById")) { jv s = J_tostr(a0); return J_dom_wrap(wc_get_by_id(JSP(s), JSL(s))); }
        if (J_is_name(k, kl, "querySelector")) { jv s = J_tostr(a0); return J_dom_wrap(wc_query_selector(JSP(s), JSL(s))); }
        if (J_is_name(k, kl, "addEventListener")) {
            jv t = J_tostr(a0);
            if (n > 1 && a1.t == JT_OBJ && J_ndom_loaded < JS_MAX_TIMERS &&
                ((JSL(t) == 16 && J_memeq(JSP(t), "DOMContentLoaded", 16)) || (JSL(t) == 4 && J_memeq(JSP(t), "load", 4))))
                J_dom_loaded_fn[J_ndom_loaded++] = a1;
            return J_undef();                                   
        }
        if (J_is_name(k, kl, "removeEventListener")) return J_undef();
        J_error("TypeError: unknown document method"); return J_undef();
    }
    if (o->kind == JK_DOM) {
        int node = (int)o->aux;
        if (J_is_name(k, kl, "addEventListener")) {
            jv t = J_tostr(a0);
            if (JSL(t) == 5 && J_memeq(JSP(t), "click", 5) && a1.t == JT_OBJ) js_click_fn[node] = (int)a1.v + 1;
            return J_undef();
        }
        if (J_is_name(k, kl, "setAttribute")) {
            jv nm = J_tostr(a0), v = J_tostr(a1);
            if (JSL(nm) == 2 && J_memeq(JSP(nm), "id", 2)) wc_nodes[node].id = wc_put(JSP(v), JSL(v));
            else if (JSL(nm) == 5 && J_memeq(JSP(nm), "class", 5)) wc_nodes[node].cls = wc_put(JSP(v), JSL(v));
            else if (JSL(nm) == 5 && J_memeq(JSP(nm), "style", 5)) wc_nodes[node].style = wc_put(JSP(v), JSL(v));
            else if (JSL(nm) == 4 && J_memeq(JSP(nm), "href", 4)) wc_nodes[node].href = wc_put(JSP(v), JSL(v));
            wj_dirty = 1; return J_undef();
        }
        if (J_is_name(k, kl, "getAttribute")) {
            jv nm = J_tostr(a0);
            if (JSL(nm) == 2 && J_memeq(JSP(nm), "id", 2)) return J_mkstr(WCS(wc_nodes[node].id), wc_nodes[node].id.len);
            if (JSL(nm) == 5 && J_memeq(JSP(nm), "class", 5)) return J_mkstr(WCS(wc_nodes[node].cls), wc_nodes[node].cls.len);
            if (JSL(nm) == 4 && J_memeq(JSP(nm), "href", 4)) return J_mkstr(WCS(wc_nodes[node].href), wc_nodes[node].href.len);
            return J_null();
        }
        J_error("TypeError: unknown element method"); return J_undef();
    }
    if (o->kind == JK_WIN) {
        if (J_is_name(k, kl, "addEventListener")) {
            jv t = J_tostr(a0);
            if (n > 1 && a1.t == JT_OBJ && J_ndom_loaded < JS_MAX_TIMERS &&
                ((JSL(t) == 16 && J_memeq(JSP(t), "DOMContentLoaded", 16)) || (JSL(t) == 4 && J_memeq(JSP(t), "load", 4))))
                J_dom_loaded_fn[J_ndom_loaded++] = a1;
            return J_undef();
        }
        if (J_is_name(k, kl, "removeEventListener")) return J_undef();
        int p = J_prop_find(J_genv, k, kl);
        if (p >= 0 && js_props[p].val.t == JT_OBJ) return J_call_value(js_props[p].val, recv, args, n);
        J_error("TypeError: unknown window method"); return J_undef();
    }
    if (o->kind == JK_CLASSLIST) {
        int node = (int)o->aux; jv nm = J_tostr(a0);
        const char *cs = WCS(wc_nodes[node].cls); u32 cl = wc_nodes[node].cls.len, nl = JSL(nm);
        const char *name = JSP(nm);
        int has = 0; u32 i = 0;
        while (i < cl && nl) { while (i < cl && wc_isspace(cs[i])) i++; u32 st = i; while (i < cl && !wc_isspace(cs[i])) i++; if (i - st == nl && J_memeq(cs + st, name, nl)) has = 1; }
        if (J_is_name(k, kl, "contains")) return J_bool(has);
        int want_add = J_is_name(k, kl, "add") || (J_is_name(k, kl, "toggle") && !has);
        int want_rem = J_is_name(k, kl, "remove") || (J_is_name(k, kl, "toggle") && has);
        if (!want_add && !want_rem) { J_error("TypeError: unknown classList method"); return J_undef(); }
        static char b[600]; u32 o2 = 0;
        if (want_add) {
            if (!has && nl) { for (u32 q = 0; q < cl && o2 < 500; q++) b[o2++] = cs[q]; if (o2) b[o2++] = ' '; for (u32 q = 0; q < nl && o2 < 590; q++) b[o2++] = name[q]; wc_nodes[node].cls = wc_put(b, o2); wj_dirty = 1; }
        } else {
            i = 0;
            while (i < cl) { while (i < cl && wc_isspace(cs[i])) i++; u32 st = i; while (i < cl && !wc_isspace(cs[i])) i++;
                if (i > st && !(i - st == nl && J_memeq(cs + st, name, nl))) { if (o2) b[o2++] = ' '; for (u32 q = st; q < i && o2 < 590; q++) b[o2++] = cs[q]; } }
            wc_nodes[node].cls = wc_put(b, o2); wj_dirty = 1;
        }
        return J_is_name(k, kl, "toggle") ? J_bool(want_add) : J_undef();
    }
    if (o->kind == JK_STYLE) {
        if (J_is_name(k, kl, "setProperty")) { jv nm = J_tostr(a0), v = J_tostr(a1); wc_style_set((int)o->aux, JSP(nm), JSL(nm), JSP(v), JSL(v)); wj_dirty = 1; return J_undef(); }
        if (J_is_name(k, kl, "removeProperty") || J_is_name(k, kl, "getPropertyValue")) return J_mkcstr("");
    }
     
    {
        int p = J_prop_find((int)recv.v, k, kl);
        if (p >= 0 && js_props[p].val.t == JT_OBJ) return J_call_value(js_props[p].val, recv, args, n);
    }
    J_error("TypeError: object has no such method");
    return J_undef();
}

 
static void J_def_native(const char *name, int id) {
    int o = J_new(JK_NATIVE); js_objs[o].aux = (u32)id;
    J_prop_setc(J_genv, name, J_objv(o));
}
static void wj_reset(void) {
    js_arena_len = 1; js_obj_count = 1; js_prop_count = 0; js_elem_count = 0;    
    for (int i = 0; i < WC_MAX_NODES; i++) { js_dom_cache[i] = 0; js_click_fn[i] = 0; }
    J_err = 0; J_errmsg[0] = 0; J_flow = 0; J_depth = 0; J_steps = 0; J_ntimers = 0; J_ndom_loaded = 0;
    J_script_node = -1; J_write_cursor = -1; wj_dirty = 0; wj_status[0] = 0;
    wj_script_errors = wj_scripts_run = wj_scripts_skipped = 0; wj_err_text[0] = 0;
    J_genv = J_env_new(-1); J_env = J_genv;
    int win = J_new(JK_WIN), doc = J_new(JK_DOC), math = J_new(JK_MATH), con = J_new(JK_CONSOLE);
    J_prop_setc(J_genv, "window", J_objv(win)); J_prop_setc(J_genv, "self", J_objv(win)); J_prop_setc(J_genv, "this", J_objv(win));
    J_prop_setc(J_genv, "document", J_objv(doc)); J_prop_setc(J_genv, "Math", J_objv(math)); J_prop_setc(J_genv, "console", J_objv(con));
    J_def_native("alert", N_ALERT); J_def_native("parseInt", N_PARSEINT); J_def_native("parseFloat", N_PARSEINT);
    J_def_native("String", N_STRING); J_def_native("Number", N_NUMBER); J_def_native("Boolean", N_BOOLEAN);
    J_def_native("isNaN", N_ISNAN); J_def_native("setTimeout", N_SETTIMEOUT); J_def_native("setInterval", N_SETTIMEOUT);
    J_def_native("clearTimeout", N_NOOP); J_def_native("clearInterval", N_NOOP);
    J_def_native("matchMedia", N_MATCHMEDIA); J_def_native("requestAnimationFrame", N_SETTIMEOUT); J_def_native("cancelAnimationFrame", N_NOOP);
     
    {
        jv noop = J_native_val(N_NOOP), nul = J_native_val(N_NULL);
        for (int w = 0; w < 2; w++) {                              
            int ls = J_new(JK_OBJ);
            J_prop_setc(ls, "getItem", nul); J_prop_setc(ls, "key", nul);
            J_prop_setc(ls, "setItem", noop); J_prop_setc(ls, "removeItem", noop); J_prop_setc(ls, "clear", noop);
            J_prop_setc(ls, "length", J_num(0));
            J_prop_setc(J_genv, w ? "sessionStorage" : "localStorage", J_objv(ls));
        }
        int nav = J_new(JK_OBJ);
        J_prop_setc(nav, "userAgent", J_mkcstr("Fexox OS x86_64")); J_prop_setc(nav, "language", J_mkcstr("en"));
        J_prop_setc(nav, "platform", J_mkcstr("Fexox OS")); J_prop_setc(nav, "onLine", J_bool(1));
        J_prop_setc(J_genv, "navigator", J_objv(nav));
        int loc = J_new(JK_OBJ);
        const char *u = wj_page_url, *h = u; u32 hl = 0;
        if (u[0] == 'h' || u[0] == 'H') { while (*h && *h != '/') h++; if (*h == '/' && h[1] == '/') h += 2; } 
        const char *hs = h; while (*h && *h != '/') { h++; }
        hl = (u32)(h - hs);
        u32 hn = hl; for (u32 q = 0; q < hl; q++) if (hs[q] == ':') { hn = q; break; }
        const char *ps = h; u32 pl = 0; while (ps[pl] && ps[pl] != '?' && ps[pl] != '#') pl++;
        J_prop_setc(loc, "href", J_mkcstr(u)); J_prop_setc(loc, "protocol", J_mkcstr("http:"));
        J_prop_setc(loc, "host", J_mkstr(hs, hl)); J_prop_setc(loc, "hostname", J_mkstr(hs, hn));
        J_prop_setc(loc, "pathname", pl ? J_mkstr(ps, pl) : J_mkcstr("/")); J_prop_setc(loc, "origin", J_mkcstr(""));
        J_prop_setc(J_genv, "location", J_objv(loc));
    }
}

 
static int wj_exec(const char *code, u32 n, int this_node) {
    J_p = code; J_end = code + n;
    J_err = 0; J_errmsg[0] = 0; J_flow = 0; J_steps = 0; J_depth = 0; J_env = J_genv;
    int saved_this = -1;
    int tp = J_prop_find(J_genv, "this", 4);
    jv old_this = J_undef();
    if (tp >= 0) old_this = js_props[tp].val;
    if (this_node >= 0) J_prop_setc(J_genv, "this", J_dom_wrap(this_node));
    (void)saved_this;
    J_next();
     
    {
        jstate st0 = J_save(); int depth = 0;
        while (J_tok.type != TK_EOF && !J_err) {
            if (J_isp('{')) depth++; else if (J_isp('}')) depth--;
            else if (depth == 0 && J_isid("function")) {
                J_next();
                if (J_tok.type == TK_ID) {
                    const char *nm = J_tok.s; u32 nl = J_tok.len; J_next();
                    if (J_isp('(')) { jv f = J_make_func(J_tok.s, 0); if (!J_err) J_prop_set(J_genv, nm, nl, f); }
                }
                continue;
            }
            J_next();
        }
        J_restore(st0);
    }
    while (!J_err && J_tok.type != TK_EOF) J_stmt(1);
    if (this_node >= 0) J_prop_setc(J_genv, "this", old_this);
    if (J_err) { J_record_error(this_node >= 0); return -1; }
    return 0;
}

static void wj_run_timers(void) {
     
    for (int i = 1; i < J_ntimers; i++) { jtimer t = J_timers[i]; int k = i; while (k > 0 && J_timers[k - 1].ms > t.ms) { J_timers[k] = J_timers[k - 1]; k--; } J_timers[k] = t; }
    int n = J_ntimers; J_ntimers = 0;
    for (int i = 0; i < n; i++) {
        J_err = 0; J_steps = 0; J_depth = 0; J_flow = 0;
        J_call_value(J_timers[i].fn, J_undef(), NULL, 0);
        if (J_err) { J_record_error(0); J_err = 0; }
    }
}

 
static void wj_run_page(void) {
    wj_reset();
    for (int i = 1; i < wc_node_count; i++) {
        wc_node *nd = &wc_nodes[i];
        if (nd->type != 1 || nd->tag != TG_SCRIPT) continue;
        if (nd->aux.len || (nd->flags & 0x20)) { wj_scripts_skipped++; continue; }
        static char code[65536]; u32 cl = 0;
        for (int c = nd->first; c >= 0; c = wc_nodes[c].next)
            if (wc_nodes[c].type == 2) for (u32 k = 0; k < wc_nodes[c].text.len && cl < sizeof(code) - 1; k++) code[cl++] = WCS(wc_nodes[c].text)[k];
        code[cl] = 0;
        J_script_node = i; J_write_cursor = i;
        wj_scripts_run++;
        wj_exec(code, cl, -1);
        J_err = 0;
    }
    J_script_node = -1; J_write_cursor = -1;
     
    for (int i = 0; i < J_ndom_loaded; i++) { J_err = 0; J_call_value(J_dom_loaded_fn[i], J_undef(), NULL, 0); if (J_err) { wj_script_errors++; J_err = 0; } }
    int p = J_prop_find(J_genv, "onload", 6);
    if (p >= 0 && js_props[p].val.t == JT_OBJ) { J_err = 0; J_call_value(js_props[p].val, J_undef(), NULL, 0); if (J_err) { wj_script_errors++; J_err = 0; } }
    wj_run_timers();
}

 

static int wj_click(int node) {
    for (int n = node; n >= 0; n = wc_nodes[n].parent) {
        if (wc_nodes[n].type != 1) continue;
        if (js_click_fn[n]) {
            J_err = 0; J_steps = 0; J_depth = 0; J_flow = 0; J_write_cursor = -1;
            J_call_value(J_objv(js_click_fn[n] - 1), J_dom_wrap(n), NULL, 0);
            if (J_err) { J_record_error(1); J_err = 0; }
            wj_run_timers();
            return 1;
        }
        if (wc_nodes[n].has_onclick) {
            J_write_cursor = -1;
            wj_exec(WCS(wc_nodes[n].onclick), wc_nodes[n].onclick.len, n);
            wj_run_timers();
            return 1;
        }
    }
    return 0;
}

#endif  
