/* Fexox OS Project, EULA GPLv2 */
 


#ifndef WEBCORE_H
#define WEBCORE_H

#define WC_MAX_NODES   4000
#define WC_POOL_SIZE   (512u * 1024u)
#define WC_MAX_RULES   500
#define WC_MAX_DECLS   3000
#define WC_LTEXT_SIZE  (200u * 1024u)
#define WC_MAX_SEGS    14000
#define WC_MAX_LINES   7000
#define WC_MAX_BOXES   700
#define WC_MAX_DEPTH   48

 
typedef struct { u32 off, len; } wstr;
static char wc_pool[WC_POOL_SIZE];
static u32  wc_pool_len;
#define WCS(w) (wc_pool + (w).off)

static wstr wc_put(const char *s, u32 n) {
    wstr r; r.off = 0; r.len = 0;
    if (wc_pool_len + n + 1 > WC_POOL_SIZE) return r;
    r.off = wc_pool_len; r.len = n;
    for (u32 i = 0; i < n; i++) wc_pool[wc_pool_len++] = s[i];
    wc_pool[wc_pool_len++] = 0;
    return r;
}
static u32 wc_slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static int wc_lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
 
static int wc_ieq(const char *s, u32 n, const char *lit) {
    u32 i = 0;
    for (; i < n; i++) { if (!lit[i] || wc_lc((u8)s[i]) != wc_lc((u8)lit[i])) return 0; }
    return lit[i] == 0;
}
static int wc_contains_ci(const char *s, u32 n, const char *lit) {
    u32 l = wc_slen(lit);
    for (u32 i = 0; i + l <= n; i++) { u32 k = 0; while (k < l && wc_lc((u8)s[i + k]) == wc_lc((u8)lit[k])) k++; if (k == l) return 1; }
    return 0;
}
static int wc_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int wc_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int wc_isdigit(int c) { return c >= '0' && c <= '9'; }
static int wc_isident(int c) { return wc_isalpha(c) || wc_isdigit(c) || c == '-' || c == '_'; }

 
enum {
    TG_UNKNOWN = 0, TG_HTML, TG_HEAD, TG_BODY, TG_TITLE, TG_STYLE, TG_SCRIPT, TG_NOSCRIPT,
    TG_DIV, TG_P, TG_SPAN, TG_A, TG_H1, TG_H2, TG_H3, TG_H4, TG_H5, TG_H6,
    TG_UL, TG_OL, TG_LI, TG_BR, TG_HR, TG_PRE, TG_CODE, TG_B, TG_I, TG_U, TG_S, TG_MARK,
    TG_IMG, TG_TABLE, TG_TR, TG_TD, TG_TH, TG_BLOCKQUOTE, TG_BUTTON, TG_INPUT, TG_CENTER,
    TG_SMALL, TG_BIG, TG_TEXTAREA, TG_META, TG_LINK, TG_BLOCK, TG_FONT, TG_DT, TG_DD,
    TG_HIDDEN, TG_LABEL
};
#define TF_BLOCK 1
#define TF_VOID  2
#define TF_RAW   4      
typedef struct { const char *name; u8 id; u8 flags; } wc_tagdef;
static const wc_tagdef wc_tags[] = {
    {"html",TG_HTML,TF_BLOCK},{"head",TG_HEAD,0},{"body",TG_BODY,TF_BLOCK},{"title",TG_TITLE,TF_RAW},
    {"style",TG_STYLE,TF_RAW},{"script",TG_SCRIPT,TF_RAW},{"noscript",TG_NOSCRIPT,0},
    {"div",TG_DIV,TF_BLOCK},{"p",TG_P,TF_BLOCK},{"span",TG_SPAN,0},{"a",TG_A,0},
    {"h1",TG_H1,TF_BLOCK},{"h2",TG_H2,TF_BLOCK},{"h3",TG_H3,TF_BLOCK},{"h4",TG_H4,TF_BLOCK},
    {"h5",TG_H5,TF_BLOCK},{"h6",TG_H6,TF_BLOCK},{"ul",TG_UL,TF_BLOCK},{"ol",TG_OL,TF_BLOCK},
    {"li",TG_LI,TF_BLOCK},{"br",TG_BR,TF_VOID},{"hr",TG_HR,TF_BLOCK|TF_VOID},{"pre",TG_PRE,TF_BLOCK},
    {"code",TG_CODE,0},{"tt",TG_CODE,0},{"kbd",TG_CODE,0},{"samp",TG_CODE,0},
    {"b",TG_B,0},{"strong",TG_B,0},{"i",TG_I,0},{"em",TG_I,0},{"cite",TG_I,0},{"var",TG_I,0},{"dfn",TG_I,0},
    {"u",TG_U,0},{"ins",TG_U,0},{"s",TG_S,0},{"strike",TG_S,0},{"del",TG_S,0},{"mark",TG_MARK,0},
    {"img",TG_IMG,TF_VOID},{"table",TG_TABLE,TF_BLOCK},{"tr",TG_TR,TF_BLOCK},{"td",TG_TD,0},{"th",TG_TH,0},
    {"blockquote",TG_BLOCKQUOTE,TF_BLOCK},{"button",TG_BUTTON,0},{"input",TG_INPUT,TF_VOID},
    {"center",TG_CENTER,TF_BLOCK},{"small",TG_SMALL,0},{"big",TG_BIG,0},{"textarea",TG_TEXTAREA,TF_RAW},
    {"meta",TG_META,TF_VOID},{"link",TG_LINK,TF_VOID},{"font",TG_FONT,0},{"label",TG_LABEL,0},
    {"dt",TG_DT,TF_BLOCK},{"dd",TG_DD,TF_BLOCK},
    {"section",TG_BLOCK,TF_BLOCK},{"article",TG_BLOCK,TF_BLOCK},{"nav",TG_BLOCK,TF_BLOCK},
    {"header",TG_BLOCK,TF_BLOCK},{"footer",TG_BLOCK,TF_BLOCK},{"main",TG_BLOCK,TF_BLOCK},
    {"aside",TG_BLOCK,TF_BLOCK},{"figure",TG_BLOCK,TF_BLOCK},{"figcaption",TG_BLOCK,TF_BLOCK},
    {"fieldset",TG_BLOCK,TF_BLOCK},{"address",TG_BLOCK,TF_BLOCK},{"form",TG_BLOCK,TF_BLOCK},
    {"dl",TG_BLOCK,TF_BLOCK},{"thead",TG_BLOCK,0},{"tbody",TG_BLOCK,0},{"tfoot",TG_BLOCK,0},
    {"caption",TG_BLOCK,TF_BLOCK},{"details",TG_BLOCK,TF_BLOCK},{"summary",TG_BLOCK,TF_BLOCK},
    {"menu",TG_UL,TF_BLOCK},{"dir",TG_UL,TF_BLOCK},
     
    {"svg",TG_HIDDEN,0},{"iframe",TG_HIDDEN,0},{"object",TG_HIDDEN,0},{"canvas",TG_HIDDEN,0},
    {"template",TG_HIDDEN,0},{"select",TG_HIDDEN,0},{"video",TG_HIDDEN,0},{"audio",TG_HIDDEN,0},
    {"base",TG_META,TF_VOID},{"area",TG_META,TF_VOID},{"col",TG_META,TF_VOID},{"embed",TG_META,TF_VOID},
    {"param",TG_META,TF_VOID},{"source",TG_META,TF_VOID},{"track",TG_META,TF_VOID},{"wbr",TG_META,TF_VOID},
    {NULL,0,0}
};

 
typedef struct {
    u8   type;             
    u8   tn;               
    u8   tag;
    u8   flags;            
    int  parent, first, last, next;
    wstr text;             
    wstr id, cls, style, href, aux, onclick;
    u8   has_onclick;
} wc_node;
static wc_node wc_nodes[WC_MAX_NODES];
static int     wc_node_count;      
static wstr    wc_title;
static int     wc_js_enabled = 1;    

static int wc_new_node(int type, int tag, int flags) {
    if (wc_node_count >= WC_MAX_NODES) return -1;
    int i = wc_node_count++;
    wc_node *n = &wc_nodes[i];
    n->type = (u8)type; n->tag = (u8)tag; n->flags = (u8)flags; n->tn = 0;
    n->parent = n->first = n->last = n->next = -1;
    n->text.off = n->text.len = 0;
    n->id = n->cls = n->style = n->href = n->aux = n->onclick = n->text;
    n->has_onclick = 0;
    return i;
}
static void wc_append_child(int parent, int child) {
    wc_node *p = &wc_nodes[parent];
    wc_nodes[child].parent = parent;
    wc_nodes[child].next = -1;
    if (p->last < 0) { p->first = p->last = child; }
    else { wc_nodes[p->last].next = child; p->last = child; }
}
 
static void wc_insert_after(int sibling, int child) {
    wc_node *s = &wc_nodes[sibling];
    int par = s->parent;
    wc_nodes[child].parent = par;
    wc_nodes[child].next = s->next;
    s->next = child;
    if (par >= 0 && wc_nodes[par].last == sibling) wc_nodes[par].last = child;
}
static void wc_remove_children(int node) {
    wc_nodes[node].first = wc_nodes[node].last = -1;
}

static const wc_tagdef *wc_find_tag(const char *s, u32 n) {
    for (int i = 0; wc_tags[i].name; i++) if (wc_ieq(s, n, wc_tags[i].name)) return &wc_tags[i];
    return NULL;
}

 
static u32 wc_emit(char *out, u32 cap, u32 o, const char *s) {
    while (*s && o < cap) out[o++] = *s++;
    return o;
}

 
static const char *wc_fold_l1[64] = {
    "A","A","A","A","A","A","AE","C",
    "E","E","E","E","I","I","I","I",
    "D","N","O","O","O","O","O","x",
    "O","U","U","U","U","Y","Th","ss",
    "a","a","a","a","a","a","ae","c",
    "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","/",
    "o","u","u","u","u","y","th","y",
};
static const char *wc_fold_la[128] = {
    "A","a","A","a","A","a","C","c",
    "C","c","C","c","C","c","D","d",
    "D","d","E","e","E","e","E","e",
    "E","e","E","e","G","g","G","g",
    "G","g","G","g","H","h","H","h",
    "I","i","I","i","I","i","I","i",
    "I","i","IJ","ij","J","j","K","k",
    "k","L","l","L","l","L","l","L",
    "l","L","l","N","n","N","n","N",
    "n","n","N","n","O","o","O","o",
    "O","o","OE","oe","R","r","R","r",
    "R","r","S","s","S","s","S","s",
    "S","s","T","t","T","t","T","t",
    "U","u","U","u","U","u","U","u",
    "U","u","U","u","W","w","Y","y",
    "Y","Z","z","Z","z","Z","z","s",
};
static const char *wc_cyr_up[32] = {
    "A","B","V","G","D","E","Zh","Z","I","Y","K","L","M","N","O","P","R","S","T","U","F","Kh","Ts","Ch","Sh","Shch","\"","Y","'","E","Yu","Ya"
};
static const char *wc_cyr_lo[32] = {
    "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p","r","s","t","u","f","kh","ts","ch","sh","shch","\"","y","'","e","yu","ya"
};
 
