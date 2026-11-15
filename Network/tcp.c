/* Fexox OS Project, EULA GPLv2 */
 











#include "net.h"

#define TCP_OOO_SLOTS 6

typedef struct {
    bool     used;
    int      state;
    bool     is_listener;

    ip4_t    lip, rip;
    uint16_t lport, rport;

     
    uint32_t snd_una, snd_nxt, snd_wnd, snd_iss;
    uint8_t  tx[TCP_BUF];
    uint32_t tx_head, tx_tail, tx_len;      
    uint32_t tx_unsent;                      

     
    uint32_t rcv_nxt, rcv_irs;
    uint8_t  rx[TCP_BUF];
    uint32_t rx_head, rx_tail, rx_len;

     
    uint32_t cwnd, ssthresh;
    uint16_t mss;
    uint64_t rto_ms, t_rto_set;
    uint32_t rtt_srtt, rtt_rttvar;           
    uint8_t  rtx_count;
    uint32_t dup_acks;
    uint32_t last_ack_seen;

    bool     fin_sent, fin_acked, fin_rx;
    bool     need_ack;
    uint64_t t_delack;
    uint64_t t_time_wait;
    int      error;                           

     


    struct { bool used; bool fin; uint16_t len; uint32_t seq; uint8_t data[NET_MTU]; } ooo[TCP_OOO_SLOTS];

     
    int      accept_q[8];
    uint8_t  accept_h, accept_n;
    int      backlog;
} tcp_sock_t;

static tcp_sock_t g_tcp[TCP_MAX_SOCKS];
static uint8_t    g_seg[NET_MTU];

#define SEQ_LT(a, b)  ((int32_t)((a) - (b)) < 0)
#define SEQ_LEQ(a, b) ((int32_t)((a) - (b)) <= 0)
#define SEQ_GT(a, b)  ((int32_t)((a) - (b)) > 0)
#define SEQ_GEQ(a, b) ((int32_t)((a) - (b)) >= 0)

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

bool tcp_port_in_use(uint16_t port) {
    for (int i = 0; i < TCP_MAX_SOCKS; i++)
        if (g_tcp[i].used && g_tcp[i].lport == port &&
            (g_tcp[i].is_listener || g_tcp[i].state != TCPS_CLOSED))
            return true;
    return false;
}

static uint16_t alloc_ephemeral(void) {
    for (int tries = 0; tries < 200; tries++) {
        uint16_t p = (uint16_t)(49152u + net_rand32() % 16384u);
        if (!tcp_port_in_use(p) && !udp_port_in_use(p)) return p;
    }
    return 0;
}

 
static uint32_t ring_put(uint8_t *ring, uint32_t *tail, uint32_t *len, const uint8_t *data, uint32_t n) {
    uint32_t space = TCP_BUF - *len;
    if (n > space) n = space;
    for (uint32_t i = 0; i < n; i++) { ring[*tail] = data[i]; *tail = (*tail + 1) % TCP_BUF; }
    *len += n;
    return n;
}
static uint32_t ring_peek(const uint8_t *ring, uint32_t head, uint32_t avail, uint8_t *out, uint32_t n) {
    if (n > avail) n = avail;
    for (uint32_t i = 0; i < n; i++) out[i] = ring[(head + i) % TCP_BUF];
    return n;
}
static uint32_t ring_get(uint8_t *ring, uint32_t *head, uint32_t *len, uint8_t *out, uint32_t n) {
    n = ring_peek(ring, *head, *len, out, n);
    *head = (*head + n) % TCP_BUF;
    *len -= n;
    return n;
}

 
static tcp_sock_t *find_conn(ip4_t lip, uint16_t lport, ip4_t rip, uint16_t rport) {
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        tcp_sock_t *t = &g_tcp[i];
        if (t->used && !t->is_listener && t->lport == lport && t->rport == rport &&
            t->rip == rip && (t->lip == 0 || t->lip == lip))
            return t;
    }
    return NULL;
}
static tcp_sock_t *find_listener(ip4_t lip, uint16_t lport) {
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        tcp_sock_t *t = &g_tcp[i];
        if (t->used && t->is_listener && t->lport == lport && (t->lip == 0 || t->lip == lip))
            return t;
    }
    return NULL;
}
static int idx_of(tcp_sock_t *t) { return (int)(t - g_tcp); }

 


