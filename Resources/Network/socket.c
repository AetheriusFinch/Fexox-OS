/* Fexox OS Project, EULA GPLv2 */
 



#include "net.h"

typedef struct {
    bool used;
    int  type;       
    int  sub;         
} net_sock_t;

static net_sock_t g_sk[NET_MAX_SOCKETS];

bool net_sock_valid(int s) { return s >= 0 && s < NET_MAX_SOCKETS && g_sk[s].used; }

int net_socket(int type) {
    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM) return -NET_EPROTONOSUPPORT;
    int sub = (type == NET_SOCK_STREAM) ? tcp_open() : udp_open();
    if (sub < 0) return sub;

    uint64_t fl = net_lock();
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sk[i].used) {
            g_sk[i].used = true; g_sk[i].type = type; g_sk[i].sub = sub;
            net_unlock(fl);
            return i;
        }
    }
    net_unlock(fl);
    if (type == NET_SOCK_STREAM) tcp_close(sub); else udp_close(sub);
    return -NET_ENOMEM;
}

int net_bind(int s, ip4_t ip, uint16_t port) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    return g_sk[s].type == NET_SOCK_STREAM ? tcp_bind(g_sk[s].sub, ip, port) : udp_bind(g_sk[s].sub, ip, port);
}

int net_listen(int s, int backlog) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;
    return tcp_listen(g_sk[s].sub, backlog);
}

int net_accept(int s, ip4_t *rip, uint16_t *rport, uint32_t timeout_ms) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;
    uint64_t deadline = np_now_ms() + timeout_ms;
    for (;;) {
        int sub = tcp_accept_nb(g_sk[s].sub, rip, rport);
        if (sub != -NET_EAGAIN) {
            if (sub < 0) return sub;
            uint64_t fl = net_lock();
            int ns = -1;
            for (int i = 0; i < NET_MAX_SOCKETS; i++) if (!g_sk[i].used) { ns = i; break; }
            if (ns < 0) { net_unlock(fl); tcp_close(sub); return -NET_ENOMEM; }
            g_sk[ns].used = true; g_sk[ns].type = NET_SOCK_STREAM; g_sk[ns].sub = sub;
            net_unlock(fl);
            return ns;
        }
        if (timeout_ms != 0xFFFFFFFFu && np_now_ms() >= deadline) return -NET_EAGAIN;
        np_yield();
    }
}

int net_connect(int s, ip4_t ip, uint16_t port, uint32_t timeout_ms) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;     
    int sub = g_sk[s].sub;
    int r = tcp_connect_start(sub, ip, port);
     




    if (r == -NET_EISCONN) {
        int st0 = tcp_state(sub);
        if (st0 == TCPS_ESTABLISHED) return 0;
        if (st0 != TCPS_SYN_SENT) return r;
    } else if (r < 0) {
        return r;
    }
    uint64_t deadline = np_now_ms() + timeout_ms;
    for (;;) {
        int st = tcp_state(sub);
        if (st == TCPS_ESTABLISHED) return 0;
        if (st == TCPS_CLOSED) { int e = tcp_error(sub); return e ? e : -NET_ECONNREFUSED; }
        if (np_now_ms() >= deadline) return -NET_ETIMEDOUT;
        np_yield();
    }
}

int net_send(int s, const void *buf, size_t len, uint32_t timeout_ms) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;
    int sub = g_sk[s].sub;
    size_t sent = 0;
    uint64_t deadline = np_now_ms() + timeout_ms;
    while (sent < len) {
        int r = tcp_send_nb(sub, (const uint8_t *)buf + sent, len - sent);
        if (r > 0) { sent += (size_t)r; continue; }
        if (r == -NET_EAGAIN) {
            if (timeout_ms != 0xFFFFFFFFu && np_now_ms() >= deadline) break;
            np_yield();
            continue;
        }
        return sent > 0 ? (int)sent : r;
    }
    return (int)sent;
}

int net_recv(int s, void *buf, size_t len, uint32_t timeout_ms) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;
    int sub = g_sk[s].sub;
    uint64_t deadline = np_now_ms() + timeout_ms;
    for (;;) {
        int r = tcp_recv_nb(sub, buf, len);
        if (r != -NET_EAGAIN) return r;
        if (timeout_ms != 0xFFFFFFFFu && np_now_ms() >= deadline) return -NET_EAGAIN;
        np_yield();
    }
}

int net_sendto(int s, const void *buf, size_t len, ip4_t ip, uint16_t port) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_DGRAM) return -NET_EOPNOTSUPP;
    if (len > UDP_MAX_DGRAM) return -NET_EMSGSIZE;
    return udp_sendto(g_sk[s].sub, ip, port, buf, (uint16_t)len);
}

int net_recvfrom(int s, void *buf, size_t len, ip4_t *ip, uint16_t *port, uint32_t timeout_ms) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_DGRAM) return -NET_EOPNOTSUPP;
    int sub = g_sk[s].sub;
    uint64_t deadline = np_now_ms() + timeout_ms;
    for (;;) {
        int r = udp_recv_nb(sub, buf, (uint16_t)(len > 0xFFFF ? 0xFFFF : len), ip, port);
        if (r != 0) return r;
        if (timeout_ms != 0xFFFFFFFFu && np_now_ms() >= deadline) return -NET_EAGAIN;
        np_yield();
    }
}

int net_shutdown(int s, int how) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_EOPNOTSUPP;
    (void)how;                                              
    return tcp_shutdown_wr(g_sk[s].sub);
}

int net_close(int s) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    int r = g_sk[s].type == NET_SOCK_STREAM ? tcp_close(g_sk[s].sub) : udp_close(g_sk[s].sub);
    uint64_t fl = net_lock();
    g_sk[s].used = false;
    net_unlock(fl);
    return r;
}

int net_getsockname(int s, ip4_t *ip, uint16_t *port) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type == NET_SOCK_STREAM) {
        ip4_t l, r; uint16_t lp, rp;
        int rc = tcp_getnames(g_sk[s].sub, &l, &lp, &r, &rp);
        if (rc < 0) return rc;
        if (ip) *ip = l;
        if (port) *port = lp;
        return 0;
    }
    if (ip) *ip = 0;
    if (port) *port = udp_local_port(g_sk[s].sub);
    return 0;
}

int net_getpeername(int s, ip4_t *ip, uint16_t *port) {
    if (!net_sock_valid(s)) return -NET_EBADF;
    if (g_sk[s].type != NET_SOCK_STREAM) return -NET_ENOTCONN;
    ip4_t l, r; uint16_t lp, rp;
    int rc = tcp_getnames(g_sk[s].sub, &l, &lp, &r, &rp);
    if (rc < 0) return rc;
    if (r == 0) return -NET_ENOTCONN;
    if (ip) *ip = r;
    if (port) *port = rp;
    return 0;
}