static u32 wc_uni_emit(char *out, u32 cap, u32 o, u32 cp) {
    const char *r = NULL;
    if (cp >= 0xC0 && cp <= 0xFF) r = wc_fold_l1[cp - 0xC0];
    else if (cp >= 0x100 && cp <= 0x17F) r = wc_fold_la[cp - 0x100];
    else if (cp >= 0x410 && cp <= 0x42F) r = wc_cyr_up[cp - 0x410];
    else if (cp >= 0x430 && cp <= 0x44F) r = wc_cyr_lo[cp - 0x430];
    else switch (cp) {
        case 0xA0: case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: r = " "; break;
        case 0xA1: r = "!"; break; case 0xBF: r = "?"; break;
        case 0xA2: r = "c"; break; case 0xA3: r = "GBP "; break; case 0xA5: r = "JPY "; break; case 0x20AC: r = "EUR "; break;
        case 0xA9: r = "(c)"; break; case 0xAE: r = "(R)"; break; case 0x2122: r = "(TM)"; break;
        case 0xAB: r = "<<"; break; case 0xBB: r = ">>"; break;
        case 0xB0: r = "deg"; break; case 0xB1: r = "+/-"; break; case 0xB7: case 0x2219: case 0x22C5: r = "."; break;
        case 0xBC: r = "1/4"; break; case 0xBD: r = "1/2"; break; case 0xBE: r = "3/4"; break;
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: r = "-"; break;
        case 0x2014: case 0x2015: r = "--"; break;
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: r = "'"; break;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: r = "\""; break;
        case 0x2022: case 0x25CF: case 0x25AA: r = "*"; break;
        case 0x2026: r = "..."; break;
        case 0x2190: r = "<-"; break; case 0x2192: r = "->"; break; case 0x2191: r = "^"; break; case 0x2193: r = "v"; break;
        case 0x21D2: r = "=>"; break; case 0x2264: r = "<="; break; case 0x2265: r = ">="; break; case 0x2260: r = "!="; break;
        case 0x401: r = "Yo"; break; case 0x451: r = "yo"; break; case 0x404: r = "Ye"; break; case 0x454: r = "ye"; break;
        case 0x406: r = "I"; break; case 0x456: r = "i"; break; case 0x407: r = "Yi"; break; case 0x457: r = "yi"; break;
        case 0x490: r = "G"; break; case 0x491: r = "g"; break;
        case 0x2116: r = "No."; break;
        case 0xFE0F: case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: r = ""; break;      
        default: break;
    }
    if (!r) r = "?";
    while (*r && o < cap) out[o++] = *r++;
    return o;
}

 
static u32 wc_decode(const u8 *s, u32 n, char *out, u32 cap) {
    u32 o = 0, i = 0;
    while (i < n && o < cap) {
        u8 c = s[i];
        if (c == '&') {
            u32 j = i + 1; char ent[12]; int el = 0;
            while (j < n && el < 10 && s[j] != ';' && s[j] != '&' && s[j] != '<' && !wc_isspace(s[j])) ent[el++] = (char)s[j++];
            ent[el] = 0;
            if (j < n && s[j] == ';' && el > 0) {
                const char *rep = NULL;
                if (wc_ieq(ent, (u32)el, "amp")) rep = "&"; else if (wc_ieq(ent, (u32)el, "lt")) rep = "<";
                else if (wc_ieq(ent, (u32)el, "gt")) rep = ">"; else if (wc_ieq(ent, (u32)el, "quot")) rep = "\"";
                else if (wc_ieq(ent, (u32)el, "apos")) rep = "'"; else if (wc_ieq(ent, (u32)el, "nbsp")) rep = " ";
                else if (wc_ieq(ent, (u32)el, "copy")) rep = "(c)"; else if (wc_ieq(ent, (u32)el, "reg")) rep = "(R)";
                else if (wc_ieq(ent, (u32)el, "hellip")) rep = "..."; else if (wc_ieq(ent, (u32)el, "mdash")) rep = "--";
                else if (wc_ieq(ent, (u32)el, "ndash")) rep = "-"; else if (wc_ieq(ent, (u32)el, "bull")) rep = "*";
                else if (wc_ieq(ent, (u32)el, "middot")) rep = "."; else if (wc_ieq(ent, (u32)el, "laquo")) rep = "<<";
                else if (wc_ieq(ent, (u32)el, "raquo")) rep = ">>"; else if (wc_ieq(ent, (u32)el, "times")) rep = "x";
                else if (wc_ieq(ent, (u32)el, "lsquo") || wc_ieq(ent, (u32)el, "rsquo")) rep = "'";
                else if (wc_ieq(ent, (u32)el, "ldquo") || wc_ieq(ent, (u32)el, "rdquo")) rep = "\"";
                else if (wc_ieq(ent, (u32)el, "trade")) rep = "(TM)"; else if (wc_ieq(ent, (u32)el, "euro")) rep = "EUR";
                if (rep) { o = wc_emit(out, cap, o, rep); i = j + 1; continue; }
                if (ent[0] == '#') {
                    u32 code = 0; int hex = (ent[1] == 'x' || ent[1] == 'X');
                    for (int q = hex ? 2 : 1; q < el; q++) {
                        int ch = ent[q], d;
                        if (wc_isdigit(ch)) d = ch - '0';
                        else if (hex && ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                        else if (hex && ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                        else { d = -1; }
                        if (d < 0) break;
                        code = code * (hex ? 16u : 10u) + (u32)d;
                        if (code > 0x10FFFF) break;
                    }
                    if (code >= 0x20 && code < 0x7F) out[o++] = (char)code;
                    else if (code == '\t' || code == '\n') out[o++] = ' ';
                    else o = wc_uni_emit(out, cap, o, code);
                    i = j + 1; continue;
                }
            }
            out[o++] = '&'; i++; continue;
        }
        if (c < 0x80) { out[o++] = (char)c; i++; continue; }
         
        u32 len = (c >= 0xF8) ? 1 : (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC2) ? 2 : 1;    
        u32 cp = 0xFFFD;
        if (len > 1) {
            int ok = (i + len <= n);
            for (u32 k = 1; ok && k < len; k++) if ((s[i + k] & 0xC0) != 0x80) ok = 0;
            if (ok) {
                cp = (len == 2) ? (u32)(c & 0x1F) : (len == 3) ? (u32)(c & 0x0F) : (u32)(c & 0x07);
                for (u32 k = 1; k < len; k++) cp = (cp << 6) | (u32)(s[i + k] & 0x3F);
            } else len = 1;                                
        }
        o = wc_uni_emit(out, cap, o, cp);
        i += len;
    }
    return o;
}

 
static int wc_open[WC_MAX_DEPTH + 2];    
static int wc_open_n;

static void wc_add_text(int parent, const u8 *s, u32 n, int decode) {
    if (n == 0) return;
    static char tmp[16384];
    while (n > 0) {
        u32 chunk = n > 8000 ? 8000 : n;
        u32 l;
        if (decode) l = wc_decode(s, chunk, tmp, sizeof(tmp));
        else { l = chunk; for (u32 i = 0; i < l; i++) tmp[i] = (char)s[i]; }
        if (l > 0) {
            int t = wc_new_node(2, 0, 0);
            if (t < 0) return;
            wc_nodes[t].text = wc_put(tmp, l);
            wc_append_child(parent, t);
        }
        s += chunk; n -= chunk;
    }
}

 
static void wc_parse_attrs(int node, const u8 *s, u32 n) {
    static char legacy[160];
    u32 ll = 0;
    legacy[0] = 0;
    u32 i = 0;
    int tag = wc_nodes[node].tag;
    while (i < n) {
        while (i < n && (wc_isspace(s[i]) || s[i] == '/')) i++;
        u32 ns = i;
        while (i < n && !wc_isspace(s[i]) && s[i] != '=' && s[i] != '/' ) i++;
        u32 nl = i - ns;
        if (nl == 0) { i++; continue; }
        while (i < n && wc_isspace(s[i])) i++;
        u32 vs = 0, vl = 0;
        if (i < n && s[i] == '=') {
            i++;
            while (i < n && wc_isspace(s[i])) i++;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                u8 q = s[i++]; vs = i;
                while (i < n && s[i] != q) i++;
                vl = i - vs; if (i < n) i++;
            } else {
                vs = i;
                while (i < n && !wc_isspace(s[i])) i++;
                vl = i - vs;
            }
        }
        static char val[2048];
        u32 dl = wc_decode(s + vs, vl > 2000 ? 2000 : vl, val, sizeof(val) - 1);
        val[dl] = 0;
        const char *nm = (const char *)s + ns;
        wc_node *nd = &wc_nodes[node];
        if (wc_ieq(nm, nl, "id")) nd->id = wc_put(val, dl);
        else if (wc_ieq(nm, nl, "class")) nd->cls = wc_put(val, dl);
        else if (wc_ieq(nm, nl, "style")) nd->style = wc_put(val, dl);
        else if (wc_ieq(nm, nl, "href")) nd->href = wc_put(val, dl);
        else if (wc_ieq(nm, nl, "onclick")) { nd->onclick = wc_put(val, dl); nd->has_onclick = 1; }
        else if (wc_ieq(nm, nl, "alt") || (wc_ieq(nm, nl, "value") && nd->aux.len == 0) ||
                 (wc_ieq(nm, nl, "placeholder") && nd->aux.len == 0)) nd->aux = wc_put(val, dl);
        else if (wc_ieq(nm, nl, "type") && tag == TG_INPUT) {
             
            if (wc_ieq(val, dl, "hidden")) nd->flags |= 0x10;
        }
        else if (wc_ieq(nm, nl, "src") && tag == TG_SCRIPT) nd->aux = wc_put(val, dl);        
        else if (wc_ieq(nm, nl, "rel") && tag == TG_LINK) { if (wc_contains_ci(val, dl, "stylesheet")) nd->flags |= 0x40; }
        else if (wc_ieq(nm, nl, "media") && tag == TG_LINK) { if (wc_contains_ci(val, dl, "print")) nd->flags |= 0x80; }
        else if (wc_ieq(nm, nl, "type") && tag == TG_SCRIPT) {
             
            if (dl > 0 && !wc_ieq(val, dl, "text/javascript") && !wc_ieq(val, dl, "application/javascript")) nd->flags |= 0x20;
        }
         
        else if (wc_ieq(nm, nl, "bgcolor") && ll + dl + 20 < sizeof(legacy)) {
            ll = wc_emit(legacy, sizeof(legacy) - 1, ll, "background-color:"); for (u32 k = 0; k < dl; k++) legacy[ll++] = val[k]; legacy[ll++] = ';'; legacy[ll] = 0;
        } else if (wc_ieq(nm, nl, "text") && tag == TG_BODY && ll + dl + 10 < sizeof(legacy)) {
            ll = wc_emit(legacy, sizeof(legacy) - 1, ll, "color:"); for (u32 k = 0; k < dl; k++) legacy[ll++] = val[k]; legacy[ll++] = ';'; legacy[ll] = 0;
        } else if (wc_ieq(nm, nl, "color") && tag == TG_FONT && ll + dl + 10 < sizeof(legacy)) {
            ll = wc_emit(legacy, sizeof(legacy) - 1, ll, "color:"); for (u32 k = 0; k < dl; k++) legacy[ll++] = val[k]; legacy[ll++] = ';'; legacy[ll] = 0;
        } else if (wc_ieq(nm, nl, "align") && ll + dl + 14 < sizeof(legacy)) {
            ll = wc_emit(legacy, sizeof(legacy) - 1, ll, "text-align:"); for (u32 k = 0; k < dl; k++) legacy[ll++] = val[k]; legacy[ll++] = ';'; legacy[ll] = 0;
        }
    }
    if (ll > 0) {
         
        wc_node *nd = &wc_nodes[node];
        static char both[2300];
        u32 bl = 0;
        for (u32 k = 0; k < ll && bl < sizeof(both) - 1; k++) both[bl++] = legacy[k];
        for (u32 k = 0; k < nd->style.len && bl < sizeof(both) - 1; k++) both[bl++] = WCS(nd->style)[k];
        both[bl] = 0;
        nd->style = wc_put(both, bl);
    }
}

static void wc_pop_to(int idx) { wc_open_n = idx; }

static int wc_stack_find_tag(int tag, int stop_at_block_container) {
    for (int k = wc_open_n - 1; k >= 1; k--) {
        int t = wc_nodes[wc_open[k]].tag;
        if (t == tag) return k;
        if (stop_at_block_container && (t == TG_UL || t == TG_OL || t == TG_TABLE || t == TG_BODY || t == TG_HTML)) return -1;
    }
    return -1;
}

 

static void wc_parse_fragment(int root, const u8 *src, u32 len) {
    int saved_n = wc_open_n;
    int base = wc_open_n;
    wc_open[wc_open_n++] = root;            
    u32 i = 0;
    while (i < len) {
        if (src[i] != '<') {
            u32 j = i;
            while (j < len && src[j] != '<') j++;
            int cur = wc_open[wc_open_n - 1];
            int pre = 0;
            for (int k = wc_open_n - 1; k >= 0; k--) if (wc_nodes[wc_open[k]].tag == TG_PRE) { pre = 1; break; }
            (void)pre;
            wc_add_text(cur, src + i, j - i, 1);
            i = j;
            continue;
        }
         
        if (i + 3 < len && src[i + 1] == '!' && src[i + 2] == '-' && src[i + 3] == '-') {
            u32 j = i + 4;
            while (j + 2 < len && !(src[j] == '-' && src[j + 1] == '-' && src[j + 2] == '>')) j++;
            i = (j + 2 < len) ? j + 3 : len;
            continue;
        }
        if (i + 1 < len && (src[i + 1] == '!' || src[i + 1] == '?')) {       
            while (i < len && src[i] != '>') i++;
            i++;
            continue;
        }
        u32 j = i + 1;
        int closing = 0;
        if (j < len && src[j] == '/') { closing = 1; j++; }
        u32 ns = j;
        while (j < len && (wc_isalpha(src[j]) || wc_isdigit(src[j]) || src[j] == '-' )) j++;
        u32 nl = j - ns;
        if (nl == 0) {                             
            wc_add_text(wc_open[wc_open_n - 1], src + i, 1, 0);
            i++;
            continue;
        }
         
        u32 k = j; u8 q = 0;
        while (k < len) {
            if (q) { if (src[k] == q) q = 0; }
            else if (src[k] == '"' || src[k] == '\'') q = src[k];
            else if (src[k] == '>') break;
            k++;
        }
        u32 after = (k < len) ? k + 1 : k;
        const wc_tagdef *td = wc_find_tag((const char *)src + ns, nl);
        int tag = td ? td->id : TG_UNKNOWN;
        int tflags = td ? td->flags : 0;

        if (closing) {
            if (tag == TG_BR && 0) {}
             
            int found = -1;
            for (int s2 = wc_open_n - 1; s2 > base; s2--) {
                wc_node *on = &wc_nodes[wc_open[s2]];
                int same = td ? (on->tag == tag && (tag != TG_UNKNOWN)) : 0;
                if (!td) {
                     
                    same = (on->tag == TG_UNKNOWN);
                }
                if (same) { found = s2; break; }
                if (s2 < wc_open_n - 12) break;       
            }
            if (found > 0) wc_pop_to(found);
            else if (tag == TG_P) {                    
                int e = wc_new_node(1, TG_P, TF_BLOCK);
                if (e >= 0) wc_append_child(wc_open[wc_open_n - 1], e);
            }
            i = after;
            continue;
        }

         
        if (wc_open_n > base + 1) {
            int top_tag = wc_nodes[wc_open[wc_open_n - 1]].tag;
            if ((tflags & TF_BLOCK) && top_tag == TG_P && tag != TG_BR) wc_pop_to(wc_open_n - 1);
            if (tag == TG_LI) { int f = wc_stack_find_tag(TG_LI, 1); if (f > base) wc_pop_to(f); }
            else if (tag == TG_TR) {
                int f = wc_stack_find_tag(TG_TR, 1); if (f > base) wc_pop_to(f);
            } else if (tag == TG_TD || tag == TG_TH) {
                int f1 = wc_stack_find_tag(TG_TD, 1), f2 = wc_stack_find_tag(TG_TH, 1);
                int f = f1 > f2 ? f1 : f2; if (f > base) wc_pop_to(f);
            } else if (tag == TG_DT || tag == TG_DD) {
                int f1 = wc_stack_find_tag(TG_DT, 1), f2 = wc_stack_find_tag(TG_DD, 1);
                int f = f1 > f2 ? f1 : f2; if (f > base) wc_pop_to(f);
            }
        }

        int e = wc_new_node(1, tag, tflags);
        if (e < 0) break;
        wc_nodes[e].tn = td ? (u8)((td - wc_tags) + 1) : 0;
        wc_parse_attrs(e, src + j, (k > j ? k - j : 0));
        wc_append_child(wc_open[wc_open_n - 1], e);

         
        int selfclose = (k > j && k <= len && src[k - 1] == '/');

        if (tflags & TF_RAW) {
             
            u32 c = after;
            for (; c + 1 < len; c++) {
                if (src[c] == '<' && src[c + 1] == '/' && c + 2 + nl <= len) {
                    u32 z = 0;
                    while (z < nl && wc_lc(src[c + 2 + z]) == wc_lc(src[ns + z])) z++;
                    if (z == nl) break;
                }
            }
            if (c + 1 >= len) c = len;
            wc_add_text(e, src + after, c - after, tag == TG_TITLE || tag == TG_TEXTAREA);
            if (tag == TG_TITLE && wc_nodes[e].first >= 0) wc_title = wc_nodes[wc_nodes[e].first].text;
            u32 d = c;
            while (d < len && src[d] != '>') d++;
            i = (d < len) ? d + 1 : len;
            continue;
        }
        if (!(tflags & TF_VOID) && !selfclose && wc_open_n < WC_MAX_DEPTH) wc_open[wc_open_n++] = e;
        i = after;
    }
    wc_open_n = saved_n;
}

static void wc_parse_document(const u8 *src, u32 len) {
    wc_pool_len = 1;
    wc_node_count = 0;
    wc_open_n = 0;
    wc_title.off = wc_title.len = 0;
    int root = wc_new_node(1, TG_UNKNOWN, TF_BLOCK);
    (void)root;
    wc_parse_fragment(0, src, len);
}

 
enum {
    P_NONE = 0, P_COLOR, P_BG, P_FSIZE, P_FWEIGHT, P_TALIGN, P_TDECO, P_DISPLAY,
    P_MT, P_MB, P_ML, P_MR, P_PT, P_PB, P_PL, P_PR, P_WS, P_VIS, P_LSTYLE, P_FSTYLE,
    P_VAR, P_WIDTH, P_MAXW, P_TXFORM, P_BORDER, P_BORDER_T, P_BORDER_R, P_BORDER_B, P_BORDER_L,
    P_BCOLOR, P_BWIDTH, P_BSTYLE
};
typedef struct { u8 prop; u8 important; wstr val; wstr name; } wc_decl;

 

#define WC_MAX_VARS 96
static struct { wstr name, val; } wc_vars[WC_MAX_VARS];
static int wc_var_count;
static wc_decl wc_decls[WC_MAX_DECLS];
static int     wc_decl_count;

typedef struct {
    u8   tag;           
    u8   any_tag;
    wstr id;
    wstr cls[3];
    u8   n_cls;
    u8   child;         
} wc_comp;
typedef struct {
    wc_comp comp[3];    
    u8   n_comp;
    u16  spec;
    u16  order;
    u16  decl_start, decl_count;
} wc_rule;
static wc_rule wc_rules[WC_MAX_RULES];
static int     wc_rule_count;

typedef struct { u8 r, g, b; } wc_rgb;
typedef struct { const char *name; u8 r, g, b; } wc_named;
static const wc_named wc_colors[] = {
    {"black",0,0,0},{"white",255,255,255},{"red",255,0,0},{"green",0,128,0},{"blue",0,0,255},
    {"yellow",255,255,0},{"gray",128,128,128},{"grey",128,128,128},{"silver",192,192,192},
    {"maroon",128,0,0},{"purple",128,0,128},{"fuchsia",255,0,255},{"magenta",255,0,255},
    {"lime",0,255,0},{"olive",128,128,0},{"navy",0,0,128},{"teal",0,128,128},{"aqua",0,255,255},
    {"cyan",0,255,255},{"orange",255,165,0},{"pink",255,192,203},{"brown",165,42,42},
    {"gold",255,215,0},{"indigo",75,0,130},{"violet",238,130,238},{"crimson",220,20,60},
    {"coral",255,127,80},{"salmon",250,128,114},{"tomato",255,99,71},{"khaki",240,230,140},
    {"lightgray",211,211,211},{"lightgrey",211,211,211},{"darkgray",169,169,169},{"darkgrey",169,169,169},
    {"lightblue",173,216,230},{"lightgreen",144,238,144},{"darkblue",0,0,139},{"darkgreen",0,100,0},
    {"darkred",139,0,0},{"skyblue",135,206,235},{"steelblue",70,130,180},{"whitesmoke",245,245,245},
    {"beige",245,245,220},{"ivory",255,255,240},{"lavender",230,230,250},{"tan",210,180,140},
    {"orangered",255,69,0},{"dodgerblue",30,144,255},{"royalblue",65,105,225},{"slategray",112,128,144},
    {"dimgray",105,105,105},{"gainsboro",220,220,220},{"aliceblue",240,248,255},
    {NULL,0,0,0}
};
static int wc_hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
 
static int wc_parse_color(const char *s, u32 n, wc_rgb *out) {
    while (n && wc_isspace(*s)) { s++; n--; }
    while (n && wc_isspace(s[n - 1])) n--;
    if (n == 0) return 0;
    if (s[0] == '#') {
        if (n == 4) {
            int a = wc_hexv(s[1]), b = wc_hexv(s[2]), c = wc_hexv(s[3]);
            if (a < 0 || b < 0 || c < 0) return 0;
            out->r = (u8)(a * 17); out->g = (u8)(b * 17); out->b = (u8)(c * 17); return 1;
        }
        if (n == 7) {
            int v[6];
            for (int i = 0; i < 6; i++) { v[i] = wc_hexv(s[1 + i]); if (v[i] < 0) return 0; }
            out->r = (u8)(v[0] * 16 + v[1]); out->g = (u8)(v[2] * 16 + v[3]); out->b = (u8)(v[4] * 16 + v[5]); return 1;
        }
        return 0;
    }
    if (n > 4 && (wc_ieq(s, 4, "rgb(") || (n > 5 && wc_ieq(s, 5, "rgba(")))) {
        u32 i = (s[3] == '(') ? 4 : 5;
        int comp[3] = {0, 0, 0}, ci = 0;
        while (i < n && ci < 3) {
            while (i < n && (wc_isspace(s[i]) || s[i] == ',')) i++;
            int v = 0, any = 0;
            while (i < n && wc_isdigit(s[i])) { v = v * 10 + (s[i] - '0'); i++; any = 1; }
            if (!any) return 0;
            if (i < n && s[i] == '%') { v = v * 255 / 100; i++; }
            if (v > 255) v = 255;
            comp[ci++] = v;
        }
        if (ci < 3) return 0;
        out->r = (u8)comp[0]; out->g = (u8)comp[1]; out->b = (u8)comp[2];
        return 1;
    }
    if (wc_ieq(s, n, "transparent")) return 2;
    for (int i = 0; wc_colors[i].name; i++)
        if (wc_ieq(s, n, wc_colors[i].name)) { out->r = wc_colors[i].r; out->g = wc_colors[i].g; out->b = wc_colors[i].b; return 1; }
    return 0;
}

 
static int wc_parse_num100(const char *s, u32 n, int *out) {
    u32 i = 0; int neg = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-'); i++; }
    int ip = 0, fp = 0, fd = 0, any = 0;
    while (i < n && wc_isdigit(s[i])) { ip = ip * 10 + (s[i] - '0'); if (ip > 100000) ip = 100000; i++; any = 1; }
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && wc_isdigit(s[i])) { if (fd < 2) { fp = fp * 10 + (s[i] - '0'); fd++; } i++; any = 1; }
    }
    if (!any) return -1;
    while (fd < 2) { fp *= 10; fd++; }
    int v = ip * 100 + fp;
    *out = neg ? -v : v;
    return (int)i;
}
 