static uint32_t g_tcp_dbg[10];      
void tcp_dbg(uint32_t out[10]) { for (int i = 0; i < 10; i++) out[i] = g_tcp_dbg[i]; }

 
static void tcp_send_seg(tcp_sock_t *t, uint8_t flags, uint32_t seq, const uint8_t *data, uint16_t dlen) {
    uint8_t *p = g_seg;
    if (flags & TCP_FLAG_SYN) g_tcp_dbg[4]++;
    put_be16(p + 0, t->lport); put_be16(p + 2, t->rport);
    put_be32(p + 4, seq);
    put_be32(p + 8, (flags & TCP_FLAG_ACK) ? t->rcv_nxt : 0);
     


    uint16_t hl = (flags & TCP_FLAG_SYN) ? 24 : 20;
    p[12] = (uint8_t)((hl / 4) << 4); p[13] = flags;
    uint32_t win = TCP_BUF - t->rx_len;
    put_be16(p + 14, (uint16_t)(win > 0xFFFF ? 0xFFFF : win));
    put_be16(p + 16, 0);
    put_be16(p + 18, 0);
    if (hl == 24) { p[20] = 2; p[21] = 4; put_be16(p + 22, 1360); }
    for (uint16_t i = 0; i < dlen; i++) p[hl + i] = data[i];
    uint16_t total = (uint16_t)(hl + dlen);
    put_be16(p + 16, net_l4_checksum(t->lip ? t->lip : (net_default_if() ? net_default_if()->ip : 0), t->rip, IPPROTO_TCP, p, total));
    ip_send(t->lip, t->rip, IPPROTO_TCP, p, total);
    if (flags & TCP_FLAG_ACK) { t->need_ack = false; }
}

 
static void tcp_send_rst_for(ip4_t lip, ip4_t rip, uint16_t lport, uint16_t rport,
                             uint32_t seq, uint32_t ack, bool ack_set) {
    uint8_t p[20];
    put_be16(p + 0, lport); put_be16(p + 2, rport);
    put_be32(p + 4, ack_set ? ack : 0);
    put_be32(p + 8, ack_set ? 0 : seq + 1);
    p[12] = 5 << 4; p[13] = (uint8_t)(TCP_FLAG_RST | (ack_set ? 0 : TCP_FLAG_ACK));
    put_be16(p + 14, 0); put_be16(p + 16, 0); put_be16(p + 18, 0);
    put_be16(p + 16, net_l4_checksum(lip, rip, IPPROTO_TCP, p, 20));
    ip_send(lip, rip, IPPROTO_TCP, p, 20);
}

static void tcp_reset(tcp_sock_t *t, int err) {
    if (t->state != TCPS_CLOSED && t->state != TCPS_LISTEN)
        tcp_send_seg(t, TCP_FLAG_RST | TCP_FLAG_ACK, t->snd_nxt, NULL, 0);
    t->state = TCPS_CLOSED;
    t->error = err;
}

 
int tcp_open(void) {
    uint64_t fl = net_lock();
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        if (!g_tcp[i].used) {
            tcp_sock_t *t = &g_tcp[i];
            for (size_t k = 0; k < sizeof(*t); k++) ((uint8_t *)t)[k] = 0;
            t->used = true;
            t->state = TCPS_CLOSED;
            t->mss = (uint16_t)(NET_MTU - 40);
            t->cwnd = t->mss * 2u;
            t->ssthresh = 65535;
            t->rto_ms = 1000;
            net_unlock(fl);
            return i;
        }
    }
    net_unlock(fl);
    return -NET_ENOMEM;
}

int tcp_bind(int s, ip4_t ip, uint16_t port) {
    uint64_t fl = net_lock();
    int r = 0;
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) r = -NET_EBADF;
    else if (g_tcp[s].state != TCPS_CLOSED) r = -NET_EINVAL;
    else if (port && tcp_port_in_use(port)) r = -NET_EADDRINUSE;
    else { g_tcp[s].lip = ip; g_tcp[s].lport = port; }
    net_unlock(fl);
    return r;
}

