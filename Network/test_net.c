/* Fexox OS Project, EULA GPLv2 */
 


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Network/net.h"

 
static uint64_t g_clock_ms = 1000;
static int      g_iters = 0;
#define MAX_ITERS 400000

static void fake_peer_service(void);

uint64_t np_now_ms(void) { g_clock_ms += 2; return g_clock_ms; }
uint64_t np_irq_save(void) { return 0; }
void     np_irq_restore(uint64_t f) { (void)f; }
void     np_yield(void) {
    if (++g_iters > MAX_ITERS) { fprintf(stderr, "\n*** np_yield budget exceeded (test stuck) ***\n"); exit(2); }
    fake_peer_service();
    net_poll_once();
}
void     np_idle(void) { np_yield(); }
int      np_thread_create(void (*fn)(void *), void *arg, const char *name) { (void)fn; (void)arg; (void)name; return 0; }
void     np_log(const char *m) { (void)m;   }
uint64_t np_entropy(void) { static uint64_t s = 0x123456789ABCDEFULL; s = s * 6364136223846793005ULL + 1; return s ^ g_clock_ms; }
void     np_usb_hotplug(void) { }

 
#define WIRE_MAX 16
static struct { uint8_t data[NET_FRAME_MAX]; uint16_t len; } g_to_peer[WIRE_MAX], g_to_us[WIRE_MAX];
static int g_to_peer_n, g_to_us_n;
static bool g_drop_next_to_peer = false;    

static int wire_tx(netif_t *nif, const uint8_t *frame, uint16_t len) {
    (void)nif;
    if (g_to_peer_n >= WIRE_MAX) return -1;
    memcpy(g_to_peer[g_to_peer_n].data, frame, len);
    g_to_peer[g_to_peer_n].len = len;
    g_to_peer_n++;
    return 0;
}
static int wire_rx(netif_t *nif, uint8_t *buf, uint16_t bufsz) {
    (void)nif;
    if (g_to_us_n == 0) return 0;
    uint16_t n = g_to_us[0].len < bufsz ? g_to_us[0].len : bufsz;
    memcpy(buf, g_to_us[0].data, n);
    for (int i = 1; i < g_to_us_n; i++) g_to_us[i - 1] = g_to_us[i];
    g_to_us_n--;
    return n;
}
static void to_us_push(const uint8_t *data, uint16_t len) {
    if (g_to_us_n >= WIRE_MAX) return;
    memcpy(g_to_us[g_to_us_n].data, data, len);
    g_to_us[g_to_us_n].len = len;
    g_to_us_n++;
}

 
static mac_t PEER_MAC = { { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE } };
static const ip4_t PEER_IP = IP4(192, 168, 42, 129);
static const ip4_t OUR_IP  = IP4(192, 168, 42, 2);
static const ip4_t NETMASK = IP4(255, 255, 255, 0);
static const ip4_t DNS_A_RESULT = IP4(93, 184, 216, 34);
static mac_t g_our_mac;
static bool g_our_mac_known;

static uint32_t csum_add(uint32_t sum, const uint8_t *d, int n) {
    while (n > 1) { sum += (uint32_t)((d[0] << 8) | d[1]); d += 2; n -= 2; }
    if (n) sum += (uint32_t)(d[0] << 8);
    return sum;
}
static uint16_t csum_fold(uint32_t sum) { while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16); return (uint16_t)~sum; }

static void eth_wrap(uint8_t *out, const mac_t *dst, const mac_t *src, uint16_t ethertype, const uint8_t *pl, uint16_t plen) {
    memcpy(out, dst->b, 6); memcpy(out + 6, src->b, 6);
    put_be16(out + 12, ethertype);
    memcpy(out + 14, pl, plen);
}