static int wc_vw = 900;         
static int wc_rem_px = 16;      
static int wc_istart_(const char *s, u32 n, const char *lit) {
    u32 l = wc_slen(lit); if (n < l) return 0;
    for (u32 i = 0; i < l; i++) if (wc_lc((u8)s[i]) != wc_lc((u8)lit[i])) return 0;
    return 1;
}
static int wc_ex_expr(const char **pp, const char *e, int base, long long *out, int depth);
static int wc_ex_factor(const char **pp, const char *e, int base, long long *out, int depth) {
    const char *p = *pp;
    while (p < e && wc_isspace(*p)) p++;
    if (p >= e || depth > 8) return 0;
    if (*p == '(') {
        p++;
        if (!wc_ex_expr(&p, e, base, out, depth + 1)) return 0;
        while (p < e && wc_isspace(*p)) p++;
        if (p >= e || *p != ')') return 0;
        *pp = p + 1; return 1;
    }
    int fn = 0;
    if (wc_istart_(p, (u32)(e - p), "calc(")) { fn = 1; p += 5; }
    else if (wc_istart_(p, (u32)(e - p), "min(")) { fn = 2; p += 4; }
    else if (wc_istart_(p, (u32)(e - p), "max(")) { fn = 3; p += 4; }
    else if (wc_istart_(p, (u32)(e - p), "clamp(")) { fn = 4; p += 6; }
    if (fn) {
        long long v[3]; int nv = 0;
        for (;;) {
            if (nv >= 3) return 0;
            if (!wc_ex_expr(&p, e, base, &v[nv], depth + 1)) return 0;
            nv++;
            while (p < e && wc_isspace(*p)) p++;
            if (p < e && *p == ',') { p++; continue; }
            break;
        }
        if (p >= e || *p != ')') return 0;
        p++;
        long long r = v[0];
        if (fn == 1) { if (nv != 1) return 0; }
        else if (fn == 2) { for (int i = 1; i < nv; i++) if (v[i] < r) r = v[i]; }
        else if (fn == 3) { for (int i = 1; i < nv; i++) if (v[i] > r) r = v[i]; }
        else { if (nv != 3) return 0; long long m = v[1] < v[2] ? v[1] : v[2]; r = v[0] > m ? v[0] : m; }
        *out = r; *pp = p; return 1;
    }
    int v100; int pos = wc_parse_num100(p, (u32)(e - p), &v100);
    if (pos < 0) return 0;
    p += pos;
    const char *us = p;
    while (p < e && (wc_isalpha(*p) || *p == '%')) p++;
    u32 ul = (u32)(p - us);
    long long r;
    if (ul == 0) r = v100;
    else if (wc_ieq(us, ul, "px")) r = v100;
    else if (wc_ieq(us, ul, "em")) r = (long long)v100 * base;
    else if (wc_ieq(us, ul, "rem")) r = (long long)v100 * wc_rem_px;
    else if (wc_ieq(us, ul, "pt")) r = (long long)v100 * 4 / 3;
    else if (wc_ieq(us, ul, "vw")) r = (long long)v100 * wc_vw / 100;
    else if (wc_ieq(us, ul, "vh")) r = (long long)v100 * 8;
    else if (wc_ieq(us, ul, "ex")) r = (long long)v100 * base / 2;
    else return 0;
    *out = r; *pp = p; return 1;
}
static int wc_ex_term(const char **pp, const char *e, int base, long long *out, int depth) {
    long long a;
    if (!wc_ex_factor(pp, e, base, &a, depth)) return 0;
    for (;;) {
        const char *p = *pp;
        while (p < e && wc_isspace(*p)) p++;
        if (p < e && (*p == '*' || *p == '/')) {
            char op = *p++;
            long long b;
            if (!wc_ex_factor(&p, e, base, &b, depth)) return 0;
            if (op == '*') a = a * b / 100; else { if (b == 0) return 0; a = a * 100 / b; }
            *pp = p;
        } else break;
    }
    *out = a; return 1;
}
static int wc_ex_expr(const char **pp, const char *e, int base, long long *out, int depth) {
    long long a;
    if (!wc_ex_term(pp, e, base, &a, depth)) return 0;
    for (;;) {
        const char *p = *pp;
        while (p < e && wc_isspace(*p)) p++;
        if (p < e && (*p == '+' || *p == '-') && p + 1 < e && wc_isspace(p[1])) {
            char op = *p++;
            long long b;
            if (!wc_ex_term(&p, e, base, &b, depth)) return 0;
            a = (op == '+') ? a + b : a - b;
            *pp = p;
        } else break;
    }
    *out = a; return 1;
}

 