int tcp_listen(int s, int backlog) {
    uint64_t fl = net_lock();
    int r = 0;
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) r = -NET_EBADF;
    else {
        tcp_sock_t *t = &g_tcp[s];
        if (t->lport == 0) t->lport = alloc_ephemeral();
        if (!t->lport) r = -NET_EADDRINUSE;
        else { t->is_listener = true; t->state = TCPS_LISTEN; t->backlog = backlog > 8 ? 8 : (backlog < 1 ? 1 : backlog); }
    }
    net_unlock(fl);
    return r;
}

int tcp_accept_nb(int s, ip4_t *rip, uint16_t *rport) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used || !g_tcp[s].is_listener) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->accept_n == 0) { net_unlock(fl); return -NET_EAGAIN; }
    int ns = t->accept_q[t->accept_h];
    t->accept_h = (uint8_t)((t->accept_h + 1) % 8);
    t->accept_n--;
    if (rip) *rip = g_tcp[ns].rip;
    if (rport) *rport = g_tcp[ns].rport;
    net_unlock(fl);
    return ns;
}

int tcp_connect_start(int s, ip4_t ip, uint16_t port) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->state != TCPS_CLOSED) { net_unlock(fl); return -NET_EISCONN; }
    netif_t *nif = net_default_if();
    if (!nif || !nif->ip_valid || !nif->link_up) { net_unlock(fl); return -NET_ENETUNREACH; }
    if (ip_route_next_hop(ip) == 0) { net_unlock(fl); return -NET_ENETUNREACH; }
    if (!t->lport) t->lport = alloc_ephemeral();
    if (!t->lport) { net_unlock(fl); return -NET_EADDRINUSE; }
    t->lip = nif->ip; t->rip = ip; t->rport = port;
    t->snd_iss = net_rand32();
    t->snd_una = t->snd_iss; t->snd_nxt = t->snd_iss + 1;
    t->snd_wnd = 0xFFFF;
    t->state = TCPS_SYN_SENT;
    t->rto_ms = 1000; t->t_rto_set = np_now_ms(); t->rtx_count = 0;
    tcp_send_seg(t, TCP_FLAG_SYN, t->snd_iss, NULL, 0);
    net_unlock(fl);
    return 0;
}

int tcp_state(int s)  { return (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) ? -NET_EBADF : g_tcp[s].state; }
int tcp_error(int s)  { return (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) ? -NET_EBADF : g_tcp[s].error; }
size_t tcp_tx_space(int s) { return (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) ? 0 : TCP_BUF - g_tcp[s].tx_len; }
size_t tcp_rx_avail(int s) { return (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) ? 0 : g_tcp[s].rx_len; }

int tcp_getnames(int s, ip4_t *lip, uint16_t *lport, ip4_t *rip, uint16_t *rport) {
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) return -NET_EBADF;
    tcp_sock_t *t = &g_tcp[s];
    if (lip) *lip = t->lip;
    if (lport) *lport = t->lport;
    if (rip) *rip = t->rip;
    if (rport) *rport = t->rport;
    return 0;
}

 
int tcp_send_nb(int s, const void *buf, size_t len) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->fin_sent) { net_unlock(fl); return -NET_EPIPE; }
    if (t->state != TCPS_ESTABLISHED && t->state != TCPS_CLOSE_WAIT) {
        net_unlock(fl);
        return (t->state == TCPS_SYN_SENT || t->state == TCPS_SYN_RCVD) ? -NET_EAGAIN : -NET_ENOTCONN;
    }
    uint32_t n = ring_put(t->tx, &t->tx_tail, &t->tx_len, (const uint8_t *)buf, (uint32_t)len);
    t->tx_unsent += n;
    net_unlock(fl);
    return (int)n;
}

int tcp_recv_nb(int s, void *buf, size_t len) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->rx_len > 0) {
        uint32_t n = ring_get(t->rx, &t->rx_head, &t->rx_len, (uint8_t *)buf, (uint32_t)len);
        t->need_ack = true;                                  
        net_unlock(fl);
        return (int)n;
    }
    int r;
    if (t->fin_rx) r = 0;
    else if (t->error) r = t->error;
    else if (t->state == TCPS_ESTABLISHED || t->state == TCPS_FIN_WAIT1 || t->state == TCPS_FIN_WAIT2) r = -NET_EAGAIN;
    else if (t->state == TCPS_SYN_SENT || t->state == TCPS_SYN_RCVD) r = -NET_EAGAIN;
    else r = -NET_ENOTCONN;
    net_unlock(fl);
    return r;
}