static void peer_send_arp_reply(ip4_t tpa_asked, const mac_t *to) {
    uint8_t p[28];
    put_be16(p + 0, 1); put_be16(p + 2, 0x0800); p[4] = 6; p[5] = 4; put_be16(p + 6, 2);
    memcpy(p + 8, PEER_MAC.b, 6); put_be32(p + 14, PEER_IP);
    memcpy(p + 18, to->b, 6); put_be32(p + 24, tpa_asked);
    uint8_t f[64]; eth_wrap(f, to, &PEER_MAC, 0x0806, p, 28);
    to_us_push(f, 14 + 28);
}

static void peer_send_ip(ip4_t src, ip4_t dst, const mac_t *dstmac, uint8_t proto, const uint8_t *payload, uint16_t len, bool corrupt) {
    uint8_t ip[20 + 2048];
    ip[0] = 0x45; ip[1] = 0; put_be16(ip + 2, (uint16_t)(20 + len));
    put_be16(ip + 4, 1234); put_be16(ip + 6, 0x4000); ip[8] = 64; ip[9] = proto;
    put_be16(ip + 10, 0); put_be32(ip + 12, src); put_be32(ip + 16, dst);
    put_be16(ip + 10, csum_fold(csum_add(0, ip, 20)));
    memcpy(ip + 20, payload, len);
    if (corrupt) ip[20 + len - 1] ^= 0xFF;                  
    uint8_t f[14 + 20 + 2048]; eth_wrap(f, dstmac, &PEER_MAC, 0x0800, ip, (uint16_t)(20 + len));
    to_us_push(f, (uint16_t)(14 + 20 + len));
}

