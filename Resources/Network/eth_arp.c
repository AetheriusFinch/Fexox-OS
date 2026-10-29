/* Fexox OS Project, EULA GPLv2 */
 







#include "net.h"

#define ARP_ENTRIES   16
#define ARP_TTL_MS    (5u * 60u * 1000u)
#define ARP_PEND_MAX  6

typedef struct { bool used; ip4_t ip; mac_t mac; uint64_t t; } arp_ent_t;
typedef struct {
    bool     used;
    ip4_t    hop;
    uint16_t len;
    uint8_t  pkt[NET_MTU];
    uint64_t t_last;
    uint8_t  tries;
} arp_pend_t;

static arp_ent_t  g_arp[ARP_ENTRIES];
static arp_pend_t g_pend[ARP_PEND_MAX];
static uint8_t    g_txbuf[NET_FRAME_MAX];

static const mac_t MAC_BCAST = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };

static bool mac_eq(const mac_t *a, const mac_t *b) {
    for (int i = 0; i < 6; i++) if (a->b[i] != b->b[i]) return false;
    return true;
}

 
static int eth_tx(netif_t *nif, const mac_t *dst, uint16_t ethertype, const uint8_t *payload, uint16_t len) {
    if (!nif->used || !nif->tx || !nif->link_up) return -NET_ENETDOWN;
    if ((uint32_t)ETH_HLEN + len > NET_FRAME_MAX) return -NET_EMSGSIZE;
    for (int i = 0; i < 6; i++) { g_txbuf[i] = dst->b[i]; g_txbuf[6 + i] = nif->mac.b[i]; }
    put_be16(g_txbuf + 12, ethertype);
    for (uint16_t i = 0; i < len; i++) g_txbuf[ETH_HLEN + i] = payload[i];
    uint16_t total = (uint16_t)(ETH_HLEN + len);
    while (total < 60) g_txbuf[total++] = 0;                
    int r = nif->tx(nif, g_txbuf, total);
    if (r == 0) { nif->tx_frames++; nif->tx_bytes += total; } else nif->tx_errors++;
    return r;
}

 
static void arp_learn(ip4_t ip, const mac_t *mac) {
    if (ip == 0 || ip == 0xFFFFFFFFu) return;
    uint64_t now = np_now_ms();
    int free_i = -1, oldest = 0;
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (g_arp[i].used && g_arp[i].ip == ip) { g_arp[i].mac = *mac; g_arp[i].t = now; return; }
        if (!g_arp[i].used && free_i < 0) free_i = i;
        if (g_arp[i].used && g_arp[i].t < g_arp[oldest].t) oldest = i;
    }
    int i = (free_i >= 0) ? free_i : oldest;
    g_arp[i].used = true; g_arp[i].ip = ip; g_arp[i].mac = *mac; g_arp[i].t = now;
}

bool arp_lookup(ip4_t ip, mac_t *out) {
    uint64_t now = np_now_ms();
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (g_arp[i].used && g_arp[i].ip == ip) {
            if (now - g_arp[i].t > ARP_TTL_MS) { g_arp[i].used = false; return false; }
            *out = g_arp[i].mac;
            return true;
        }
    }
    return false;
}

void arp_flush(void) {
    for (int i = 0; i < ARP_ENTRIES; i++) g_arp[i].used = false;
    for (int i = 0; i < ARP_PEND_MAX; i++) g_pend[i].used = false;
}

static void arp_send(netif_t *nif, uint16_t op, const mac_t *dst_eth, const mac_t *tha, ip4_t spa, ip4_t tpa) {
    uint8_t p[28];
    put_be16(p + 0, 1);               
    put_be16(p + 2, ETH_P_IP);
    p[4] = 6; p[5] = 4;
    put_be16(p + 6, op);
    for (int i = 0; i < 6; i++) { p[8 + i] = nif->mac.b[i]; p[18 + i] = tha->b[i]; }
    put_be32(p + 14, spa);
    put_be32(p + 24, tpa);
    eth_tx(nif, dst_eth, ETH_P_ARP, p, 28);
}