static int wc_parse_len(const char *s, u32 n, int base_px, int *out) {
    while (n && wc_isspace(*s)) { s++; n--; }
    while (n && wc_isspace(s[n - 1])) n--;
    if (n == 0) return 0;
    if (n >= 4 && (wc_istart_(s, n, "calc(") || wc_istart_(s, n, "min(") || wc_istart_(s, n, "max(") || wc_istart_(s, n, "clamp("))) {
        const char *q = s; long long v;
        if (!wc_ex_expr(&q, s + n, base_px, &v, 0)) return 0;
        while (q < s + n && wc_isspace(*q)) q++;
        if (q != s + n) return 0;
        *out = (int)(v / 100); return 1;
    }
    int v100;
    int pos = wc_parse_num100(s, n, &v100);
    if (pos < 0) return 0;
    const char *u = s + pos; u32 ul = n - (u32)pos;
    if (ul == 0) { if (v100 == 0) { *out = 0; return 1; } *out = v100 / 100; return 1; }
    if (wc_ieq(u, ul, "px")) { *out = v100 / 100; return 1; }
    if (wc_ieq(u, ul, "em")) { *out = (int)((long)v100 * base_px / 100); return 1; }
    if (wc_ieq(u, ul, "rem")) { *out = (int)((long)v100 * wc_rem_px / 100); return 1; }
    if (wc_ieq(u, ul, "vw")) { *out = (int)((long)v100 * wc_vw / 10000); return 1; }
    if (wc_ieq(u, ul, "vh")) { *out = (int)((long)v100 * 8 / 100); return 1; }
    if (wc_ieq(u, ul, "%")) { *out = (int)((long)v100 * base_px / 10000); return 1; }
    if (wc_ieq(u, ul, "pt")) { *out = v100 * 4 / 300; return 1; }
    if (wc_ieq(u, ul, "ex")) { *out = (int)((long)v100 * base_px / 200); return 1; }
    return 0;
}

static int wc_prop_id(const char *s, u32 n) {
    if (wc_ieq(s, n, "color")) return P_COLOR;
    if (wc_ieq(s, n, "background-color") || wc_ieq(s, n, "background")) return P_BG;
    if (wc_ieq(s, n, "font-size")) return P_FSIZE;
    if (wc_ieq(s, n, "font-weight")) return P_FWEIGHT;
    if (wc_ieq(s, n, "font-style")) return P_FSTYLE;
    if (wc_ieq(s, n, "text-align")) return P_TALIGN;
    if (wc_ieq(s, n, "text-decoration") || wc_ieq(s, n, "text-decoration-line")) return P_TDECO;
    if (wc_ieq(s, n, "display")) return P_DISPLAY;
    if (wc_ieq(s, n, "margin-top")) return P_MT;
    if (wc_ieq(s, n, "margin-bottom")) return P_MB;
    if (wc_ieq(s, n, "margin-left")) return P_ML;
    if (wc_ieq(s, n, "margin-right")) return P_MR;
    if (wc_ieq(s, n, "padding-top")) return P_PT;
    if (wc_ieq(s, n, "padding-bottom")) return P_PB;
    if (wc_ieq(s, n, "padding-left")) return P_PL;
    if (wc_ieq(s, n, "padding-right")) return P_PR;
    if (wc_ieq(s, n, "white-space")) return P_WS;
    if (wc_ieq(s, n, "visibility")) return P_VIS;
    if (wc_ieq(s, n, "list-style") || wc_ieq(s, n, "list-style-type")) return P_LSTYLE;
    if (wc_ieq(s, n, "width")) return P_WIDTH;
    if (wc_ieq(s, n, "max-width")) return P_MAXW;
    if (wc_ieq(s, n, "text-transform")) return P_TXFORM;
    if (wc_ieq(s, n, "border")) return P_BORDER;
    if (wc_ieq(s, n, "border-top")) return P_BORDER_T;
    if (wc_ieq(s, n, "border-right")) return P_BORDER_R;
    if (wc_ieq(s, n, "border-bottom")) return P_BORDER_B;
    if (wc_ieq(s, n, "border-left")) return P_BORDER_L;
    if (wc_ieq(s, n, "border-color")) return P_BCOLOR;
    if (wc_ieq(s, n, "border-width")) return P_BWIDTH;
    if (wc_ieq(s, n, "border-style")) return P_BSTYLE;
    return P_NONE;
}

 
static const char *wc_css_skip(const char *p, const char *e) {
    for (;;) {
        while (p < e && wc_isspace(*p)) p++;
        if (p + 1 < e && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < e && !(p[0] == '*' && p[1] == '/')) p++;
            p = (p + 1 < e) ? p + 2 : e;
            continue;
        }
        return p;
    }
}

 
static int wc_tok(const char **pq, const char *e, const char **ts, u32 *tl) {
    const char *q = *pq;
    while (q < e && wc_isspace(*q)) q++;
    if (q >= e) { *pq = q; return 0; }
    const char *s0 = q; int depth = 0;
    while (q < e && (depth > 0 || !wc_isspace(*q))) { if (*q == '(') depth++; else if (*q == ')' && depth > 0) depth--; q++; }
    *ts = s0; *tl = (u32)(q - s0); *pq = q;
    return 1;
}
static int wc_istart(const char *s, u32 n, const char *lit) {
    u32 l = wc_slen(lit);
    if (n < l) return 0;
    for (u32 i = 0; i < l; i++) if (wc_lc((u8)s[i]) != wc_lc((u8)lit[i])) return 0;
    return 1;
}
 
static int wc_gradient_color(const char *s, u32 n, wc_rgb *out) {
    u32 i = 0; while (i < n && s[i] != '(') i++;
    i++;
    int depth = 0; u32 st = i;
    for (u32 k = i; k <= n; k++) {
        char ch = k < n ? s[k] : ')';
        if (ch == '(') depth++;
        else if (ch == ')' && depth > 0) depth--;
        else if ((ch == ',' || ch == ')') && depth == 0) {
            const char *a = s + st; u32 al = k > st ? k - st : 0;
            const char *q = a, *ts; u32 tl;
            while (wc_tok(&q, a + al, &ts, &tl)) if (wc_parse_color(ts, tl, out) == 1) return 1;
            st = k + 1;
            if (ch == ')') break;
        }
    }
    return 0;
}
 
static int wc_bg_value(const char *v, u32 n, wc_rgb *out) {
    const char *q = v, *ts; u32 tl;
    while (wc_tok(&q, v + n, &ts, &tl)) {
        if (wc_istart(ts, tl, "linear-gradient(") || wc_istart(ts, tl, "radial-gradient(") ||
            wc_istart(ts, tl, "repeating-linear-gradient(") || wc_istart(ts, tl, "conic-gradient(")) {
            if (wc_gradient_color(ts, tl, out)) return 1;
            continue;
        }
        int r = wc_parse_color(ts, tl, out);
        if (r) return r;
        if (wc_ieq(ts, tl, "none")) return 2;
    }
    return 0;
}
static int wc_has_var(const char *v, u32 n) {
    for (u32 i = 0; i + 4 <= n; i++) if (v[i] == 'v' && wc_ieq(v + i, 4, "var(")) return 1;
    return 0;
}
static int wc_var_lookup(const char *nm, u32 nl, const char **v, u32 *vl) {
    for (int i = wc_var_count - 1; i >= 0; i--) {
        if (wc_vars[i].name.len == nl) {
            const char *a = WCS(wc_vars[i].name); u32 k = 0;
            while (k < nl && a[k] == nm[k]) k++;
            if (k == nl) { *v = WCS(wc_vars[i].val); *vl = wc_vars[i].val.len; return 1; }
        }
    }
    return 0;
}
 
static u32 wc_resolve_vars(const char *v, u32 n, char *out, u32 cap, int depth) {
    u32 o = 0, i = 0;
    while (i < n && o + 1 < cap) {
        if (i + 4 <= n && v[i] == 'v' && wc_ieq(v + i, 4, "var(")) {
            u32 a = i + 4; while (a < n && wc_isspace(v[a])) a++;
            u32 b = a; while (b < n && v[b] != ',' && v[b] != ')') b++;
            u32 ne = b; while (ne > a && wc_isspace(v[ne - 1])) ne--;
            u32 fs = 0, fe = 0, j = b;
            if (j < n && v[j] == ',') {
                fs = j + 1; int dp = 1; j = fs;
                while (j < n) { if (v[j] == '(') dp++; else if (v[j] == ')') { dp--; if (dp == 0) break; } j++; }
                fe = j;
            }
            const char *vv; u32 vl;
            if (wc_var_lookup(v + a, ne - a, &vv, &vl)) {
                if (depth < 4 && wc_has_var(vv, vl)) o += wc_resolve_vars(vv, vl, out + o, cap - o, depth + 1);
                else for (u32 k = 0; k < vl && o + 1 < cap; k++) out[o++] = vv[k];
            } else if (fe > fs) {
                u32 k0 = fs; while (k0 < fe && wc_isspace(v[k0])) k0++;
                if (depth < 4 && wc_has_var(v + k0, fe - k0)) o += wc_resolve_vars(v + k0, fe - k0, out + o, cap - o, depth + 1);
                else for (u32 k = k0; k < fe && o + 1 < cap; k++) out[o++] = v[k];
            }
            i = (j < n) ? j + 1 : n;
        } else out[o++] = v[i++];
    }
    out[o] = 0;
    return o;
}

 