static uint16_t l4_csum(ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *seg, uint16_t len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF; sum += src & 0xFFFF; sum += (dst >> 16) & 0xFFFF; sum += dst & 0xFFFF;
    sum += proto; sum += len;
    sum = csum_add(sum, seg, len);
    return csum_fold(sum);
}

 
static uint32_t g_offered_xid;
static bool dhcp_server_handle(const uint8_t *m, int len) {
    if (len < 241 || m[0] != 1) return false;
    uint32_t xid = get_be32(m + 4);
    const uint8_t *o = m + 240, *end = m + len;
    uint8_t type = 0;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        uint8_t code = o[0], l = o[1];
        if (code == 53 && l >= 1) type = o[2];
        o += 2 + l;
    }
    uint8_t reply[400]; memset(reply, 0, sizeof reply);
    reply[0] = 2; reply[1] = 1; reply[2] = 6; reply[3] = 0;
    put_be32(reply + 4, xid);
    memcpy(reply + 16, (uint8_t[]){ 192, 168, 42, 2 }, 4);           
    memcpy(reply + 20, (uint8_t[]){ 192, 168, 42, 129 }, 4);         
    memcpy(reply + 28, m + 28, 16);                                   
    put_be32(reply + 236, 0x63825363u);
    uint8_t *p = reply + 240;
    uint8_t rtype = (type == 1) ? 2 : (type == 3) ? 5 : 0;            
    if (!rtype) return false;
    g_offered_xid = xid;
    *p++ = 53; *p++ = 1; *p++ = rtype;
    *p++ = 1; *p++ = 4; *p++ = 255; *p++ = 255; *p++ = 255; *p++ = 0;          
    *p++ = 3; *p++ = 4; *p++ = 192; *p++ = 168; *p++ = 42; *p++ = 129;          
    *p++ = 6; *p++ = 4; *p++ = 192; *p++ = 168; *p++ = 42; *p++ = 129;          
    *p++ = 51; *p++ = 4; put_be32(p, 3600); p += 4;                             
    *p++ = 54; *p++ = 4; *p++ = 192; *p++ = 168; *p++ = 42; *p++ = 129;         
    *p++ = 255;
    int rlen = (int)(p - reply);

    uint8_t udp[8 + 400];
    put_be16(udp, 67); put_be16(udp + 2, 68); put_be16(udp + 4, (uint16_t)(8 + rlen)); put_be16(udp + 6, 0);
    memcpy(udp + 8, reply, rlen);
    put_be16(udp + 6, l4_csum(PEER_IP, 0xFFFFFFFFu, IPPROTO_UDP, udp, (uint16_t)(8 + rlen)) ?: 0xFFFF);
    static const mac_t bcast = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };
    peer_send_ip(PEER_IP, 0xFFFFFFFFu, &bcast, IPPROTO_UDP, udp, (uint16_t)(8 + rlen), false);
    return true;
}

 
static void dns_server_handle(ip4_t src, uint16_t sport, const uint8_t *q, int len) {
    if (len < 12) return;
    uint16_t id = get_be16(q);
    int off = 12; char name[256]; int ni = 0;
    bool is_nx = false;
    while (off < len && q[off]) {
        int l = q[off++];
        if (ni) name[ni++] = '.';
        for (int i = 0; i < l && off < len; i++) name[ni++] = (char)q[off++];
    }
    name[ni] = 0;
    if (off < len) off++;                  
    int qend = off + 4;                     
    if (strstr(name, "nxdomain")) is_nx = true;

    uint8_t r[300]; memset(r, 0, sizeof r);
    put_be16(r, id);
    put_be16(r + 2, is_nx ? 0x8183 : 0x8180);
    put_be16(r + 4, 1); put_be16(r + 6, is_nx ? 0 : 1);
    memcpy(r + 12, q + 12, (size_t)(qend - 12));    
    int rl = 12 + (qend - 12);
    if (!is_nx) {
        memcpy(r + rl, q + 12, (size_t)(qend - 12 - 4));    
        rl += qend - 12 - 4;
        put_be16(r + rl, 1); rl += 2; put_be16(r + rl, 1); rl += 2;
        put_be32(r + rl, 300); rl += 4;
        put_be16(r + rl, 4); rl += 2;
        put_be32(r + rl, DNS_A_RESULT); rl += 4;
    }
    uint8_t udp[8 + 300];
    put_be16(udp, 53); put_be16(udp + 2, sport); put_be16(udp + 4, (uint16_t)(8 + rl)); put_be16(udp + 6, 0);
    memcpy(udp + 8, r, (size_t)rl);
    put_be16(udp + 6, l4_csum(PEER_IP, src, IPPROTO_UDP, udp, (uint16_t)(8 + rl)) ?: 0xFFFF);
    peer_send_ip(PEER_IP, src, &g_our_mac, IPPROTO_UDP, udp, (uint16_t)(8 + rl), false);
}

 
typedef struct { bool used; ip4_t rip; uint16_t rport; uint32_t snd_nxt, rcv_nxt; int state; } peer_tcp_t;
static peer_tcp_t g_ptcp[4];
enum { PT_LISTEN = 0, PT_SYNRCVD, PT_EST, PT_FINWAIT, PT_CLOSED };

static void peer_tcp_send(peer_tcp_t *c, uint8_t flags, const uint8_t *data, uint16_t dlen, bool corrupt) {
    uint8_t seg[20 + 1500];
    put_be16(seg, 80); put_be16(seg + 2, c->rport);
    put_be32(seg + 4, c->snd_nxt); put_be32(seg + 8, (flags & 0x10) ? c->rcv_nxt : 0);
    seg[12] = 5 << 4; seg[13] = flags; put_be16(seg + 14, 8192); put_be16(seg + 16, 0); put_be16(seg + 18, 0);
    if (dlen) memcpy(seg + 20, data, dlen);
    put_be16(seg + 16, l4_csum(PEER_IP, c->rip, IPPROTO_TCP, seg, (uint16_t)(20 + dlen)));
    peer_send_ip(PEER_IP, c->rip, &g_our_mac, IPPROTO_TCP, seg, (uint16_t)(20 + dlen), corrupt);
    if (flags & 0x02) c->snd_nxt += 1; else if (flags & 0x01) c->snd_nxt += 1; else c->snd_nxt += dlen;
}

