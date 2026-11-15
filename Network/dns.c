/* Fexox OS Project, EULA GPLv2 */
 


#include "net.h"

#define DNS_CACHE 8

static struct { bool used; char name[64]; ip4_t ip; uint64_t expire; } g_dns_cache[DNS_CACHE];

static int name_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static bool ieq(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
        if (!x) return true;
    }
}

static bool cache_get(const char *name, ip4_t *out) {
    uint64_t now = np_now_ms();
    for (int i = 0; i < DNS_CACHE; i++)
        if (g_dns_cache[i].used && g_dns_cache[i].expire > now && ieq(g_dns_cache[i].name, name)) {
            *out = g_dns_cache[i].ip; return true;
        }
    return false;
}

static void cache_put(const char *name, ip4_t ip, uint32_t ttl_s) {
    if (ttl_s < 30) ttl_s = 30;
    if (ttl_s > 600) ttl_s = 600;
    int slot = 0; uint64_t oldest = ~0ULL;
    for (int i = 0; i < DNS_CACHE; i++) {
        if (!g_dns_cache[i].used || ieq(g_dns_cache[i].name, name)) { slot = i; break; }
        if (g_dns_cache[i].expire < oldest) { oldest = g_dns_cache[i].expire; slot = i; }
    }
    int n = name_len(name); if (n > 63) n = 63;
    for (int i = 0; i < n; i++) g_dns_cache[slot].name[i] = name[i];
    g_dns_cache[slot].name[n] = '\0';
    g_dns_cache[slot].ip = ip;
    g_dns_cache[slot].expire = np_now_ms() + (uint64_t)ttl_s * 1000u;
    g_dns_cache[slot].used = true;
}

 
static int encode_name(const char *name, uint8_t *out) {
    int o = 0, i = 0;
    if (name_len(name) > 253 || name[0] == '\0') return -1;
    while (name[i]) {
        int start = i;
        while (name[i] && name[i] != '.') i++;
        int l = i - start;
        if (l == 0 || l > 63) return -1;
        out[o++] = (uint8_t)l;
        for (int k = 0; k < l; k++) out[o++] = (uint8_t)name[start + k];
        if (name[i] == '.') i++;
    }
    out[o++] = 0;
    return o;
}

static int skip_name(const uint8_t *m, int len, int off) {
    while (off < len) {
        uint8_t b = m[off];
        if (b == 0) return off + 1;
        if ((b & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        off += b + 1;
    }
    return -1;
}

 
static int parse_response(const uint8_t *m, int len, uint16_t id, ip4_t *ip, uint32_t *ttl) {
    if (len < 12 || get_be16(m) != id || !(m[2] & 0x80)) return -NET_EPROTO;
    int rcode = m[3] & 0x0F;
    if (rcode == 3) return -NET_ENOENT;
    if (rcode != 0) return -NET_EHOSTUNREACH;
    int qd = get_be16(m + 4), an = get_be16(m + 6);
    int off = 12;
    for (int i = 0; i < qd; i++) {
        off = skip_name(m, len, off);
        if (off < 0 || off + 4 > len) return -NET_EPROTO;
        off += 4;
    }
    for (int i = 0; i < an; i++) {
        off = skip_name(m, len, off);
        if (off < 0 || off + 10 > len) return -NET_EPROTO;
        uint16_t type = get_be16(m + off), cls = get_be16(m + off + 2);
        uint32_t t = get_be32(m + off + 4);
        uint16_t rdlen = get_be16(m + off + 8);
        off += 10;
        if (off + rdlen > len) return -NET_EPROTO;
        if (type == 1 && cls == 1 && rdlen == 4) { *ip = get_be32(m + off); *ttl = t; return 0; }
        off += rdlen;                                      
    }
    return -NET_ENOENT;
}

int dns_resolve(const char *name, ip4_t *out, uint32_t timeout_ms) {
    if (ip4_parse(name, out) == 0) return 0;

    uint64_t fl = net_lock();
    bool hit = cache_get(name, out);
    netif_t *nif = net_default_if();
    ip4_t srv[3] = { 0, 0, 0 };
    int nsrv = 0;
    if (nif && nif->ip_valid) {
        if (nif->dns[0]) srv[nsrv++] = nif->dns[0];
        if (nif->dns[1]) srv[nsrv++] = nif->dns[1];
        if (nif->gw)     srv[nsrv++] = nif->gw;
    }
    net_unlock(fl);
    if (hit) return 0;
    if (nsrv == 0) return -NET_ENETDOWN;

    int s = udp_open();
    if (s < 0) return s;
    int rc = udp_bind(s, 0, 0);
    if (rc < 0) { udp_close(s); return rc; }

    uint8_t q[300], r[600];
    uint16_t id = (uint16_t)net_rand32();
    put_be16(q, id); put_be16(q + 2, 0x0100);         
    put_be16(q + 4, 1); put_be16(q + 6, 0); put_be16(q + 8, 0); put_be16(q + 10, 0);
    int nl = encode_name(name, q + 12);
    if (nl < 0) { udp_close(s); return -NET_EINVAL; }
    put_be16(q + 12 + nl, 1);                         
    put_be16(q + 14 + nl, 1);                         
    int qlen = 16 + nl;

    uint64_t deadline = np_now_ms() + timeout_ms;
    int result = -NET_ETIMEDOUT;
    for (int attempt = 0; attempt < 4 && np_now_ms() < deadline; attempt++) {
        ip4_t server = srv[attempt % nsrv];
        int sr = udp_sendto(s, server, 53, q, (uint16_t)qlen);
        if (sr < 0 && sr != -NET_ENETDOWN) { result = sr; }
        uint64_t attempt_end = np_now_ms() + (timeout_ms / 4 < 800 ? 800 : timeout_ms / 4);
        while (np_now_ms() < attempt_end && np_now_ms() < deadline) {
            ip4_t from; uint16_t fp;
            int n = udp_recv_nb(s, r, sizeof r, &from, &fp);
            if (n > 0) {
                ip4_t ip; uint32_t ttl = 60;
                int pr = parse_response(r, n, id, &ip, &ttl);
                if (pr == 0) {
                    fl = net_lock(); cache_put(name, ip, ttl); net_unlock(fl);
                    *out = ip; udp_close(s); return 0;
                }
                if (pr == -NET_ENOENT || pr == -NET_EHOSTUNREACH) { udp_close(s); return pr; }
                continue;                               
            }
            np_yield();
        }
    }
    udp_close(s);
    return result;
}
