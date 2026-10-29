/* Fexox OS Project, EULA GPLv2 */
 



#include "net.h"

 
int ip4_parse(const char *s, ip4_t *out) {
    uint32_t v = 0; int parts = 0;
    for (;;) {
        if (*s < '0' || *s > '9') return -1;
        uint32_t n = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (uint32_t)(*s - '0'); s++; if (++digits > 3) return -1; }
        if (n > 255) return -1;
        v = (v << 8) | n; parts++;
        if (*s == '.') { s++; continue; }
        break;
    }
    if (*s != '\0' || parts != 4) return -1;
    *out = v;
    return 0;
}

void ip4_format(ip4_t ip, char *out) {
    int o = 0;
    for (int k = 3; k >= 0; k--) {
        uint32_t b = (ip >> (k * 8)) & 0xFF;
        char t[4]; int n = 0;
        if (b == 0) t[n++] = '0';
        while (b) { t[n++] = (char)('0' + b % 10); b /= 10; }
        while (n) out[o++] = t[--n];
        if (k) out[o++] = '.';
    }
    out[o] = '\0';
}

uint16_t net_l4_checksum(ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *seg, uint16_t len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF; sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF; sum += dst & 0xFFFF;
    sum += proto;
    sum += len;
    sum = net_csum_add(sum, seg, len);
    return net_csum_fold(sum);
}

 
bool ip_is_local(ip4_t a) {
    netif_t *nif = net_default_if();
    if (!nif) return false;
    if (a == 0xFFFFFFFFu) return true;
    if (nif->ip_valid && (a == nif->ip || (nif->mask && a == (nif->ip | ~nif->mask)))) return true;
    return false;
}

ip4_t ip_route_next_hop(ip4_t dst) {
    netif_t *nif = net_default_if();
    if (!nif || !nif->ip_valid) return 0;
    if (dst == 0xFFFFFFFFu) return dst;
    if (nif->mask && (dst & nif->mask) == (nif->ip & nif->mask)) return dst;      
    return nif->gw;                                                               
}

 
void ip_input(netif_t *nif, const uint8_t *p, uint16_t len) {
    if (len < 20 || (p[0] >> 4) != 4) { nif->rx_dropped++; return; }
    uint16_t ihl = (uint16_t)((p[0] & 0x0F) * 4);
    uint16_t tot = get_be16(p + 2);
    if (ihl < 20 || tot < ihl || tot > len) { nif->rx_dropped++; return; }
    if (net_csum_fold(net_csum_add(0, p, ihl)) != 0) { nif->rx_dropped++; return; }
    if (get_be16(p + 6) & 0x3FFFu) { nif->rx_dropped++; return; }             

    uint8_t proto = p[9];
    ip4_t src = get_be32(p + 12), dst = get_be32(p + 16);
    const uint8_t *pl = p + ihl;
    uint16_t pl_len = (uint16_t)(tot - ihl);

    if (nif->ip_valid) {
        bool mine = (dst == nif->ip) || dst == 0xFFFFFFFFu || (nif->mask && dst == (nif->ip | ~nif->mask));
        if (!mine) return;
    } else if (proto != IPPROTO_UDP) {
        return;                                     
    }

    if (proto == IPPROTO_ICMP)      icmp_input(nif, src, dst, pl, pl_len);
    else if (proto == IPPROTO_UDP)  udp_input(nif, src, dst, pl, pl_len);
    else if (proto == IPPROTO_TCP)  tcp_input(nif, src, dst, pl, pl_len);
}

 
static uint8_t g_ippkt[NET_MTU];

static int ip_build(uint8_t *pkt, ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *payload, uint16_t len) {
    if ((uint32_t)len + 20 > NET_MTU) return -NET_EMSGSIZE;
    uint16_t total = (uint16_t)(20 + len);
    pkt[0] = 0x45; pkt[1] = 0;
    put_be16(pkt + 2, total);
    put_be16(pkt + 4, net_ip_id_next());
    put_be16(pkt + 6, 0x4000);                        
    pkt[8] = 64; pkt[9] = proto;
    put_be16(pkt + 10, 0);
    put_be32(pkt + 12, src);
    put_be32(pkt + 16, dst);
    put_be16(pkt + 10, net_csum_fold(net_csum_add(0, pkt, 20)));
    for (uint16_t i = 0; i < len; i++) pkt[20 + i] = payload[i];
    return total;
}