static void tcp_server_handle(ip4_t src, const uint8_t *seg, int len) {
    if (len < 20) return;
    uint16_t sport = get_be16(seg), dport = get_be16(seg + 2);
    if (dport != 80) return;
    uint32_t seq = get_be32(seg + 4), ack = get_be32(seg + 8); (void)ack;
    uint8_t flags = seg[13];
    uint16_t doff = (uint16_t)((seg[12] >> 4) * 4);
    const uint8_t *data = seg + doff; uint16_t dlen = (uint16_t)(len - doff);

    peer_tcp_t *c = NULL;
    for (int i = 0; i < 4; i++) if (g_ptcp[i].used && g_ptcp[i].rip == src && g_ptcp[i].rport == sport) { c = &g_ptcp[i]; break; }

    if (!c && (flags & 0x02)) {
        for (int i = 0; i < 4; i++) if (!g_ptcp[i].used) { c = &g_ptcp[i]; break; }
        if (!c) return;
        memset(c, 0, sizeof *c);
        c->used = true; c->rip = src; c->rport = sport;
        c->rcv_nxt = seq + 1; c->snd_nxt = 0x10000000u;
        c->state = PT_SYNRCVD;
        peer_tcp_send(c, 0x12, NULL, 0, false);                  
        return;
    }
    if (!c) return;

    if (flags & 0x04) { c->used = false; return; }                

    if (dlen > 0 && seq == c->rcv_nxt) {
        c->rcv_nxt += dlen;
         
        peer_tcp_send(c, 0x18, data, dlen, false);                 
        c->state = PT_EST;
    } else if (dlen == 0 && !(flags & 0x01)) {
         
        if (c->state == PT_SYNRCVD) c->state = PT_EST;
    }
    if (flags & 0x01) {                                             
        c->rcv_nxt += 1;
        peer_tcp_send(c, 0x11, NULL, 0, false);                     
        c->state = PT_CLOSED;
        c->used = false;
    }
}

 
static void fake_peer_service(void) {
    for (int i = 0; i < g_to_peer_n; i++) {
        const uint8_t *f = g_to_peer[i].data; uint16_t flen = g_to_peer[i].len;
        if (flen < 14) continue;
        mac_t src; memcpy(src.b, f + 6, 6);
        uint16_t et = get_be16(f + 12);
        const uint8_t *pl = f + 14; uint16_t pll = (uint16_t)(flen - 14);

        if (et == 0x0806 && pll >= 28) {
            uint16_t op = get_be16(pl + 6);
            ip4_t spa = get_be32(pl + 14), tpa = get_be32(pl + 24);
            if (op == 1) {
                if (tpa == PEER_IP) peer_send_arp_reply(spa, &src);
                if (!g_our_mac_known || spa == OUR_IP) { g_our_mac = src; g_our_mac_known = true; }
            }
        } else if (et == 0x0800 && pll >= 20) {
            uint8_t ihl = (uint8_t)((pl[0] & 0x0F) * 4);
            uint8_t proto = pl[9];
            ip4_t s = get_be32(pl + 12);
             






            uint16_t ip_tot = get_be16(pl + 2);
            if (ip_tot < ihl || ip_tot > pll) continue;
            const uint8_t *l4 = pl + ihl; uint16_t l4len = (uint16_t)(ip_tot - ihl);
            g_our_mac = src; g_our_mac_known = true;

            if (proto == IPPROTO_UDP && l4len >= 8) {
                uint16_t sport = get_be16(l4), dport = get_be16(l4 + 2);
                const uint8_t *data = l4 + 8; int dl = l4len - 8;
                if (dport == 67) dhcp_server_handle(data, dl);
                else if (dport == 53) dns_server_handle(s, sport, data, dl);
                else if (dport == 7777) {                             
                    uint8_t udp[8 + 256]; int n = dl > 256 ? 256 : dl;
                    put_be16(udp, 7777); put_be16(udp + 2, sport); put_be16(udp + 4, (uint16_t)(8 + n)); put_be16(udp + 6, 0);
                    memcpy(udp + 8, data, (size_t)n);
                    put_be16(udp + 6, l4_csum(PEER_IP, s, IPPROTO_UDP, udp, (uint16_t)(8 + n)) ?: 0xFFFF);
                    peer_send_ip(PEER_IP, s, &g_our_mac, IPPROTO_UDP, udp, (uint16_t)(8 + n), false);
                }
            } else if (proto == IPPROTO_ICMP && l4len >= 8 && l4[0] == 8) {
                uint8_t rep[8 + 64]; int n = l4len < (int)sizeof(rep) ? l4len : (int)sizeof(rep);
                memcpy(rep, l4, (size_t)n); rep[0] = 0; put_be16(rep + 2, 0);
                put_be16(rep + 2, csum_fold(csum_add(0, rep, (unsigned)n)));
                peer_send_ip(PEER_IP, s, &g_our_mac, IPPROTO_ICMP, rep, (uint16_t)n, false);
            } else if (proto == IPPROTO_TCP) {
                tcp_server_handle(s, l4, l4len);
            }
        }
    }
    g_to_peer_n = 0;
}

 
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static int fails = 0;