static int wc_parse_decls(const char *p, const char *e, wc_decl *out, int max) {
    int cnt = 0;
    while (p < e && cnt < max) {
        p = wc_css_skip(p, e);
        if (p >= e) break;
        const char *ns = p;
        while (p < e && *p != ':' && *p != ';') p++;
        if (p >= e || *p == ';') { if (p < e) p++; continue; }
        const char *ne = p;
        while (ne > ns && wc_isspace(ne[-1])) ne--;
        p++;                                     
        const char *vs = p;
        int depth = 0;
        while (p < e && (depth > 0 || *p != ';')) {
            if (*p == '(') depth++; else if (*p == ')' && depth > 0) depth--;
            p++;
        }
        const char *ve = p;
        if (p < e) p++;
        while (vs < ve && wc_isspace(*vs)) vs++;
        while (ve > vs && wc_isspace(ve[-1])) ve--;
        int imp = 0;
        if (ve - vs >= 10 && wc_ieq(ve - 10, 10, "!important")) { imp = 1; ve -= 10; while (ve > vs && wc_isspace(ve[-1])) ve--; }
        int prop = wc_prop_id(ns, (u32)(ne - ns));
        if (ne - ns >= 3 && ns[0] == '-' && ns[1] == '-' && ve > vs) {          
            out[cnt].prop = P_VAR; out[cnt].important = 0;
            out[cnt].name.off = (u32)(ns - wc_pool); out[cnt].name.len = (u32)(ne - ns);
            out[cnt].val.off = (u32)(vs - wc_pool); out[cnt].val.len = (u32)(ve - vs);
            cnt++;
            continue;
        }
         
        int is_margin = wc_ieq(ns, (u32)(ne - ns), "margin"), is_pad = wc_ieq(ns, (u32)(ne - ns), "padding");
        if (is_margin || is_pad) {
            const char *tok[4]; u32 tl[4]; int nt = 0;
            const char *q = vs, *ts; u32 tlen;
            while (nt < 4 && wc_tok(&q, ve, &ts, &tlen)) { tok[nt] = ts; tl[nt] = tlen; nt++; }
            if (nt > 0 && cnt + 4 <= max) {
                int t = 0, r = 0, b = 0, l = 0;
                if (nt == 1) { t = r = b = l = 0; }
                else if (nt == 2) { t = 0; r = 1; b = 0; l = 1; }
                else if (nt == 3) { t = 0; r = 1; b = 2; l = 1; }
                else { t = 0; r = 1; b = 2; l = 3; }
                int idx[4] = { t, r, b, l };
                int props[4] = { is_margin ? P_MT : P_PT, is_margin ? P_MR : P_PR, is_margin ? P_MB : P_PB, is_margin ? P_ML : P_PL };
                for (int k = 0; k < 4; k++) {
                    out[cnt].prop = (u8)props[k]; out[cnt].important = (u8)imp;
                    out[cnt].val.off = (u32)(tok[idx[k]] - wc_pool); out[cnt].val.len = tl[idx[k]];
                    cnt++;
                }
            }
            continue;
        }
        if (prop == P_NONE || ve <= vs) continue;
        out[cnt].prop = (u8)prop; out[cnt].important = (u8)imp;
        out[cnt].val.off = (u32)(vs - wc_pool); out[cnt].val.len = (u32)(ve - vs);
        cnt++;
    }
    return cnt;
}

 
static int wc_parse_compound(const char **pp, const char *e, wc_comp *c) {
    const char *p = *pp;
    c->tag = 0; c->any_tag = 1; c->id.off = c->id.len = 0; c->n_cls = 0; c->child = 0;
    int any = 0;
    if (p < e && *p == '*') { p++; any = 1; }
    else if (p < e && wc_isalpha(*p)) {
        const char *ts = p;
        while (p < e && wc_isident(*p)) p++;
        const wc_tagdef *td = wc_find_tag(ts, (u32)(p - ts));
        c->any_tag = 0;
        c->tag = td ? (u8)((td - wc_tags) + 1) : 255;        
        any = 1;
    }
    while (p < e && (*p == '#' || *p == '.')) {
        char kind = *p++;
        const char *ts = p;
        while (p < e && wc_isident(*p)) p++;
        if (p == ts) return 0;
        wstr w = wc_put(ts, (u32)(p - ts));
        if (kind == '#') c->id = w;
        else { if (c->n_cls >= 3) return 0; c->cls[c->n_cls++] = w; }
        any = 1;
    }
    if (p + 5 <= e && p[0] == ':' && wc_ieq(p, 5, ":root")) {           
        { const wc_tagdef *th = wc_find_tag("html", 4); c->any_tag = 0; c->tag = (u8)((th - wc_tags) + 1); p += 5; any = 1; }
    }
    if (p < e && (*p == '[' || *p == ':')) return 0;      
    if (!any) return 0;
    *pp = p;
    return 1;
}

 
static int wc_parse_selector(const char *s, const char *e, wc_rule *r) {
    r->n_comp = 0; r->spec = 0;
    const char *p = s;
    int next_child = 0;
    for (;;) {
        p = wc_css_skip(p, e);
        if (p >= e) break;
        if (*p == '>') { next_child = 1; p++; continue; }
        if (*p == '+' || *p == '~') return 0;
        if (r->n_comp >= 3) return 0;
        wc_comp *c = &r->comp[r->n_comp];
        if (!wc_parse_compound(&p, e, c)) return 0;
        c->child = (u8)next_child; next_child = 0;
        r->spec = (u16)(r->spec + (c->id.len ? 100 : 0) + c->n_cls * 10 + (c->any_tag ? 0 : 1));
        r->n_comp++;
    }
    return r->n_comp > 0;
}

static int wc_css_nest;
static void wc_css_parse(const char *p, const char *e);
 
static int wc_media_feature(const char *s, u32 n) {
    u32 c = 0; while (c < n && s[c] != ':') c++;
    u32 ne = c; while (ne > 0 && wc_isspace(s[ne - 1])) ne--;
    u32 vs = c < n ? c + 1 : n; while (vs < n && wc_isspace(s[vs])) vs++;
    u32 ve = n; while (ve > vs && wc_isspace(s[ve - 1])) ve--;
    const char *nm = s; const char *v = s + vs; u32 vl = ve - vs; int px;
    if (wc_ieq(nm, ne, "min-width")) return wc_parse_len(v, vl, 16, &px) && wc_vw >= px;
    if (wc_ieq(nm, ne, "max-width")) return wc_parse_len(v, vl, 16, &px) && wc_vw <= px;
    if (wc_ieq(nm, ne, "width")) return wc_parse_len(v, vl, 16, &px) && wc_vw == px;
    if (wc_ieq(nm, ne, "min-height")) return wc_parse_len(v, vl, 16, &px) && 800 >= px;
    if (wc_ieq(nm, ne, "max-height")) return wc_parse_len(v, vl, 16, &px) && 800 <= px;
    if (wc_ieq(nm, ne, "prefers-color-scheme")) return wc_ieq(v, vl, "light");
    if (wc_ieq(nm, ne, "prefers-reduced-motion")) return wc_ieq(v, vl, "no-preference");
    if (wc_ieq(nm, ne, "hover") || wc_ieq(nm, ne, "any-hover")) return wc_ieq(v, vl, "hover");
    if (wc_ieq(nm, ne, "pointer") || wc_ieq(nm, ne, "any-pointer")) return wc_ieq(v, vl, "fine");
    if (wc_ieq(nm, ne, "orientation")) return wc_ieq(v, vl, "landscape");
    return 0;
}
static int wc_media_query(const char *s, u32 n) {
    int neg = 0, ok = 1;
    const char *q = s, *ts; u32 tl;
    while (wc_tok(&q, s + n, &ts, &tl)) {
        if (wc_ieq(ts, tl, "not")) { neg = 1; continue; }
        if (wc_ieq(ts, tl, "only") || wc_ieq(ts, tl, "and") || wc_ieq(ts, tl, "screen") || wc_ieq(ts, tl, "all")) continue;
        if (ts[0] == '(' && tl >= 2) { if (!wc_media_feature(ts + 1, tl - 2)) ok = 0; continue; }
        ok = 0;                                    
    }
    return neg ? !ok : ok;
}
static int wc_media_matches(const char *s, u32 n) {
    while (n && wc_isspace(*s)) { s++; n--; }
    if (n == 0) return 1;
    u32 st = 0; int depth = 0;
    for (u32 i = 0; i <= n; i++) {
        char ch = i < n ? s[i] : ',';
        if (ch == '(') depth++; else if (ch == ')' && depth > 0) depth--;
        else if (ch == ',' && depth == 0) { if (wc_media_query(s + st, i - st)) return 1; st = i + 1; }
    }
    return 0;
}

static void wc_css_parse(const char *p, const char *e) {
    while (p < e) {
        p = wc_css_skip(p, e);
        if (p >= e) break;
        if (*p == '@') {
            const char *at = p;
            while (p < e && *p != '{' && *p != ';') p++;
            const char *prelude_end = p;
            if (p < e && *p == '{') {
                const char *bs0 = p + 1; int depth = 0;
                while (p < e) { if (*p == '{') depth++; else if (*p == '}') { depth--; if (depth == 0) break; } p++; }
                const char *be0 = p;
                if (p < e) p++;
                 

                int take = 0;
                if (wc_istart_(at, (u32)(prelude_end - at), "@media")) take = wc_media_matches(at + 6, (u32)(prelude_end - at - 6));
                else if (wc_istart_(at, (u32)(prelude_end - at), "@supports") || wc_istart_(at, (u32)(prelude_end - at), "@layer")) take = 1;
                if (take && wc_css_nest < 4) { wc_css_nest++; wc_css_parse(bs0, be0); wc_css_nest--; }
            } else if (p < e) p++;
            continue;
        }
        const char *ss = p;
        while (p < e && *p != '{') p++;
        if (p >= e) break;
        const char *se = p;
        p++;
        const char *bs = p;
        while (p < e && *p != '}') p++;
        const char *be = p;
        if (p < e) p++;
        if (wc_decl_count >= WC_MAX_DECLS - 8) continue;
        int dstart = wc_decl_count;
        int nd = wc_parse_decls(bs, be, &wc_decls[wc_decl_count], WC_MAX_DECLS - wc_decl_count);
        wc_decl_count += nd;
        if (nd == 0) continue;
         
        int added = 0;
        const char *q = ss;
        while (q < se) {
            const char *ts = q;
            while (q < se && *q != ',') q++;
            const char *te = q;
            if (q < se) q++;
            if (wc_rule_count >= WC_MAX_RULES) break;
            wc_rule *r = &wc_rules[wc_rule_count];
            if (wc_parse_selector(ts, te, r)) {
                r->order = (u16)wc_rule_count;
                r->decl_start = (u16)dstart; r->decl_count = (u16)nd;
                wc_rule_count++;
                added++;
            }
        }
         

        if (added) for (int d = 0; d < nd; d++) {
            wc_decl *dd = &wc_decls[dstart + d];
            if (dd->prop == P_VAR && wc_var_count < WC_MAX_VARS) { wc_vars[wc_var_count].name = dd->name; wc_vars[wc_var_count].val = dd->val; wc_var_count++; }
        }
    }
}

 
static int wc_has_class(const wc_node *n, wstr cls) {
    if (n->cls.len == 0 || cls.len == 0) return 0;
    const char *s = WCS(n->cls); u32 sl = n->cls.len;
    const char *c = WCS(cls); u32 cl = cls.len;
    u32 i = 0;
    while (i < sl) {
        while (i < sl && wc_isspace(s[i])) i++;
        u32 st = i;
        while (i < sl && !wc_isspace(s[i])) i++;
        if (i - st == cl) { u32 k = 0; while (k < cl && s[st + k] == c[k]) k++; if (k == cl) return 1; }
    }
    return 0;
}
static int wc_comp_match(const wc_comp *c, int node) {
    const wc_node *n = &wc_nodes[node];
    if (n->type != 1) return 0;
    if (!c->any_tag) {
        if (c->tag == 255) return 0;
        if (n->tn != c->tag) return 0;
    }
    if (c->id.len) {
        if (n->id.len != c->id.len) return 0;
        const char *a = WCS(n->id), *b = WCS(c->id);
        for (u32 i = 0; i < c->id.len; i++) if (a[i] != b[i]) return 0;
    }
    for (int i = 0; i < c->n_cls; i++) if (!wc_has_class(n, c->cls[i])) return 0;
    return 1;
}
static int wc_match_from(const wc_rule *r, int ci, int node) {
    if (!wc_comp_match(&r->comp[ci], node)) return 0;
    if (ci == 0) return 1;
    int child_only = r->comp[ci].child;
    int p = wc_nodes[node].parent;
    while (p >= 0) {
        if (wc_match_from(r, ci - 1, p)) return 1;
        if (child_only) return 0;
        p = wc_nodes[p].parent;
    }
    return 0;
}
static int wc_rule_matches(const wc_rule *r, int node) { return wc_match_from(r, r->n_comp - 1, node); }

 
enum { D_INLINE = 0, D_BLOCK, D_NONE, D_LISTITEM, D_CELL };
enum { AL_LEFT = 0, AL_CENTER, AL_RIGHT };
typedef struct {
    u8  color[3], bg[3];
    u8  has_bg, bold, underline, strike, ital, pre, align, display, hidden, lstyle;
    u16 fs;                        
    short mt, mb, ml, pl, pt, pb;
    short mr, pr;                  
    short width, maxw;             
    u8  wpct, maxpct;              
    u8  ml_auto, mr_auto;          
    u8  bw[4];                     
    u8  bc[3];                     
    u8  xform;                     
} wc_style;
static wc_style wc_cs[WC_MAX_NODES];

