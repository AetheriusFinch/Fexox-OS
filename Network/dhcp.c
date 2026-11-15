/* Fexox OS Project, EULA GPLv2 */
 


#include "net.h"

enum { DH_IDLE = 0, DH_SELECTING, DH_REQUESTING, DH_BOUND, DH_RENEWING };

#define DHCP_MAGIC 0x63825363u

static struct {
    int      state;
    uint32_t xid;
    uint64_t t_last, t_bound;
    uint8_t  tries;
    ip4_t    offered, server;
    uint32_t lease_s;
} g_dh;
static int g_dh_sock = -1;

static void put_opt(uint8_t **p, uint8_t code, uint8_t len, const void *data) {
    (*p)[0] = code; (*p)[1] = len;
    for (uint8_t i = 0; i < len; i++) (*p)[2 + i] = ((const uint8_t *)data)[i];
    *p += 2 + len;
}

 
static int dhcp_build(netif_t *nif, uint8_t *b, uint8_t type, bool with_req_ip, bool unicast_renew) {
    for (int i = 0; i < 300; i++) b[i] = 0;
    b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
    put_be32(b + 4, g_dh.xid);
    put_be16(b + 10, unicast_renew ? 0 : 0x8000);            
    if (unicast_renew) put_be32(b + 12, nif->ip);            
    for (int i = 0; i < 6; i++) b[28 + i] = nif->mac.b[i];   
    put_be32(b + 236, DHCP_MAGIC);
    uint8_t *p = b + 240;
    put_opt(&p, 53, 1, &type);
    uint8_t cid[7]; cid[0] = 1; for (int i = 0; i < 6; i++) cid[1 + i] = nif->mac.b[i];
    put_opt(&p, 61, 7, cid);
    put_opt(&p, 12, 8, "fexox-os");
    if (with_req_ip) {
        uint8_t v[4]; put_be32(v, g_dh.offered); put_opt(&p, 50, 4, v);
        put_be32(v, g_dh.server); put_opt(&p, 54, 4, v);
    }
    static const uint8_t prl[] = { 1, 3, 6, 15, 51, 54 };
    put_opt(&p, 55, sizeof prl, prl);
    uint8_t mm[2]; put_be16(mm, 1500); put_opt(&p, 57, 2, mm);
    *p++ = 255;
    return (int)(p - b);
}

static void dhcp_send(netif_t *nif, uint8_t type, bool with_req_ip, bool renew) {
    static uint8_t buf[320];
    int n = dhcp_build(nif, buf, type, with_req_ip, renew);
    if (renew) udp_sendto(g_dh_sock, g_dh.server, 67, buf, (uint16_t)n);
    else       udp_send_bcast_noip(nif, 68, 67, buf, (uint16_t)n);
    g_dh.t_last = np_now_ms();
}

static void dhcp_restart(netif_t *nif) {
    g_dh.state = DH_SELECTING;
    g_dh.xid = net_rand32();
    g_dh.tries = 1;
    g_dh.offered = 0; g_dh.server = 0;
    dhcp_send(nif, 1, false, false);
}

void dhcp_start(netif_t *nif) {
    if (g_dh_sock >= 0) { udp_close(g_dh_sock); g_dh_sock = -1; }
    g_dh_sock = udp_open();
    if (g_dh_sock < 0) return;
    if (udp_bind(g_dh_sock, 0, 68) < 0) { udp_close(g_dh_sock); g_dh_sock = -1; return; }
    np_log("[DHCP] discover");
    dhcp_restart(nif);
}

void dhcp_stop(void) {
    g_dh.state = DH_IDLE;
    if (g_dh_sock >= 0) { udp_close(g_dh_sock); g_dh_sock = -1; }
}

