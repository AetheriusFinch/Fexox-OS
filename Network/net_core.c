/* Fexox OS Project, EULA GPLv2 */
 



#include "net.h"

static netif_t g_if;                  
static uint32_t g_rng_state;
static uint16_t g_ip_id;

 
uint32_t net_csum_add(uint32_t sum, const uint8_t *d, size_t n) {
    while (n > 1) { sum += (uint32_t)((d[0] << 8) | d[1]); d += 2; n -= 2; }
    if (n) sum += (uint32_t)(d[0] << 8);
    return sum;
}

 
uint64_t net_lock(void)            { return np_irq_save(); }
void     net_unlock(uint64_t f)    { np_irq_restore(f); }
uint64_t net_now_ms(void)          { return np_now_ms(); }

uint32_t net_rand32(void) {
    if (g_rng_state == 0) g_rng_state = (uint32_t)np_entropy() | 1u;
    g_rng_state ^= (uint32_t)np_entropy();           
    uint32_t x = g_rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    if (x == 0) x = 0x9E3779B9u;
    g_rng_state = x;
    return x;
}

uint16_t net_ip_id_next(void) {
    if (g_ip_id == 0) g_ip_id = (uint16_t)net_rand32();
    return ++g_ip_id;
}

 
netif_t *net_default_if(void) { return g_if.used ? &g_if : NULL; }

netif_t *netif_create(const char *name, const mac_t *mac) {
    uint64_t fl = net_lock();
    if (g_if.used) { net_unlock(fl); return NULL; }         
    for (size_t i = 0; i < sizeof(g_if); i++) ((uint8_t *)&g_if)[i] = 0;
    size_t i = 0;
    while (name[i] && i < sizeof(g_if.name) - 1) { g_if.name[i] = name[i]; i++; }
    g_if.mac = *mac;
    g_if.mtu = NET_MTU;
    g_if.used = true;
    arp_flush();
    net_unlock(fl);
    return &g_if;
}

void netif_remove(netif_t *nif) {
    uint64_t fl = net_lock();
    if (nif != &g_if || !g_if.used) { net_unlock(fl); return; }
    tcp_reset_all();
    dhcp_stop();
    arp_flush();
    g_if.link_up = false;
    g_if.ip_valid = false;
    g_if.used = false;
    g_if.tx = NULL; g_if.rx_poll = NULL; g_if.service = NULL; g_if.drv = NULL;
    net_unlock(fl);
    np_log("[NET] interface removed");
}

void netif_set_link(netif_t *nif, bool up) {
    uint64_t fl = net_lock();
    if (nif->link_up != up) {
        nif->link_up = up;
        if (up) {
            np_log("[NET] link up");
            dhcp_start(nif);
        } else {
            np_log("[NET] link down");
            tcp_reset_all();
            dhcp_stop();
            nif->ip_valid = false;
            arp_flush();
        }
    }
    net_unlock(fl);
}

void netif_set_static(netif_t *nif, ip4_t ip, ip4_t mask, ip4_t gw, ip4_t dns) {
    uint64_t fl = net_lock();
    nif->ip = ip; nif->mask = mask; nif->gw = gw; nif->dns[0] = dns; nif->dns[1] = 0;
    nif->ip_valid = (ip != 0);
    net_unlock(fl);
}

 
static uint8_t g_rx_frame[NET_FRAME_MAX + 64];

int net_poll_once(void) {
    netif_t *nif = &g_if;
    if (!nif->used || !nif->rx_poll) return 0;

    int handled = 0;
    for (int i = 0; i < 16; i++) {
        int r = nif->rx_poll(nif, g_rx_frame, (uint16_t)sizeof(g_rx_frame));    
        if (r <= 0) break;
        uint64_t fl = net_lock();
        if (nif->used) {
            nif->rx_frames++; nif->rx_bytes += (uint32_t)r;
            eth_input(nif, g_rx_frame, (uint16_t)r);
        }
        net_unlock(fl);
        handled++;
    }

    static uint64_t last_service = 0;
    uint64_t now = np_now_ms();
    if (now - last_service >= 1000 && nif->used) {
        last_service = now;
        if (nif->service) nif->service(nif);               
    }

    uint64_t fl = net_lock();
    if (nif->used) {
        arp_tick(nif);
        dhcp_tick(nif);
        tcp_tick();
    }
    net_unlock(fl);
    return handled;
}

static void netd_thread(void *arg) {
    (void)arg;
    uint64_t last_hotplug = 0;
    np_log("[NET] netd started");
    for (;;) {
        int n = net_poll_once();
        uint64_t now = np_now_ms();
        if (now - last_hotplug >= 300) { last_hotplug = now; np_usb_hotplug(); }
        if (n == 0) np_idle(); else np_yield();
    }
}

int net_init(void) {
    if (np_thread_create(netd_thread, NULL, "netd") < 0) {
        np_log("[NET] cannot create netd thread");
        return -1;
    }
    return 0;
}

 
static char *st_str(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
static char *st_u(char *d, uint32_t v) {
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v && n < 11) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *d++ = t[--n];
    return d;
}

void net_status_text(char *out, size_t n) {
    char b[320]; char *d = b;
    netif_t *nif = net_default_if();
    if (!nif) {
        d = st_str(d, "net: no USB modem (phone: enable USB tethering)");
    } else if (!nif->link_up) {
        d = st_str(d, "net: "); d = st_str(d, nif->name); d = st_str(d, " link down");
    } else if (!nif->ip_valid) {
        d = st_str(d, "net: "); d = st_str(d, nif->name); d = st_str(d, " DHCP...");
    } else {
        char t[20];
        d = st_str(d, "net: "); d = st_str(d, nif->name); d = st_str(d, " ");
        ip4_format(nif->ip, t); d = st_str(d, t);
        d = st_str(d, " gw "); ip4_format(nif->gw, t); d = st_str(d, t);
        d = st_str(d, " dns "); ip4_format(nif->dns[0], t); d = st_str(d, t);
        d = st_str(d, " rx "); d = st_u(d, nif->rx_frames);
        d = st_str(d, " tx "); d = st_u(d, nif->tx_frames);
         


        extern void tcp_dbg(uint32_t out[10]);
        extern void rndis_dbg(uint32_t out[3]);
        uint32_t td[10]; tcp_dbg(td);
        uint32_t rd[3]; rndis_dbg(rd);
        d = st_str(d, " drop "); d = st_u(d, nif->rx_dropped);
        d = st_str(d, " | tcp rx "); d = st_u(d, td[0]);
        d = st_str(d, " bad "); d = st_u(d, td[1]);
        d = st_str(d, " nm "); d = st_u(d, td[2]);
        d = st_str(d, " rst "); d = st_u(d, td[3]);
        d = st_str(d, " syn "); d = st_u(d, td[4]);
        d = st_str(d, " sa "); d = st_u(d, td[5]);
         

        d = st_str(d, " | ooo "); d = st_u(d, td[6]);
        d = st_str(d, " dup "); d = st_u(d, td[7]);
        d = st_str(d, " B "); d = st_u(d, td[8]);
        d = st_str(d, " rtx "); d = st_u(d, td[9]);
        d = st_str(d, " | ru "); d = st_u(d, rd[0]); d = st_str(d, "/"); d = st_u(d, rd[1]);
        d = st_str(d, " "); d = st_u(d, rd[2] / 1024); d = st_str(d, "K");
    }
    *d = '\0';
    size_t i = 0;
    while (b[i] && i + 1 < n) { out[i] = b[i]; i++; }
    if (n) out[i] = '\0';
}