static void arp_request(netif_t *nif, ip4_t target) {
    static const mac_t zero = { { 0, 0, 0, 0, 0, 0 } };
    arp_send(nif, 1, &MAC_BCAST, &zero, nif->ip_valid ? nif->ip : 0, target);
}

void arp_announce(netif_t *nif) {
    if (!nif->ip_valid) return;
    static const mac_t zero = { { 0, 0, 0, 0, 0, 0 } };
    arp_send(nif, 1, &MAC_BCAST, &zero, nif->ip, nif->ip);        
}

static void pend_flush_for(netif_t *nif, ip4_t hop, const mac_t *mac) {
    for (int i = 0; i < ARP_PEND_MAX; i++) {
        if (g_pend[i].used && g_pend[i].hop == hop) {
            eth_tx(nif, mac, ETH_P_IP, g_pend[i].pkt, g_pend[i].len);
            g_pend[i].used = false;
        }
    }
}

static void arp_input(netif_t *nif, const uint8_t *p, uint16_t len) {
    if (len < 28) return;
    if (get_be16(p) != 1 || get_be16(p + 2) != ETH_P_IP || p[4] != 6 || p[5] != 4) return;
    uint16_t op = get_be16(p + 6);
    mac_t sha; for (int i = 0; i < 6; i++) sha.b[i] = p[8 + i];
    ip4_t spa = get_be32(p + 14);
    ip4_t tpa = get_be32(p + 24);

    if (op == 1) {                                        
        if (nif->ip_valid && tpa == nif->ip) {
            arp_learn(spa, &sha);
            arp_send(nif, 2, &sha, &sha, nif->ip, spa);
        }
    } else if (op == 2) {                                 
        arp_learn(spa, &sha);
        pend_flush_for(nif, spa, &sha);
    }
}

void arp_tick(netif_t *nif) {
    uint64_t now = np_now_ms();
    for (int i = 0; i < ARP_PEND_MAX; i++) {
        if (!g_pend[i].used) continue;
        if (now - g_pend[i].t_last < 500) continue;
        if (g_pend[i].tries >= 4) { g_pend[i].used = false; continue; }     
        g_pend[i].tries++;
        g_pend[i].t_last = now;
        arp_request(nif, g_pend[i].hop);
    }
}

 
int eth_send_ip(netif_t *nif, ip4_t next_hop, const uint8_t *ip_pkt, uint16_t len) {
    if (len > NET_MTU) return -NET_EMSGSIZE;
    bool bcast = (next_hop == 0xFFFFFFFFu) ||
                 (nif->ip_valid && nif->mask && next_hop == (nif->ip | ~nif->mask));
    if (bcast) return eth_tx(nif, &MAC_BCAST, ETH_P_IP, ip_pkt, len);

    mac_t mac;
    if (arp_lookup(next_hop, &mac)) return eth_tx(nif, &mac, ETH_P_IP, ip_pkt, len);

     
    int slot = -1;
    for (int i = 0; i < ARP_PEND_MAX; i++) if (!g_pend[i].used) { slot = i; break; }
    if (slot < 0) slot = 0;                                   
    g_pend[slot].used = true;
    g_pend[slot].hop = next_hop;
    g_pend[slot].len = len;
    for (uint16_t i = 0; i < len; i++) g_pend[slot].pkt[i] = ip_pkt[i];
    g_pend[slot].t_last = np_now_ms();
    g_pend[slot].tries = 1;
    arp_request(nif, next_hop);
    return 0;
}

 
void eth_input(netif_t *nif, const uint8_t *f, uint16_t len) {
    if (len < ETH_HLEN) { nif->rx_dropped++; return; }
    mac_t dst; for (int i = 0; i < 6; i++) dst.b[i] = f[i];
    bool for_us = mac_eq(&dst, &nif->mac) || mac_eq(&dst, &MAC_BCAST);
    if (!for_us) return;                                      
    uint16_t type = get_be16(f + 12);
    const uint8_t *pl = f + ETH_HLEN;
    uint16_t pl_len = (uint16_t)(len - ETH_HLEN);
    if (type == ETH_P_ARP)      arp_input(nif, pl, pl_len);
    else if (type == ETH_P_IP)  ip_input(nif, pl, pl_len);
    else                        nif->rx_dropped++;
}