int ip_send(ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *payload, uint16_t len) {
    netif_t *nif = net_default_if();
    if (!nif || !nif->link_up || !nif->ip_valid) return -NET_ENETDOWN;
    ip4_t hop = ip_route_next_hop(dst);
    if (hop == 0) return -NET_ENETUNREACH;
    if (src == 0) src = nif->ip;
    int total = ip_build(g_ippkt, src, dst, proto, payload, len);
    if (total < 0) return total;
    return eth_send_ip(nif, hop, g_ippkt, (uint16_t)total);
}

int ip_send_raw_broadcast(netif_t *nif, ip4_t src, uint8_t proto, const uint8_t *payload, uint16_t len) {
    int total = ip_build(g_ippkt, src, 0xFFFFFFFFu, proto, payload, len);
    if (total < 0) return total;
    return eth_send_ip(nif, 0xFFFFFFFFu, g_ippkt, (uint16_t)total);
}

 


static struct {
    bool     active, done;
    uint16_t id, seq;
    ip4_t    dst;
    uint64_t t_sent, rtt;
} g_ping;
static uint16_t g_ping_seq;

void icmp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *d, uint16_t len) {
    (void)dst;
    if (len < 8) return;
    if (net_csum_fold(net_csum_add(0, d, len)) != 0) return;
    uint8_t type = d[0];

    if (type == 8) {                                          
        if (!nif->ip_valid || len > NET_MTU - 20) return;
        static uint8_t reply[NET_MTU];
        for (uint16_t i = 0; i < len; i++) reply[i] = d[i];
        reply[0] = 0;
        put_be16(reply + 2, 0);
        put_be16(reply + 2, net_csum_fold(net_csum_add(0, reply, len)));
        ip_send(0, src, IPPROTO_ICMP, reply, len);
    } else if (type == 0) {                                   
        if (g_ping.active && !g_ping.done && src == g_ping.dst &&
            get_be16(d + 4) == g_ping.id && get_be16(d + 6) == g_ping.seq) {
            g_ping.rtt = np_now_ms() - g_ping.t_sent;
            g_ping.done = true;
        }
    }
}

int icmp_ping(ip4_t dst, uint32_t timeout_ms, uint32_t *rtt_ms) {
    uint8_t pkt[8 + 32];
    uint64_t fl = net_lock();
    if (g_ping.active) { net_unlock(fl); return -NET_EAGAIN; }
    g_ping.active = true; g_ping.done = false;
    g_ping.id = (uint16_t)net_rand32();
    g_ping.seq = ++g_ping_seq;
    g_ping.dst = dst;
    pkt[0] = 8; pkt[1] = 0;
    put_be16(pkt + 2, 0);
    put_be16(pkt + 4, g_ping.id);
    put_be16(pkt + 6, g_ping.seq);
    for (int i = 0; i < 32; i++) pkt[8 + i] = (uint8_t)('a' + (i % 23));
    put_be16(pkt + 2, net_csum_fold(net_csum_add(0, pkt, sizeof pkt)));
    g_ping.t_sent = np_now_ms();
    int r = ip_send(0, dst, IPPROTO_ICMP, pkt, sizeof pkt);
    net_unlock(fl);
    if (r < 0) { g_ping.active = false; return r; }

    uint64_t deadline = np_now_ms() + timeout_ms;
    for (;;) {
        fl = net_lock();
        bool done = g_ping.done;
        uint64_t rtt = g_ping.rtt;
        net_unlock(fl);
        if (done) {
            g_ping.active = false;
            if (rtt_ms) *rtt_ms = (uint32_t)rtt;
            return 0;
        }
        if (np_now_ms() > deadline) { g_ping.active = false; return -NET_ETIMEDOUT; }
        np_yield();
    }
}