static void wc_apply_border(wc_style *st, int sides, const char *v, u32 n) {
    int w = -1, hasc = 0, none = 0; wc_rgb c; c.r = c.g = c.b = 0;
    const char *q = v, *ts; u32 tl; int px;
    while (wc_tok(&q, v + n, &ts, &tl)) {
        if (wc_ieq(ts, tl, "none") || wc_ieq(ts, tl, "hidden")) { none = 1; continue; }
        if (wc_ieq(ts, tl, "solid") || wc_ieq(ts, tl, "dashed") || wc_ieq(ts, tl, "dotted") || wc_ieq(ts, tl, "double") ||
            wc_ieq(ts, tl, "groove") || wc_ieq(ts, tl, "ridge") || wc_ieq(ts, tl, "inset") || wc_ieq(ts, tl, "outset")) continue;
        if (wc_ieq(ts, tl, "thin")) { w = 1; continue; }
        if (wc_ieq(ts, tl, "medium")) { w = 2; continue; }
        if (wc_ieq(ts, tl, "thick")) { w = 4; continue; }
        if (wc_parse_color(ts, tl, &c) == 1) { hasc = 1; continue; }
        if (wc_parse_len(ts, tl, st->fs, &px) && px >= 0) { w = px; continue; }
    }
    if (none) w = 0; else if (w < 0) w = 1;
    if (w > 6) w = 6;
    for (int i = 0; i < 4; i++) if (sides & (1 << i)) st->bw[i] = (u8)w;
    if (hasc) { st->bc[0] = c.r; st->bc[1] = c.g; st->bc[2] = c.b; }
    else if (w > 0) { st->bc[0] = st->color[0]; st->bc[1] = st->color[1]; st->bc[2] = st->color[2]; }
}

static void wc_apply_decl(wc_style *st, const wc_decl *d, const wc_style *parent) {
    const char *v = wc_pool + d->val.off; u32 n = d->val.len;
    char rbuf[320];
    if (wc_has_var(v, n)) { n = wc_resolve_vars(v, n, rbuf, sizeof(rbuf), 0); v = rbuf; }
    wc_rgb c; int px;
    switch (d->prop) {
    case P_COLOR: {
        int r = wc_parse_color(v, n, &c);
        if (r == 1) { st->color[0] = c.r; st->color[1] = c.g; st->color[2] = c.b; }
        else if (wc_ieq(v, n, "inherit")) { st->color[0] = parent->color[0]; st->color[1] = parent->color[1]; st->color[2] = parent->color[2]; }
        break; }
    case P_BG: {
        int r = wc_bg_value(v, n, &c);
        if (r == 1) { st->has_bg = 1; st->bg[0] = c.r; st->bg[1] = c.g; st->bg[2] = c.b; }
        else if (r == 2) st->has_bg = 0;
        break; }
    case P_VAR: break;
    case P_TXFORM:
        if (wc_ieq(v, n, "uppercase")) st->xform = 1; else if (wc_ieq(v, n, "lowercase")) st->xform = 2;
        else if (wc_ieq(v, n, "capitalize")) st->xform = 3; else if (wc_ieq(v, n, "none")) st->xform = 0;
        break;
    case P_WIDTH: case P_MAXW: {
        int is_w = (d->prop == P_WIDTH);
        short *dst = is_w ? &st->width : &st->maxw; u8 *dp = is_w ? &st->wpct : &st->maxpct;
        int num;
        if (wc_ieq(v, n, "auto") || wc_ieq(v, n, "none") || wc_ieq(v, n, "initial")) { *dst = -1; *dp = 0; }
        else if (n > 0 && v[n - 1] == '%' && wc_parse_num100(v, n, &num) > 0) { *dp = (u8)(num / 100 > 100 ? 100 : num / 100); *dst = 0; }
        else if (wc_parse_len(v, n, st->fs, &px) && px >= 0) { *dst = (short)(px > 4000 ? 4000 : px); *dp = 0; }
        break; }
    case P_BORDER: wc_apply_border(st, 15, v, n); break;
    case P_BORDER_T: wc_apply_border(st, 1, v, n); break;
    case P_BORDER_R: wc_apply_border(st, 2, v, n); break;
    case P_BORDER_B: wc_apply_border(st, 4, v, n); break;
    case P_BORDER_L: wc_apply_border(st, 8, v, n); break;
    case P_BCOLOR: if (wc_parse_color(v, n, &c) == 1) { st->bc[0] = c.r; st->bc[1] = c.g; st->bc[2] = c.b; } break;
    case P_BWIDTH: if (wc_parse_len(v, n, st->fs, &px) && px >= 0) for (int i = 0; i < 4; i++) st->bw[i] = (u8)(px > 6 ? 6 : px); break;
    case P_BSTYLE: if (wc_ieq(v, n, "none") || wc_ieq(v, n, "hidden")) for (int i = 0; i < 4; i++) st->bw[i] = 0; break;
    case P_FSIZE: {
        int base = parent->fs;
        if (wc_ieq(v, n, "xx-small")) st->fs = 9; else if (wc_ieq(v, n, "x-small")) st->fs = 10;
        else if (wc_ieq(v, n, "small")) st->fs = 13; else if (wc_ieq(v, n, "medium")) st->fs = 16;
        else if (wc_ieq(v, n, "large")) st->fs = 18; else if (wc_ieq(v, n, "x-large")) st->fs = 24;
        else if (wc_ieq(v, n, "xx-large")) st->fs = 32; else if (wc_ieq(v, n, "smaller")) st->fs = (u16)(base * 5 / 6);
        else if (wc_ieq(v, n, "larger")) st->fs = (u16)(base * 6 / 5);
        else if (wc_parse_len(v, n, base, &px) && px > 0) st->fs = (u16)(px > 200 ? 200 : px);
        break; }
    case P_FWEIGHT: {
        int num;
        if (wc_ieq(v, n, "bold") || wc_ieq(v, n, "bolder")) st->bold = 1;
        else if (wc_ieq(v, n, "normal") || wc_ieq(v, n, "lighter")) st->bold = 0;
        else if (wc_parse_num100(v, n, &num) > 0) st->bold = (num >= 60000) ? 1 : 0;
        break; }
    case P_FSTYLE: if (wc_ieq(v, n, "italic") || wc_ieq(v, n, "oblique")) st->ital = 1; else if (wc_ieq(v, n, "normal")) st->ital = 0; break;
    case P_TALIGN:
        if (wc_ieq(v, n, "center")) st->align = AL_CENTER; else if (wc_ieq(v, n, "right") || wc_ieq(v, n, "end")) st->align = AL_RIGHT;
        else if (wc_ieq(v, n, "left") || wc_ieq(v, n, "start")) st->align = AL_LEFT;
        break;
    case P_TDECO:
        if (wc_ieq(v, n, "none")) { st->underline = 0; st->strike = 0; }
        else { u32 i = 0; while (i < n) { while (i < n && wc_isspace(v[i])) i++; u32 s0 = i; while (i < n && !wc_isspace(v[i])) i++;
                   if (wc_ieq(v + s0, i - s0, "underline")) st->underline = 1; else if (wc_ieq(v + s0, i - s0, "line-through")) st->strike = 1; } }
        break;
    case P_DISPLAY:
        if (wc_ieq(v, n, "none")) st->display = D_NONE;
        else if (wc_ieq(v, n, "block") || wc_ieq(v, n, "flex") || wc_ieq(v, n, "grid") || wc_ieq(v, n, "table") || wc_ieq(v, n, "flow-root")) st->display = D_BLOCK;
        else if (wc_ieq(v, n, "inline") || wc_ieq(v, n, "inline-block") || wc_ieq(v, n, "inline-flex")) st->display = D_INLINE;
        else if (wc_ieq(v, n, "list-item")) st->display = D_LISTITEM;
        else if (wc_ieq(v, n, "table-cell")) st->display = D_CELL;
        else if (wc_ieq(v, n, "table-row")) st->display = D_BLOCK;
        break;
    case P_MT: if (wc_parse_len(v, n, st->fs, &px)) st->mt = (short)(px > 200 ? 200 : px < -50 ? -50 : px); break;
    case P_MB: if (wc_parse_len(v, n, st->fs, &px)) st->mb = (short)(px > 200 ? 200 : px < -50 ? -50 : px); break;
    case P_ML: if (wc_ieq(v, n, "auto")) { st->ml_auto = 1; st->ml = 0; } else if (wc_parse_len(v, n, st->fs, &px)) { st->ml_auto = 0; st->ml = (short)(px > 400 ? 400 : px < 0 ? 0 : px); } break;
    case P_MR: if (wc_ieq(v, n, "auto")) { st->mr_auto = 1; st->mr = 0; } else if (wc_parse_len(v, n, st->fs, &px)) { st->mr_auto = 0; st->mr = (short)(px > 400 ? 400 : px < 0 ? 0 : px); } break;
    case P_PR: if (wc_parse_len(v, n, st->fs, &px)) st->pr = (short)(px > 400 ? 400 : px < 0 ? 0 : px); break;
    case P_PT: if (wc_parse_len(v, n, st->fs, &px)) st->pt = (short)(px > 200 ? 200 : px < 0 ? 0 : px); break;
    case P_PB: if (wc_parse_len(v, n, st->fs, &px)) st->pb = (short)(px > 200 ? 200 : px < 0 ? 0 : px); break;
    case P_PL: if (wc_parse_len(v, n, st->fs, &px)) st->pl = (short)(px > 400 ? 400 : px < 0 ? 0 : px); break;
    case P_WS: if (wc_ieq(v, n, "pre") || wc_ieq(v, n, "pre-wrap") || wc_ieq(v, n, "pre-line")) st->pre = 1; else if (wc_ieq(v, n, "normal") || wc_ieq(v, n, "nowrap")) st->pre = 0; break;
    case P_VIS: st->hidden = (wc_ieq(v, n, "hidden") || wc_ieq(v, n, "collapse")) ? 1 : 0; break;
    case P_LSTYLE: if (wc_ieq(v, n, "none")) st->lstyle = 2; else if (wc_ieq(v, n, "decimal")) st->lstyle = 1; else if (wc_ieq(v, n, "disc") || wc_ieq(v, n, "circle") || wc_ieq(v, n, "square")) st->lstyle = 0; break;
    default: break;
    }
}

 
static void wc_ua_style(wc_style *st, int tag, int flags, const wc_style *par, int node) {
    st->display = (flags & TF_BLOCK) ? D_BLOCK : D_INLINE;
    st->has_bg = 0; st->mt = st->mb = st->ml = st->pl = st->pt = st->pb = 0;
    st->mr = st->pr = 0; st->width = st->maxw = -1; st->wpct = st->maxpct = 0; st->ml_auto = st->mr_auto = 0;
    st->bw[0] = st->bw[1] = st->bw[2] = st->bw[3] = 0;
    int fs = par->fs;
    switch (tag) {
    case TG_HEAD: case TG_TITLE: case TG_STYLE: case TG_SCRIPT: case TG_META: case TG_LINK: case TG_HIDDEN:
        st->display = D_NONE; break;
    case TG_NOSCRIPT: if (wc_js_enabled) st->display = D_NONE; break;
    case TG_BODY: st->mt = 8; st->mb = 8; st->ml = 8; break;
    case TG_P: st->mt = 16; st->mb = 16; break;
    case TG_H1: st->bold = 1; st->fs = 32; st->mt = 21; st->mb = 21; break;
    case TG_H2: st->bold = 1; st->fs = 24; st->mt = 19; st->mb = 19; break;
    case TG_H3: st->bold = 1; st->fs = 19; st->mt = 18; st->mb = 18; break;
    case TG_H4: st->bold = 1; st->mt = 21; st->mb = 21; break;
    case TG_H5: st->bold = 1; st->fs = 13; st->mt = 22; st->mb = 22; break;
    case TG_H6: st->bold = 1; st->fs = 11; st->mt = 24; st->mb = 24; break;
    case TG_UL: case TG_OL: st->mt = 16; st->mb = 16; st->pl = 24; if (par->display == D_LISTITEM) {} break;
    case TG_LI: st->display = D_LISTITEM; break;
    case TG_PRE: st->pre = 1; st->mt = 16; st->mb = 16; break;
    case TG_BLOCKQUOTE: st->mt = 16; st->mb = 16; st->ml = 24; break;
    case TG_DD: st->ml = 24; break;
    case TG_B: st->bold = 1; break;
    case TG_I: st->ital = 1; break;
    case TG_U: st->underline = 1; break;
    case TG_S: st->strike = 1; break;
    case TG_A: if (wc_nodes[node].href.len) { st->color[0] = 0; st->color[1] = 0; st->color[2] = 200; st->underline = 1; } break;
    case TG_MARK: st->has_bg = 1; st->bg[0] = 255; st->bg[1] = 255; st->bg[2] = 0; break;
    case TG_CENTER: st->align = AL_CENTER; break;
    case TG_SMALL: st->fs = (u16)(fs * 5 / 6); break;
    case TG_BIG: st->fs = (u16)(fs * 6 / 5); break;
    case TG_TH: st->bold = 1; st->align = AL_CENTER; st->display = D_CELL; break;
    case TG_TD: st->display = D_CELL; break;
    case TG_TABLE: st->mt = 2; st->mb = 2; break;
    case TG_BUTTON: case TG_INPUT: st->has_bg = 1; st->bg[0] = 225; st->bg[1] = 225; st->bg[2] = 230; break;
    default: break;
    }
}