int tcp_shutdown_wr(int s) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->state == TCPS_ESTABLISHED) t->state = TCPS_FIN_WAIT1;
    else if (t->state == TCPS_CLOSE_WAIT) t->state = TCPS_LAST_ACK;
    net_unlock(fl);
    return 0;
}

int tcp_close(int s) {
    uint64_t fl = net_lock();
    if (s < 0 || s >= TCP_MAX_SOCKS || !g_tcp[s].used) { net_unlock(fl); return -NET_EBADF; }
    tcp_sock_t *t = &g_tcp[s];
    if (t->is_listener) {
        for (uint8_t i = 0; i < t->accept_n; i++) {
            int ns = t->accept_q[(t->accept_h + i) % 8];
            tcp_reset(&g_tcp[ns], -NET_ECONNABORTED);
            g_tcp[ns].used = false;
        }
        t->used = false;
    } else if (t->state == TCPS_ESTABLISHED || t->state == TCPS_CLOSE_WAIT) {
        if (t->state == TCPS_ESTABLISHED) t->state = TCPS_FIN_WAIT1; else t->state = TCPS_LAST_ACK;
         
    } else if (t->state == TCPS_SYN_SENT || t->state == TCPS_LISTEN) {
        t->state = TCPS_CLOSED; t->used = false;
    } else if (t->state != TCPS_CLOSED) {
        tcp_reset(t, 0);
        t->used = false;
    } else {
        t->used = false;
    }
    net_unlock(fl);
    return 0;
}

void tcp_reset_all(void) {
    for (int i = 0; i < TCP_MAX_SOCKS; i++)
        if (g_tcp[i].used && !g_tcp[i].is_listener && g_tcp[i].state != TCPS_CLOSED) {
            g_tcp[i].state = TCPS_CLOSED;
            g_tcp[i].error = -NET_ENETDOWN;
        }
}

 
static void tcp_output(tcp_sock_t *t) {
    if (t->state == TCPS_CLOSED || t->state == TCPS_LISTEN) return;

    uint32_t inflight = t->snd_nxt - t->snd_una;
    uint32_t cwin = t->cwnd < t->snd_wnd ? t->cwnd : t->snd_wnd;
    if (cwin < t->mss && t->tx_unsent > 0 && inflight == 0) cwin = t->mss;     

    while (t->tx_unsent > 0 && inflight < cwin) {
        uint32_t room = cwin - inflight;
        uint32_t chunk = t->tx_unsent < t->mss ? t->tx_unsent : t->mss;
        if (chunk > room) chunk = room;
        if (chunk == 0) break;
        uint32_t off = t->tx_len - t->tx_unsent;                               
        uint8_t data[1460 > TCP_BUF ? TCP_BUF : 1460];
        uint32_t got = ring_peek(t->tx, (t->tx_head + off) % TCP_BUF, t->tx_unsent, data, chunk);
        uint8_t flags = TCP_FLAG_ACK;
        bool sends_fin = false;
        if (got == t->tx_unsent && t->fin_sent == false &&
            (t->state == TCPS_FIN_WAIT1 || t->state == TCPS_LAST_ACK) &&
            got < sizeof(data)) {
             

        }
        tcp_send_seg(t, flags, t->snd_nxt, data, (uint16_t)got);
        t->snd_nxt += got;
        t->tx_unsent -= got;
        inflight += got;
        (void)sends_fin;
        if (t->rtx_count == 0) { t->t_rto_set = np_now_ms(); }
    }

    bool want_fin = (t->state == TCPS_FIN_WAIT1 || t->state == TCPS_LAST_ACK) && !t->fin_sent && t->tx_unsent == 0;
    if (want_fin) {
        tcp_send_seg(t, (uint8_t)(TCP_FLAG_FIN | TCP_FLAG_ACK), t->snd_nxt, NULL, 0);
        t->fin_sent = true;
        t->snd_nxt += 1;
        t->t_rto_set = np_now_ms();
    } else if (t->need_ack) {
        tcp_send_seg(t, TCP_FLAG_ACK, t->snd_nxt, NULL, 0);
    }
}

 
static void tcp_update_rtt(tcp_sock_t *t, uint32_t rtt_ms) {
    if (t->rtt_srtt == 0) { t->rtt_srtt = rtt_ms * 8; t->rtt_rttvar = rtt_ms * 4; }
    else {
        int32_t err = (int32_t)rtt_ms - (int32_t)(t->rtt_srtt / 8);
        t->rtt_srtt = (uint32_t)((int32_t)t->rtt_srtt + err);
        t->rtt_rttvar = (uint32_t)((int32_t)t->rtt_rttvar + ((err < 0 ? -err : err) - (int32_t)(t->rtt_rttvar / 4)));
    }
    uint32_t rto = t->rtt_srtt / 8 + (t->rtt_rttvar > 4 ? t->rtt_rttvar / 4 * 4 : 4);
    if (rto < 200) rto = 200;
    if (rto > 60000) rto = 60000;
    t->rto_ms = rto;
}