int main(void) {
    mac_t our_mac = { { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 } };
    netif_t *nif = netif_create("usb0", &our_mac);
    CHECK(nif != NULL, "netif_create");
    nif->tx = wire_tx; nif->rx_poll = wire_rx; nif->drv = NULL;
    net_init();                                           
    netif_set_link(nif, true);                             

     
    uint64_t t0 = np_now_ms();
    while (!dhcp_bound() && np_now_ms() - t0 < 20000) np_yield();
    CHECK(dhcp_bound(), "DHCP bound");
    CHECK(nif->ip == OUR_IP, "assigned IP %08x", nif->ip);
    CHECK(nif->mask == NETMASK, "mask");
    CHECK(nif->gw == PEER_IP, "gateway == phone");
    CHECK(nif->dns[0] == PEER_IP, "dns == phone");
    char stbuf[200]; net_status_text(stbuf, sizeof stbuf);
    CHECK(strstr(stbuf, "192.168.42.2") != NULL, "status text has our ip: %s", stbuf);

     


    uint32_t rtt = 0xFFFFFFFF;
    int r = icmp_ping(nif->gw, 2000, &rtt);
    CHECK(r == 0, "ping gateway rc=%d", r);
    CHECK(rtt < 2000, "rtt sane (%u ms)", rtt);

     
    mac_t m;
    CHECK(arp_lookup(PEER_IP, &m) && memcmp(m.b, PEER_MAC.b, 6) == 0, "ARP cache learned gateway MAC via ping");
    r = icmp_ping(IP4(192, 168, 42, 250), 300, &rtt);             
    CHECK(r == -NET_ETIMEDOUT, "ping unreachable host times out, rc=%d", r);

     
    ip4_t resolved;
    r = dns_resolve("connectivitycheck.gstatic.com", &resolved, 3000);
    CHECK(r == 0 && resolved == DNS_A_RESULT, "DNS A record rc=%d ip=%08x", r, resolved);
    r = dns_resolve("connectivitycheck.gstatic.com", &resolved, 100);    
    CHECK(r == 0, "DNS cache hit");
    r = dns_resolve("nxdomain.example.invalid", &resolved, 3000);
    CHECK(r == -NET_ENOENT, "DNS NXDOMAIN rc=%d", r);
    ip4_t lit;
    CHECK(dns_resolve("8.8.8.8", &lit, 100) == 0 && lit == IP4(8, 8, 8, 8), "dns_resolve of literal IP");

     
    int us = net_socket(NET_SOCK_DGRAM);
    CHECK(us >= 0, "udp socket");
    const char *msg = "hello-udp";
    r = net_sendto(us, msg, (int)strlen(msg), PEER_IP, 7777);
    CHECK(r == (int)strlen(msg), "udp sendto rc=%d", r);
    char rbuf[64]; ip4_t rip; uint16_t rport;
    int n = net_recvfrom(us, rbuf, sizeof(rbuf) - 1, &rip, &rport, 2000);
    CHECK(n == (int)strlen(msg), "udp recvfrom len=%d", n);
    rbuf[n > 0 ? n : 0] = 0;
    CHECK(n > 0 && !strcmp(rbuf, msg), "udp echo content: %s", rbuf);
    CHECK(rip == PEER_IP, "udp echo from peer ip");
    net_close(us);

     
    int ts = net_socket(NET_SOCK_STREAM);
    CHECK(ts >= 0, "tcp socket");
    r = net_connect(ts, PEER_IP, 80, 3000);
    CHECK(r == 0, "tcp connect rc=%d", r);

    static char big[5000];
    for (int i = 0; i < 5000; i++) big[i] = (char)('A' + (i % 26));
    int sent_total = 0;
    while (sent_total < 5000) {
        int s = net_send(ts, big + sent_total, (size_t)(5000 - sent_total), 3000);
        CHECK(s > 0, "tcp send progress at %d (got %d)", sent_total, s);
        if (s <= 0) break;
        sent_total += s;
    }
    CHECK(sent_total == 5000, "tcp sent all 5000 bytes (%d)", sent_total);

    static char recvbuf[5000];
    int got = 0;
    uint64_t deadline = np_now_ms() + 5000;
    while (got < 5000 && np_now_ms() < deadline) {
        int rr = net_recv(ts, recvbuf + got, (size_t)(5000 - got), 500);
        if (rr > 0) got += rr;
        else if (rr < 0 && rr != -NET_EAGAIN) break;
    }
    CHECK(got == 5000, "tcp echo received all bytes (%d)", got);
    CHECK(memcmp(big, recvbuf, 5000) == 0, "tcp echo content matches (byte-exact, large + fragmented transfer)");

    ip4_t lip, rip2; uint16_t lport, rport2;
    CHECK(net_getsockname(ts, &lip, &lport) == 0 && lip == OUR_IP && lport >= 1024, "tcp getsockname");
    CHECK(net_getpeername(ts, &rip2, &rport2) == 0 && rip2 == PEER_IP && rport2 == 80, "tcp getpeername");

    net_shutdown(ts, 0);
    deadline = np_now_ms() + 3000;
    while (tcp_state(net_sock_valid(ts) ? 0 : 0), false) break;    
     
    int last = 1;
    while (np_now_ms() < deadline) { last = net_recv(ts, recvbuf, sizeof recvbuf, 200); if (last <= 0 && last != -NET_EAGAIN) break; }
    CHECK(last == 0 || last == -NET_ENOTCONN, "tcp graceful close observed (last=%d)", last);
    net_close(ts);

     
    {
        int s2 = net_socket(NET_SOCK_STREAM);
        r = net_connect(s2, PEER_IP, 80, 3000);
        CHECK(r == 0, "second tcp connect rc=%d", r);
         



        int us2 = net_socket(NET_SOCK_DGRAM);
        udp_bind(us2, 0, 9999);
        uint8_t bad[8 + 4];
        put_be16(bad, 12345); put_be16(bad + 2, 9999); put_be16(bad + 4, 12); put_be16(bad + 6, 0x1234  );
        memcpy(bad + 8, "xx", 2);
        peer_send_ip(PEER_IP, OUR_IP, &g_our_mac, IPPROTO_UDP, bad, 10, false);
        uint64_t d2 = np_now_ms() + 500;
        int got_bad = 0;
        while (np_now_ms() < d2) { char tmp[16]; ip4_t fi; uint16_t fp; int rr = net_recvfrom(us2, tmp, sizeof tmp, &fi, &fp, 50); if (rr > 0) got_bad = 1; }
        CHECK(!got_bad, "packet with bad UDP checksum is silently dropped");
        net_close(us2);
        net_close(s2);
    }

     
    netif_set_link(nif, false);
    CHECK(!nif->ip_valid, "link down clears ip");
    CHECK(!dhcp_bound(), "link down stops dhcp");

    printf("\n%s (iters=%d)\n", fails ? "FAILURES" : "ALL OK", g_iters); if (fails) printf("%d failing checks\n", fails);
    return fails != 0;
}