static void wc_compute_node(int node, int parent_idx) {
    wc_node *n = &wc_nodes[node];
    wc_style *st = &wc_cs[node];
    const wc_style *par = &wc_cs[parent_idx];
     
    *st = *par;
    wc_ua_style(st, n->tag, n->flags, par, node);

     
    int match[96]; int nm = 0;
    for (int ri = 0; ri < wc_rule_count && nm < 96; ri++) {
        const wc_rule *r = &wc_rules[ri];
        if (wc_rule_matches(r, node)) {
            int k = nm++;
            while (k > 0) {
                const wc_rule *pr = &wc_rules[match[k - 1]];
                if (pr->spec > r->spec || (pr->spec == r->spec && pr->order > r->order)) { match[k] = match[k - 1]; k--; }
                else break;
            }
            match[k] = ri;
        }
    }
    for (int m = 0; m < nm; m++) {
        const wc_rule *r = &wc_rules[match[m]];
        for (int d = 0; d < r->decl_count; d++) {
            const wc_decl *dd = &wc_decls[r->decl_start + d];
            if (!dd->important) wc_apply_decl(st, dd, par);
        }
    }
    if (n->style.len) {
        wc_decl tmp[40];
        int nd = wc_parse_decls(WCS(n->style), WCS(n->style) + n->style.len, tmp, 40);
        if (n->tag == TG_HTML || n->tag == TG_BODY)            
            for (int d = 0; d < nd; d++) if (tmp[d].prop == P_VAR && wc_var_count < WC_MAX_VARS) { wc_vars[wc_var_count].name = tmp[d].name; wc_vars[wc_var_count].val = tmp[d].val; wc_var_count++; }
        for (int d = 0; d < nd; d++) if (!tmp[d].important) wc_apply_decl(st, &tmp[d], par);
        for (int m = 0; m < nm; m++) {
            const wc_rule *r = &wc_rules[match[m]];
            for (int d = 0; d < r->decl_count; d++) {
                const wc_decl *dd = &wc_decls[r->decl_start + d];
                if (dd->important) wc_apply_decl(st, dd, par);
            }
        }
        for (int d = 0; d < nd; d++) if (tmp[d].important) wc_apply_decl(st, &tmp[d], par);
    } else {
        for (int m = 0; m < nm; m++) {
            const wc_rule *r = &wc_rules[match[m]];
            for (int d = 0; d < r->decl_count; d++) {
                const wc_decl *dd = &wc_decls[r->decl_start + d];
                if (dd->important) wc_apply_decl(st, dd, par);
            }
        }
    }
    if (st->display == D_LISTITEM) {   }
    if (n->tag == TG_HTML && st->fs >= 8 && st->fs <= 64) wc_rem_px = st->fs;       
}

 
static void wc_compute_all(void) {
    wc_style *root = &wc_cs[0];
    wc_rem_px = 16;
    root->color[0] = 20; root->color[1] = 20; root->color[2] = 24;
    root->bg[0] = root->bg[1] = root->bg[2] = 250; root->has_bg = 0;
    root->bold = root->underline = root->strike = root->ital = root->pre = 0;
    root->align = AL_LEFT; root->display = D_BLOCK; root->hidden = 0; root->lstyle = 0;
    root->fs = 16; root->mt = root->mb = root->ml = root->pl = root->pt = root->pb = 0;
    root->mr = root->pr = 0; root->width = root->maxw = -1; root->wpct = root->maxpct = 0; root->ml_auto = root->mr_auto = 0;
    root->bw[0] = root->bw[1] = root->bw[2] = root->bw[3] = 0; root->xform = 0;
     
    for (int i = 1; i < wc_node_count; i++) {
        if (wc_nodes[i].type != 1) continue;
        int p = wc_nodes[i].parent; if (p < 0) p = 0;
         
        wc_compute_node(i, p);
    }
}

 
#define WCF_BOLD   1
#define WCF_UL     2
#define WCF_STRIKE 4
#define WCF_ITAL   8
typedef struct {
    int  x;                
    u32  toff;             
    u16  len;
    u8   scale, fg[3], has_bg, bg[3], flags;
    int  node;             
} wc_seg;
typedef struct { int y; u16 h; u32 seg_start; u16 seg_count; u8 kind; int x0, x1; } wc_line;    
typedef struct { int y0, y1, x0, x1; u8 bg[3]; u8 has_bg; u8 bw[4]; u8 bc[3]; } wc_box;

static wc_seg  wc_segs[WC_MAX_SEGS];   static int wc_seg_count;
static wc_line wc_lines[WC_MAX_LINES]; static int wc_line_count;
static wc_box  wc_boxes[WC_MAX_BOXES]; static int wc_box_count;
static char    wc_ltext[WC_LTEXT_SIZE]; static u32 wc_ltext_len;
static int     wc_doc_h;
static wc_rgb  wc_page_bg;

static int L_w, L_y, L_pending, L_indent, L_x, L_line_h, L_line_seg0, L_align, L_pend_space, L_right;
static int L_marker_mode;    
#define L_RMARGIN 8

static char wc_xform_ch(char c, int mode, char prev) {
    if (mode == 1 && c >= 'a' && c <= 'z') return (char)(c - 32);
    if (mode == 2 && c >= 'A' && c <= 'Z') return (char)(c + 32);
    if (mode == 3 && c >= 'a' && c <= 'z' && (prev == ' ' || prev == '-' || prev == '(')) return (char)(c - 32);
    return c;
}
static int wc_scale(int fs) { return fs < 21 ? 1 : (fs < 30 ? 2 : 3); }
static int L_line_empty(void) { return wc_seg_count == L_line_seg0; }
static void L_apply_pending(void) { if (L_pending > 0) L_y += L_pending; L_pending = 0; }

static void L_flush(int force) {
    int n = wc_seg_count - L_line_seg0;
    if (n == 0 && !force) return;
    if (wc_line_count >= WC_MAX_LINES) { L_line_seg0 = wc_seg_count; L_line_h = 0; L_x = L_indent; return; }
    if (n == 0) L_apply_pending();
    int h = L_line_h ? L_line_h : 12;
    if (n > 0 && L_align != AL_LEFT) {
        wc_seg *last = &wc_segs[wc_seg_count - 1];
        int end = last->x + last->len * 8 * last->scale;
        int width = end - L_indent;
        int avail = L_right - L_indent;
        int shift = (L_align == AL_CENTER) ? (avail - width) / 2 : (avail - width);
        if (shift > 0) for (int i = L_line_seg0; i < wc_seg_count; i++) wc_segs[i].x += shift;
    }
    wc_line *ln = &wc_lines[wc_line_count++];
    ln->y = L_y; ln->h = (u16)h; ln->seg_start = (u32)L_line_seg0; ln->seg_count = (u16)n; ln->kind = 0; ln->x0 = ln->x1 = 0;
    L_y += h;
    L_line_seg0 = wc_seg_count; L_line_h = 0; L_x = L_indent;
}

static int wc_seg_flags(const wc_style *st) {
    return (st->bold ? WCF_BOLD : 0) | (st->underline ? WCF_UL : 0) | (st->strike ? WCF_STRIKE : 0) | (st->ital ? WCF_ITAL : 0);
}

 
static void L_emit(const char *s, u32 n, int sp, const wc_style *st, int node) {
    if (wc_seg_count >= WC_MAX_SEGS || wc_ltext_len + n + 2 > WC_LTEXT_SIZE) return;
    int scale = wc_scale(st->fs), cw = 8 * scale;
    if (L_line_empty()) { L_apply_pending(); if (!L_marker_mode && L_x < L_indent) L_x = L_indent; sp = 0; }
    u32 total = n + (sp ? 1u : 0u);
    int fl = wc_seg_flags(st);
    wc_seg *last = (wc_seg_count > L_line_seg0) ? &wc_segs[wc_seg_count - 1] : NULL;
    if (last && last->node == node && last->scale == scale && last->flags == fl &&
        last->has_bg == st->has_bg && last->fg[0] == st->color[0] && last->fg[1] == st->color[1] && last->fg[2] == st->color[2] &&
        (!st->has_bg || (last->bg[0] == st->bg[0] && last->bg[1] == st->bg[1] && last->bg[2] == st->bg[2])) &&
        last->toff + last->len == wc_ltext_len && last->x + last->len * cw == L_x && last->len + total < 60000) {
        if (sp) wc_ltext[wc_ltext_len++] = ' ';
        for (u32 i = 0; i < n; i++) {
            char pc = (i == 0) ? (sp ? ' ' : (wc_ltext_len > 0 ? wc_ltext[wc_ltext_len - 1] : ' ')) : s[i - 1];
            char ch = wc_xform_ch(s[i], st->xform, pc);
            wc_ltext[wc_ltext_len++] = ch;
        }
        last->len = (u16)(last->len + total);
    } else {
        wc_seg *sg = &wc_segs[wc_seg_count++];
        sg->x = L_x; sg->toff = wc_ltext_len; sg->len = (u16)total; sg->scale = (u8)scale; sg->flags = (u8)fl;
        sg->fg[0] = st->color[0]; sg->fg[1] = st->color[1]; sg->fg[2] = st->color[2];
        sg->has_bg = st->has_bg; sg->bg[0] = st->bg[0]; sg->bg[1] = st->bg[1]; sg->bg[2] = st->bg[2];
        sg->node = node;
        if (sp) wc_ltext[wc_ltext_len++] = ' ';
        for (u32 i = 0; i < n; i++) {
            char ch = wc_xform_ch(s[i], st->xform, i == 0 ? ' ' : s[i - 1]);
            wc_ltext[wc_ltext_len++] = ch;
        }
    }
    L_x += (int)total * cw;
    int lh = scale * 8 + 4;
    if (lh > L_line_h) L_line_h = lh;
}

static void L_word(const char *w, u32 n, const wc_style *st, int node) {
    int cw = 8 * wc_scale(st->fs);
    int right = L_right;
    while (n > 0) {
        int sp = (!L_line_empty() && L_pend_space) ? 1 : 0;
        int need = (sp + (int)n) * cw;
        if (!L_line_empty() && L_x + need > right) { L_flush(0); sp = 0; need = (int)n * cw; }
        if (L_line_empty() && L_x < L_indent) L_x = L_indent;
        int room = (right - (L_line_empty() ? L_indent : L_x));
        if (L_line_empty() && need > room && room >= cw) {
            u32 fit = (u32)(room / cw);
            if (fit == 0) fit = 1;
            if (fit > n) fit = n;
            L_emit(w, fit, 0, st, node);
            L_flush(0);
            w += fit; n -= fit; L_pend_space = 0;
            continue;
        }
        L_emit(w, n, sp, st, node);
        L_pend_space = 0;
        return;
    }
}

static void L_text(const char *s, u32 n, const wc_style *st, int node) {
    if (st->pre) {
        u32 i = 0;
        while (i < n) {
            if (s[i] == '\n') { L_flush(1); i++; continue; }
            if (s[i] == '\r') { i++; continue; }
            u32 j = i;
            static char buf[512]; u32 bl = 0;
            while (j < n && s[j] != '\n' && s[j] != '\r' && bl < 500) {
                if (s[j] == '\t') { for (int t = 0; t < 4 && bl < 500; t++) buf[bl++] = ' '; } else buf[bl++] = s[j];
                j++;
            }
            int cw = 8 * wc_scale(st->fs), right = L_right;
            u32 off = 0;
            while (off < bl) {
                int room = right - (L_line_empty() ? L_indent : L_x);
                u32 fit = (u32)(room / cw); if (fit == 0) fit = 1;
                if (fit > bl - off) fit = bl - off;
                if (!L_line_empty() && L_x + (int)fit * cw > right && fit == (u32)(room / cw)) { L_flush(0); continue; }
                L_emit(buf + off, fit, 0, st, node);
                off += fit;
                if (off < bl) L_flush(0);
            }
            i = j;
        }
        return;
    }
    u32 i = 0;
    while (i < n) {
        while (i < n && wc_isspace((u8)s[i])) { L_pend_space = 1; i++; }
        if (i >= n) break;
        u32 ws = i;
        while (i < n && !wc_isspace((u8)s[i])) i++;
        L_word(s + ws, i - ws, st, node);
    }
}

static void L_add_box(int y0, int x0, int x1, const wc_style *st, int *idx) {
    if (wc_box_count >= WC_MAX_BOXES) { *idx = -1; return; }
    wc_box *b = &wc_boxes[wc_box_count];
    b->y0 = y0; b->y1 = y0; b->x0 = x0; b->x1 = x1;
    b->has_bg = st->has_bg; b->bg[0] = st->bg[0]; b->bg[1] = st->bg[1]; b->bg[2] = st->bg[2];
    for (int i = 0; i < 4; i++) b->bw[i] = st->bw[i];
    b->bc[0] = st->bc[0]; b->bc[1] = st->bc[1]; b->bc[2] = st->bc[2];
    *idx = wc_box_count++;
}