static void tcp_process_ack(tcp_sock_t *t, uint32_t ack, uint16_t wnd) {
    if (SEQ_GT(ack, t->snd_nxt)) return;                                        
    t->snd_wnd = wnd;
    if (SEQ_GT(ack, t->snd_una)) {
        uint32_t acked = ack - t->snd_una;
        uint32_t drop = acked < t->tx_len - t->tx_unsent ? acked : t->tx_len - t->tx_unsent;
        t->tx_head = (t->tx_head + drop) % TCP_BUF;
        t->tx_len -= drop;
        t->snd_una = ack;
        t->dup_acks = 0;
        t->rtx_count = 0;
        t->t_rto_set = np_now_ms();
        if (t->cwnd < t->ssthresh) t->cwnd += t->mss;               
        else t->cwnd += (t->mss * t->mss) / (t->cwnd ? t->cwnd : t->mss);    
        if (t->cwnd > 0xFFFFu * 4u) t->cwnd = 0xFFFFu * 4u;
        if (t->fin_sent && ack == t->snd_nxt) t->fin_acked = true;
        tcp_update_rtt(t, (uint32_t)(np_now_ms() - t->t_rto_set + t->rto_ms));   
    } else if (ack == t->snd_una && t->tx_len > t->tx_unsent) {
        t->dup_acks++;
        if (t->dup_acks == 3) {                                     
            t->ssthresh = (t->snd_nxt - t->snd_una) / 2; if (t->ssthresh < t->mss * 2u) t->ssthresh = t->mss * 2u;
            t->cwnd = t->ssthresh;
            t->tx_unsent = t->tx_len;                                
            t->snd_nxt = t->snd_una;
        }
    }
}

static void tcp_rtx_tick(tcp_sock_t *t) {
    if (t->state == TCPS_CLOSED || t->state == TCPS_LISTEN || t->state == TCPS_TIME_WAIT) return;
    bool unacked = SEQ_LT(t->snd_una, t->snd_nxt);
    if (!unacked) return;
    if (np_now_ms() - t->t_rto_set < t->rto_ms) return;

    t->rtx_count++;
    g_tcp_dbg[9]++;
    if (t->rtx_count > 8) { tcp_reset(t, -NET_ETIMEDOUT); return; }
    t->ssthresh = (t->snd_nxt - t->snd_una) / 2; if (t->ssthresh < t->mss * 2u) t->ssthresh = t->mss * 2u;
    t->cwnd = t->mss;                                                 
    t->tx_unsent = t->tx_len;                                         
    t->snd_nxt = t->snd_una;
    t->rto_ms *= 2; if (t->rto_ms > 60000) t->rto_ms = 60000;
    t->t_rto_set = np_now_ms();
    tcp_output(t);
}

 
 

static void tcp_fin_received(tcp_sock_t *t) {
    t->rcv_nxt += 1;
    t->fin_rx = true;
    t->need_ack = true;
    if (t->state == TCPS_ESTABLISHED) t->state = TCPS_CLOSE_WAIT;
    else if (t->state == TCPS_FIN_WAIT1) t->state = TCPS_CLOSING;
    else if (t->state == TCPS_FIN_WAIT2) t->state = TCPS_TIME_WAIT;
}

