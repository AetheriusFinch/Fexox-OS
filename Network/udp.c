/* Fexox OS Project, EULA GPLv2 */
 



#include "net.h"

typedef struct { ip4_t ip; uint16_t port; uint16_t len; uint8_t data[UDP_MAX_DGRAM]; } udp_dgram_t;
typedef struct {
    bool     used;
    bool     bound;
    uint16_t lport;
    ip4_t    lip;
    udp_dgram_t q[UDP_RXQ];
    uint8_t  qh, qn;
} udp_sock_t;

static udp_sock_t g_udp[UDP_MAX_SOCKS];
static uint8_t    g_udp_tx[NET_MTU];

bool udp_port_in_use(uint16_t port) {
    for (int i = 0; i < UDP_MAX_SOCKS; i++)
        if (g_udp[i].used && g_udp[i].bound && g_udp[i].lport == port) return true;
    return false;
}

static uint16_t alloc_ephemeral(void) {
    for (int tries = 0; tries < 200; tries++) {
        uint16_t p = (uint16_t)(49152u + net_rand32() % 16384u);
        if (!udp_port_in_use(p) && !tcp_port_in_use(p)) return p;
    }
    return 0;
}

bool udp_is_open(int s) { return s >= 0 && s < UDP_MAX_SOCKS && g_udp[s].used; }

int udp_open(void) {
    uint64_t fl = net_lock();
    for (int i = 0; i < UDP_MAX_SOCKS; i++) {
        if (!g_udp[i].used) {
            g_udp[i].used = true; g_udp[i].bound = false;
            g_udp[i].lport = 0; g_udp[i].lip = 0; g_udp[i].qh = g_udp[i].qn = 0;
            net_unlock(fl);
            return i;
        }
    }
    net_unlock(fl);
    return -NET_ENOMEM;
}

int udp_bind(int s, ip4_t ip, uint16_t port) {
    uint64_t fl = net_lock();
    int r = 0;
    if (!udp_is_open(s)) r = -NET_EBADF;
    else if (g_udp[s].bound) r = -NET_EINVAL;
    else {
        if (port == 0) port = alloc_ephemeral();
        if (port == 0) r = -NET_EADDRINUSE;
        else if (udp_port_in_use(port)) r = -NET_EADDRINUSE;
        else { g_udp[s].bound = true; g_udp[s].lport = port; g_udp[s].lip = ip; }
    }
    net_unlock(fl);
    return r;
}

uint16_t udp_local_port(int s) { return udp_is_open(s) ? g_udp[s].lport : 0; }

int udp_close(int s) {
    uint64_t fl = net_lock();
    if (udp_is_open(s)) { g_udp[s].used = false; g_udp[s].bound = false; g_udp[s].qn = 0; }
    net_unlock(fl);
    return 0;
}

static int udp_build(uint8_t *out, ip4_t src, ip4_t dst, uint16_t sport, uint16_t dport, const uint8_t *data, uint16_t len) {
    if ((uint32_t)len + 8 + 20 > NET_MTU) return -NET_EMSGSIZE;
    uint16_t total = (uint16_t)(8 + len);
    put_be16(out + 0, sport); put_be16(out + 2, dport);
    put_be16(out + 4, total); put_be16(out + 6, 0);
    for (uint16_t i = 0; i < len; i++) out[8 + i] = data[i];
    uint16_t c = net_l4_checksum(src, dst, IPPROTO_UDP, out, total);
    put_be16(out + 6, c ? c : 0xFFFF);
    return total;
}

int udp_sendto(int s, ip4_t dst, uint16_t dport, const void *buf, uint16_t len) {
    uint64_t fl = net_lock();
    int r;
    if (!udp_is_open(s)) { net_unlock(fl); return -NET_EBADF; }
    if (!g_udp[s].bound) {
        uint16_t p = alloc_ephemeral();
        if (!p) { net_unlock(fl); return -NET_EADDRINUSE; }
        g_udp[s].bound = true; g_udp[s].lport = p;
    }
    netif_t *nif = net_default_if();
    if (!nif || !nif->ip_valid) { net_unlock(fl); return -NET_ENETDOWN; }
    ip4_t src = nif->ip;
    int n = udp_build(g_udp_tx, src, dst, g_udp[s].lport, dport, (const uint8_t *)buf, len);
    r = (n < 0) ? n : ip_send(src, dst, IPPROTO_UDP, g_udp_tx, (uint16_t)n);
    net_unlock(fl);
    return r < 0 ? r : (int)len;
}

int udp_send_bcast_noip(netif_t *nif, uint16_t sport, uint16_t dport, const uint8_t *data, uint16_t len) {
    uint64_t fl = net_lock();
    int n = udp_build(g_udp_tx, 0, 0xFFFFFFFFu, sport, dport, data, len);
    int r = (n < 0) ? n : ip_send_raw_broadcast(nif, 0, IPPROTO_UDP, g_udp_tx, (uint16_t)n);
    net_unlock(fl);
    return r;
}

void udp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *d, uint16_t len) {
    if (len < 8) return;
    uint16_t sport = get_be16(d), dport = get_be16(d + 2), ulen = get_be16(d + 4);
    if (ulen < 8 || ulen > len) return;
    if (get_be16(d + 6) != 0 && net_l4_checksum(src, dst, IPPROTO_UDP, d, ulen) != 0) return;
    if (!nif->ip_valid && dport != 68) return;               

    for (int i = 0; i < UDP_MAX_SOCKS; i++) {
        udp_sock_t *u = &g_udp[i];
        if (!u->used || !u->bound || u->lport != dport) continue;
        if (u->lip != 0 && u->lip != dst) continue;
        if (u->qn >= UDP_RXQ) return;                         
        uint16_t plen = (uint16_t)(ulen - 8);
        if (plen > UDP_MAX_DGRAM) plen = UDP_MAX_DGRAM;
        udp_dgram_t *q = &u->q[(u->qh + u->qn) % UDP_RXQ];
        q->ip = src; q->port = sport; q->len = plen;
        for (uint16_t k = 0; k < plen; k++) q->data[k] = d[8 + k];
        u->qn++;
        return;
    }
}

int udp_recv_nb(int s, void *buf, uint16_t bufsz, ip4_t *from, uint16_t *from_port) {
    uint64_t fl = net_lock();
    if (!udp_is_open(s)) { net_unlock(fl); return -NET_EBADF; }
    udp_sock_t *u = &g_udp[s];
    if (u->qn == 0) { net_unlock(fl); return 0; }
    udp_dgram_t *q = &u->q[u->qh];
    uint16_t n = q->len < bufsz ? q->len : bufsz;
    for (uint16_t i = 0; i < n; i++) ((uint8_t *)buf)[i] = q->data[i];
    if (from) *from = q->ip;
    if (from_port) *from_port = q->port;
    u->qh = (uint8_t)((u->qh + 1) % UDP_RXQ);
    u->qn--;
    net_unlock(fl);
    return n;
}