static int wc_li_number(int node) {
    int cnt = 1;
    int p = wc_nodes[node].parent;
    if (p < 0) return 1;
    for (int c = wc_nodes[p].first; c >= 0 && c != node; c = wc_nodes[c].next)
        if (wc_nodes[c].type == 1 && wc_nodes[c].tag == TG_LI) cnt++;
    return cnt;
}

static void wc_lay_node(int node, int depth) {
    wc_node *n = &wc_nodes[node];
    if (n->type == 2) {
        int pi = n->parent < 0 ? 0 : n->parent;
        const wc_style *ps = &wc_cs[pi];
        if (ps->hidden || ps->display == D_NONE) return;
        L_text(WCS(n->text), n->text.len, ps, node);
        return;
    }
    if (n->type != 1) return;
    const wc_style *st = &wc_cs[node];
    if (st->display == D_NONE || depth > WC_MAX_DEPTH) return;
    int tag = n->tag;

    if (tag == TG_BR) { L_flush(1); return; }
    if (tag == TG_HR) {
        L_flush(0);
        if (L_pending < 8) L_pending = 8;
        L_apply_pending();
        if (wc_line_count < WC_MAX_LINES) {
            wc_line *ln = &wc_lines[wc_line_count++];
            ln->y = L_y; ln->h = 10; ln->seg_start = (u32)wc_seg_count; ln->seg_count = 0; ln->kind = 1;
            ln->x0 = L_indent; ln->x1 = L_right;
            L_y += 10;
        }
        L_pending = 8;
        return;
    }
    if (tag == TG_IMG) {
        static char b[200]; u32 bl = 0;
        bl = wc_emit(b, 190, bl, "[IMG");
        if (n->aux.len) { b[bl++] = ' '; u32 k = 0; while (k < n->aux.len && bl < 190) b[bl++] = WCS(n->aux)[k++]; }
        b[bl++] = ']';
        L_text(b, bl, st, node);
        return;
    }
    if (tag == TG_INPUT) {
        if (n->flags & 0x10) return;                        
        static char b[200]; u32 bl = 0;
        b[bl++] = '[';
        if (n->aux.len) { u32 k = 0; while (k < n->aux.len && bl < 190) b[bl++] = WCS(n->aux)[k++]; }
        else { for (int k = 0; k < 8; k++) b[bl++] = '_'; }
        b[bl++] = ']';
        wc_style s2 = *st;                                  
        for (u32 k = 1; k + 1 < bl; k++) if (b[k] == ' ') b[k] = '_';
        L_text(b, bl, &s2, node);
        return;
    }

    int block = (st->display == D_BLOCK || st->display == D_LISTITEM);
    int box = -1;
    int old_indent = L_indent, old_align = L_align, old_right = L_right;
    int is_root_tag = (tag == TG_BODY || tag == TG_HTML);
    if (block) {
        L_flush(0);
        if (st->mt > L_pending) L_pending = st->mt;
        int has_border = (st->bw[0] | st->bw[1] | st->bw[2] | st->bw[3]) != 0;
        int need_box = (st->has_bg && !is_root_tag) || has_border;
         
        int avail = L_right - L_indent - st->ml - st->mr;
        if (avail < 40) avail = 40;
        int boxw = avail;
        if (st->wpct) boxw = avail * st->wpct / 100; else if (st->width >= 0) boxw = st->width;
        if (st->maxpct) { int lim = avail * st->maxpct / 100; if (boxw > lim) boxw = lim; }
        else if (st->maxw >= 0 && boxw > st->maxw) boxw = st->maxw;
        if (boxw > avail) boxw = avail;
        if (boxw < 24) boxw = 24;
        int off = 0;
        if (st->ml_auto && st->mr_auto) off = (avail - boxw) / 2; else if (st->ml_auto) off = avail - boxw;
        int box_left = L_indent + st->ml + off, box_right = box_left + boxw;
        if (need_box || st->pt > 0 || st->bw[0] > 0) {
            L_apply_pending();
            if (need_box) L_add_box(L_y, box_left, box_right, st, &box);
            L_y += st->pt + st->bw[0];
        }
        L_indent = box_left + st->bw[3] + st->pl;
        L_right = box_right - st->bw[1] - st->pr;
        if (L_right < L_indent + 16) L_right = L_indent + 16;
        L_x = L_indent;
        L_align = st->align;
        if (st->display == D_LISTITEM && st->lstyle != 2) {
            char mk[16]; u32 ml = 0;
            int par = n->parent >= 0 ? n->parent : 0;
            if (wc_nodes[par].tag == TG_OL || st->lstyle == 1) {
                int num = wc_li_number(node);
                char t[12]; int tl = 0;
                if (num == 0) t[tl++] = '0';
                while (num > 0 && tl < 10) { t[tl++] = (char)('0' + num % 10); num /= 10; }
                while (tl > 0) mk[ml++] = t[--tl];
                mk[ml++] = '.'; mk[ml++] = ' ';
            } else { mk[ml++] = '*'; mk[ml++] = ' '; }
            int cw = 8 * wc_scale(st->fs);
            L_apply_pending();
            L_x = L_indent - (int)ml * cw; if (L_x < 0) L_x = 0;
            L_marker_mode = 1;
            L_emit(mk, ml, 0, st, node);
            L_marker_mode = 0;
            L_x = L_indent;
            L_pend_space = 0;
        } else L_pend_space = 0;
    } else if (st->display == D_CELL) {
        int pn = n->parent;
        if (pn >= 0 && wc_nodes[pn].first != node && !L_line_empty()) { L_x += 24; L_pend_space = 0; }
    }
    if (tag == TG_BUTTON) { static const wc_style *dummy; (void)dummy; L_text("[", 1, st, node); L_pend_space = 0; }
    for (int c = n->first; c >= 0; c = wc_nodes[c].next) wc_lay_node(c, depth + 1);
    if (tag == TG_BUTTON) { L_pend_space = 0; L_text("]", 1, st, node); }
    if (block) {
        L_flush(0);
        L_indent = old_indent; L_align = old_align; L_right = old_right;
        L_x = L_indent;
        if (st->pb > 0 || st->bw[2] > 0 || box >= 0) { L_apply_pending(); L_y += st->pb + st->bw[2]; }
        if (box >= 0) wc_boxes[box].y1 = L_y;
        if (st->mb > L_pending) L_pending = st->mb;
    }
}

static void wc_layout(int width) {
    L_w = width < 80 ? 80 : width; L_right = L_w - L_RMARGIN; L_y = 0; L_pending = 0; L_indent = 0; L_x = 0; L_line_h = 0; L_pend_space = 0; L_align = AL_LEFT;
    wc_seg_count = wc_line_count = wc_box_count = 0; L_line_seg0 = 0; wc_ltext_len = 0;
    wc_page_bg.r = wc_page_bg.g = wc_page_bg.b = 250;
    for (int i = 1; i < wc_node_count; i++) {
        if (wc_nodes[i].type != 1) continue;
        if ((wc_nodes[i].tag == TG_HTML || wc_nodes[i].tag == TG_BODY) && wc_cs[i].has_bg) {
            wc_page_bg.r = wc_cs[i].bg[0]; wc_page_bg.g = wc_cs[i].bg[1]; wc_page_bg.b = wc_cs[i].bg[2];
        }
    }
    for (int c = wc_nodes[0].first; c >= 0; c = wc_nodes[c].next) wc_lay_node(c, 1);
    L_flush(0);
    wc_doc_h = L_y + 16;
}

 
static void wc_collect_css_node(int n, int depth) {
    if (depth > WC_MAX_DEPTH + 8) return;
    if (wc_nodes[n].type == 1 && wc_nodes[n].tag == TG_STYLE) {
        for (int c = wc_nodes[n].first; c >= 0; c = wc_nodes[c].next)
            if (wc_nodes[c].type == 2) wc_css_parse(WCS(wc_nodes[c].text), WCS(wc_nodes[c].text) + wc_nodes[c].text.len);
        return;
    }
    for (int c = wc_nodes[n].first; c >= 0; c = wc_nodes[c].next) wc_collect_css_node(c, depth + 1);
}
 

static void wc_collect_css(void) {
    wc_rule_count = 0; wc_decl_count = 0; wc_var_count = 0;
    wc_collect_css_node(0, 0);
}
static void wc_prepare_and_layout(int width) {
    wc_vw = width < 80 ? 80 : width;
    wc_collect_css();
    wc_compute_all();
    wc_layout(width);
}

 
static int wc_hit(int x, int y_doc) {
    int lo = 0, hi = wc_line_count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (y_doc < wc_lines[mid].y) hi = mid - 1;
        else if (y_doc >= wc_lines[mid].y + wc_lines[mid].h) lo = mid + 1;
        else { found = mid; break; }
    }
    if (found < 0) return -1;
    wc_line *ln = &wc_lines[found];
    for (u32 s = 0; s < ln->seg_count; s++) {
        wc_seg *sg = &wc_segs[ln->seg_start + s];
        int w = sg->len * 8 * sg->scale;
        if (x >= sg->x && x < sg->x + w) return sg->node;
    }
    return -1;
}
static int wc_link_of(int node) {
    for (int n = node; n >= 0; n = wc_nodes[n].parent)
        if (wc_nodes[n].type == 1 && wc_nodes[n].tag == TG_A && wc_nodes[n].href.len) return n;
    return -1;
}
static int wc_onclick_of(int node) {
    for (int n = node; n >= 0; n = wc_nodes[n].parent)
        if (wc_nodes[n].type == 1 && wc_nodes[n].has_onclick) return n;
    return -1;
}

 
static int wc_streq_w(wstr w, const char *s, u32 n) {
    if (w.len != n) return 0;
    const char *a = WCS(w);
    for (u32 i = 0; i < n; i++) if (a[i] != s[i]) return 0;
    return 1;
}
static int wc_get_by_id(const char *id, u32 n) {
    if (n == 0) return -1;
    for (int i = 1; i < wc_node_count; i++)
        if (wc_nodes[i].type == 1 && wc_streq_w(wc_nodes[i].id, id, n)) return i;
    return -1;
}
static int wc_find_tag_node(int tag) {
    for (int i = 1; i < wc_node_count; i++) if (wc_nodes[i].type == 1 && wc_nodes[i].tag == tag) return i;
    return -1;
}
 
static int wc_query_selector(const char *sel, u32 n) {
    wc_rule r;
    u32 save = wc_pool_len;
    int ok = wc_parse_selector(sel, sel + n, &r);
    if (!ok) { wc_pool_len = save; return -1; }
    int res = -1;
    for (int i = 1; i < wc_node_count && res < 0; i++) if (wc_nodes[i].type == 1 && wc_rule_matches(&r, i)) res = i;
    return res;
}
static u32 wc_text_content(int node, char *out, u32 cap) {
    u32 o = 0;
    if (wc_nodes[node].type == 2) { for (u32 i = 0; i < wc_nodes[node].text.len && o < cap; i++) out[o++] = WCS(wc_nodes[node].text)[i]; return o; }
    for (int c = wc_nodes[node].first; c >= 0 && o < cap; c = wc_nodes[c].next) {
        if (wc_nodes[c].type == 1 && (wc_nodes[c].tag == TG_SCRIPT || wc_nodes[c].tag == TG_STYLE)) continue;
        o += wc_text_content(c, out + o, cap - o);
    }
    return o;
}
static void wc_set_text(int node, const char *s, u32 n) {
    wc_remove_children(node);
    if (n == 0) return;
    int t = wc_new_node(2, 0, 0);
    if (t < 0) return;
    wc_nodes[t].text = wc_put(s, n);
    wc_append_child(node, t);
}
static void wc_set_html(int node, const char *s, u32 n) {
    wc_remove_children(node);
    int saved = wc_open_n;
    wc_parse_fragment(node, (const u8 *)s, n);
    wc_open_n = saved;
}
 
static void wc_style_set(int node, const char *prop, u32 pl, const char *val, u32 vl) {
    wc_node *nd = &wc_nodes[node];
    static char buf[2300];
    u32 o = 0;
    for (u32 i = 0; i < nd->style.len && o < 2000; i++) buf[o++] = WCS(nd->style)[i];
    if (o && buf[o - 1] != ';') buf[o++] = ';';
    for (u32 i = 0; i < pl && o < 2200; i++) buf[o++] = prop[i];
    buf[o++] = ':';
    for (u32 i = 0; i < vl && o < 2280; i++) buf[o++] = val[i];
    buf[o++] = ';';
    nd->style = wc_put(buf, o);
}

#endif  