bool dhcp_bound(void) { return g_dh.state == DH_BOUND || g_dh.state == DH_RENEWING; }

 
static void dhcp_handle(netif_t *nif, const uint8_t *m, int len) {
    if (len < 241 || m[0] != 2 || get_be32(m + 4) != g_dh.xid || get_be32(m + 236) != DHCP_MAGIC) return;
    for (int i = 0; i < 6; i++) if (m[28 + i] != nif->mac.b[i]) return;

    uint8_t  type = 0;
    ip4_t    mask = 0, gw = 0, dns[2] = { 0, 0 }, server = 0;
    uint32_t lease = 3600;
    const uint8_t *o = m + 240, *end = m + len;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        if (o + 2 > end) break;
        uint8_t code = o[0], l = o[1];
        if (o + 2 + l > end) break;
        const uint8_t *v = o + 2;
        if (code == 53 && l >= 1) type = v[0];
        else if (code == 1 && l >= 4) mask = get_be32(v);
        else if (code == 3 && l >= 4) gw = get_be32(v);
        else if (code == 6 && l >= 4) { dns[0] = get_be32(v); if (l >= 8) dns[1] = get_be32(v + 4); }
        else if (code == 51 && l >= 4) lease = get_be32(v);
        else if (code == 54 && l >= 4) server = get_be32(v);
        o += 2 + l;
    }
    ip4_t yiaddr = get_be32(m + 16);

    if (type == 2 && g_dh.state == DH_SELECTING) {                  
        g_dh.offered = yiaddr;
        g_dh.server = server ? server : get_be32(m + 20);
        g_dh.state = DH_REQUESTING;
        g_dh.tries = 1;
        np_log("[DHCP] offer, requesting");
        dhcp_send(nif, 3, true, false);
    } else if (type == 5 && (g_dh.state == DH_REQUESTING || g_dh.state == DH_RENEWING)) {    
        bool changed = !nif->ip_valid || nif->ip != yiaddr;
        if (mask == 0) mask = IP4(255, 255, 255, 0);
        nif->ip = yiaddr; nif->mask = mask; nif->gw = gw;
        nif->dns[0] = dns[0]; nif->dns[1] = dns[1];
        nif->ip_valid = true;
        g_dh.lease_s = lease;
        g_dh.t_bound = np_now_ms();
        g_dh.state = DH_BOUND;
        if (changed) { arp_flush(); arp_announce(nif); }
        np_log("[DHCP] bound");
    } else if (type == 6) {                                         
        np_log("[DHCP] NAK, restarting");
        nif->ip_valid = false;
        tcp_reset_all();
        dhcp_restart(nif);
    }
}

void dhcp_tick(netif_t *nif) {
    if (g_dh.state == DH_IDLE || g_dh_sock < 0 || !nif->link_up) return;

    static uint8_t rx[600];
    for (int i = 0; i < 4; i++) {
        ip4_t from; uint16_t fp;
        int n = udp_recv_nb(g_dh_sock, rx, sizeof rx, &from, &fp);
        if (n <= 0) break;
        dhcp_handle(nif, rx, n);
    }

    uint64_t now = np_now_ms();
    switch (g_dh.state) {
    case DH_SELECTING:
    case DH_REQUESTING: {
        uint32_t wait = 2000u << (g_dh.tries > 4 ? 3 : g_dh.tries - 1);
        if (now - g_dh.t_last >= wait) {
            if (g_dh.tries >= 5) { dhcp_restart(nif); break; }
            g_dh.tries++;
            dhcp_send(nif, g_dh.state == DH_SELECTING ? 1 : 3, g_dh.state == DH_REQUESTING, false);
        }
        break;
    }
    case DH_BOUND:
        if (g_dh.lease_s != 0xFFFFFFFFu && now - g_dh.t_bound >= (uint64_t)g_dh.lease_s * 500u) {
            g_dh.state = DH_RENEWING;
            g_dh.tries = 1;
            dhcp_send(nif, 3, false, true);
        }
        break;
    case DH_RENEWING:
        if (g_dh.lease_s != 0xFFFFFFFFu && now - g_dh.t_bound >= (uint64_t)g_dh.lease_s * 1000u) {
            np_log("[DHCP] lease expired");
            nif->ip_valid = false;
            tcp_reset_all();
            dhcp_restart(nif);
        } else if (now - g_dh.t_last >= 10000) {
            dhcp_send(nif, 3, false, true);
        }
        break;
    default:
        break;
    }
}