void tcp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *seg, uint16_t len) {
    (void)nif;
    if (len < 20) return;
    uint16_t doff = (uint16_t)((seg[12] >> 4) * 4);
    if (doff < 20 || doff > len) return;
    g_tcp_dbg[0]++;
    if (net_l4_checksum(src, dst, IPPROTO_TCP, seg, len) != 0) { g_tcp_dbg[1]++; return; }

    uint16_t sport = get_be16(seg), dport = get_be16(seg + 2);
    uint32_t seq = get_be32(seg + 4), ack = get_be32(seg + 8);
    uint8_t  flags = seg[13];
    uint16_t wnd = get_be16(seg + 14);
    const uint8_t *data = seg + doff;
    uint16_t dlen = (uint16_t)(len - doff);

    tcp_sock_t *t = find_conn(dst, dport, src, sport);
    if (flags & TCP_FLAG_RST) g_tcp_dbg[3]++;
    if (!t) g_tcp_dbg[2]++;

    if (!t) {
        tcp_sock_t *l = find_listener(dst, dport);
        if (l && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
            if (l->accept_n >= (uint8_t)l->backlog) return;             
            int ns = -1;
            for (int i = 0; i < TCP_MAX_SOCKS; i++) if (!g_tcp[i].used) { ns = i; break; }
            if (ns < 0) return;
            tcp_sock_t *c = &g_tcp[ns];
            for (size_t k = 0; k < sizeof(*c); k++) ((uint8_t *)c)[k] = 0;
            c->used = true; c->state = TCPS_SYN_RCVD;
            c->lip = dst; c->lport = dport; c->rip = src; c->rport = sport;
            c->rcv_nxt = seq + 1; c->rcv_irs = seq;
            c->snd_iss = net_rand32(); c->snd_una = c->snd_iss; c->snd_nxt = c->snd_iss + 1;
            c->snd_wnd = wnd; c->mss = (uint16_t)(NET_MTU - 40);
            c->cwnd = c->mss * 2u; c->ssthresh = 65535; c->rto_ms = 1000; c->t_rto_set = np_now_ms();
            tcp_send_seg(c, (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), c->snd_iss, NULL, 0);
            l->accept_q[(l->accept_h + l->accept_n) % 8] = ns;           
             


            l->accept_n++;                                              
            return;
        }
        if (!(flags & TCP_FLAG_RST))
            tcp_send_rst_for(dst, src, dport, sport, seq, ack, (flags & TCP_FLAG_ACK) != 0);
        return;
    }

    if (flags & TCP_FLAG_RST) {
        t->state = TCPS_CLOSED;
        t->error = -NET_ECONNRESET;
        return;
    }

    switch (t->state) {
    case TCPS_SYN_SENT:
        if (flags & TCP_FLAG_ACK) {
            if (ack != t->snd_nxt) { tcp_send_rst_for(dst, src, dport, sport, seq, ack, true); return; }
        }
        if (flags & TCP_FLAG_SYN) {
            t->rcv_nxt = seq + 1; t->rcv_irs = seq;
            t->snd_wnd = wnd;
            if (flags & TCP_FLAG_ACK) {
                g_tcp_dbg[5]++;
                t->snd_una = ack;
                t->state = TCPS_ESTABLISHED;
                tcp_send_seg(t, TCP_FLAG_ACK, t->snd_nxt, NULL, 0);
                np_log("[TCP] connected");
            } else {
                t->state = TCPS_SYN_RCVD;                               
                tcp_send_seg(t, (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), t->snd_iss, NULL, 0);
            }
        }
        return;

    case TCPS_SYN_RCVD:
        if (!(flags & TCP_FLAG_ACK)) return;
        if (ack != t->snd_nxt) { tcp_send_rst_for(dst, src, dport, sport, seq, ack, true); return; }
        t->snd_una = ack;
        t->state = TCPS_ESTABLISHED;
         
        for (int i = 0; i < TCP_MAX_SOCKS; i++) {
            tcp_sock_t *l = &g_tcp[i];
            if (!l->used || !l->is_listener || l->lport != t->lport) continue;
             
            break;
        }
        break;                                                           

    default:
        break;
    }

    if (t->state == TCPS_CLOSED || t->state == TCPS_LISTEN) return;

     


    if (dlen > 0 || (flags & TCP_FLAG_SYN)) {
        if (seq == t->rcv_nxt) {
            uint32_t room = TCP_BUF - t->rx_len;
            uint32_t take = dlen < room ? dlen : room;
            ring_put(t->rx, &t->rx_tail, &t->rx_len, data, take);
            t->rcv_nxt += take;
            g_tcp_dbg[8] += take;
            t->need_ack = true;
            if (take < dlen) t->need_ack = true;                         
             
            for (int guard = 0; guard < TCP_OOO_SLOTS * 2; guard++) {
                int hit = -1;
                for (int k = 0; k < TCP_OOO_SLOTS; k++) {
                    if (!t->ooo[k].used) continue;
                    uint32_t end = t->ooo[k].seq + t->ooo[k].len;
                    if (SEQ_LEQ(end, t->rcv_nxt)) { t->ooo[k].used = false; continue; }       
                    if (SEQ_LEQ(t->ooo[k].seq, t->rcv_nxt)) { hit = k; break; }
                }
                if (hit < 0) break;
                uint32_t skip = t->rcv_nxt - t->ooo[hit].seq;
                uint32_t n2 = t->ooo[hit].len - skip;
                uint32_t room2 = TCP_BUF - t->rx_len;
                if (n2 > room2) n2 = room2;
                ring_put(t->rx, &t->rx_tail, &t->rx_len, t->ooo[hit].data + skip, n2);
                t->rcv_nxt += n2;
                g_tcp_dbg[8] += n2;
                t->ooo[hit].used = false;
                if (t->ooo[hit].fin && t->rcv_nxt == t->ooo[hit].seq + t->ooo[hit].len && !t->fin_rx) {
                     
                    tcp_fin_received(t);
                    break;
                }
            }
        } else if (SEQ_LT(seq, t->rcv_nxt)) {
            g_tcp_dbg[7]++;
            t->need_ack = true;                                          
        } else if (dlen > 0 && dlen <= NET_MTU && (int32_t)(seq - t->rcv_nxt) < (int32_t)(TCP_BUF - t->rx_len)) {
             

            int slot = -1;
            for (int k = 0; k < TCP_OOO_SLOTS; k++) {
                if (t->ooo[k].used && t->ooo[k].seq == seq) { slot = -2; break; }
                if (!t->ooo[k].used && slot < 0) slot = k;
            }
            if (slot >= 0) {
                t->ooo[slot].used = true; t->ooo[slot].seq = seq; t->ooo[slot].len = dlen;
                t->ooo[slot].fin = (flags & TCP_FLAG_FIN) != 0;
                for (uint16_t i = 0; i < dlen; i++) t->ooo[slot].data[i] = data[i];
                g_tcp_dbg[6]++;
            }
            t->need_ack = true;
        }
    }
    if (flags & TCP_FLAG_FIN) {
        uint32_t fin_seq = seq + dlen;
        if (fin_seq == t->rcv_nxt) tcp_fin_received(t);
    }

    if (flags & TCP_FLAG_ACK) {
        tcp_process_ack(t, ack, wnd);
        if (t->fin_acked) {
            if (t->state == TCPS_FIN_WAIT1) t->state = TCPS_FIN_WAIT2;
            else if (t->state == TCPS_CLOSING) t->state = TCPS_TIME_WAIT;
            else if (t->state == TCPS_LAST_ACK) { t->state = TCPS_CLOSED; t->used = false; }
        }
    }

    if (t->state == TCPS_TIME_WAIT && t->t_time_wait == 0) t->t_time_wait = np_now_ms();

    tcp_output(t);
}

 
void tcp_tick(void) {
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        tcp_sock_t *t = &g_tcp[i];
        if (!t->used || t->is_listener) continue;
        if (t->state == TCPS_CLOSED) continue;

        if (t->state == TCPS_TIME_WAIT) {
            if (np_now_ms() - t->t_time_wait >= 30000) { t->state = TCPS_CLOSED; t->used = false; }
            continue;
        }

        if (t->tx_unsent > 0 || t->need_ack ||
            ((t->state == TCPS_FIN_WAIT1 || t->state == TCPS_LAST_ACK) && !t->fin_sent))
            tcp_output(t);

        tcp_rtx_tick(t);

         
        if (t->state == TCPS_CLOSED && t->used && t->fin_acked) t->used = false;
    }
}
