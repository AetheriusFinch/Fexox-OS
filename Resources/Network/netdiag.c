/* Fexox OS Project, EULA GPLv2 */
 




#include "net.h"

static char *cp(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

static void line(char *out, size_t n, const char *label, bool ok, const char *detail) {
    char b[128]; char *d = b;
    d = cp(d, ok ? "[OK] " : "[--] ");
    d = cp(d, label);
    if (detail && *detail) { d = cp(d, ": "); d = cp(d, detail); }
    *d = '\0';
    size_t i = 0; while (b[i] && i + 1 < n) { out[i] = b[i]; i++; } if (n) out[i] = '\0';
}

 
static int http_check(ip4_t ip, uint32_t timeout_ms, char *status_out, size_t status_n) {
    int s = net_socket(NET_SOCK_STREAM);
    if (s < 0) return s;
    int r = net_connect(s, ip, 80, timeout_ms);
    if (r < 0) { net_close(s); return r; }
    static const char req[] = "HEAD / HTTP/1.0\r\nHost: connectivitycheck\r\nConnection: close\r\n\r\n";
    r = net_send(s, req, sizeof(req) - 1, timeout_ms);
    if (r < 0) { net_close(s); return r; }
    char buf[128]; int n = net_recv(s, buf, sizeof(buf) - 1, timeout_ms);
    net_close(s);
    if (n <= 0) return n < 0 ? n : -NET_ECONNRESET;
    buf[n] = '\0';
    size_t i = 0; while (buf[i] && buf[i] != '\r' && buf[i] != '\n' && i + 1 < status_n) { status_out[i] = buf[i]; i++; }
    status_out[i] = '\0';
    return 0;
}

 
void net_selftest_text(int step, char *out, size_t n) {
    netif_t *nif = net_default_if();

    if (step == 0) {
        if (!nif) line(out, n, "USB modem", false, "not found - enable USB tethering on phone");
        else if (!nif->link_up) line(out, n, "USB modem", false, "link down");
        else line(out, n, "USB modem", true, nif->name);
        return;
    }
    if (!nif || !nif->link_up) { line(out, n, "(skipped)", false, ""); return; }

    if (step == 1) {
        uint64_t deadline = np_now_ms() + 8000;
        while (!dhcp_bound() && np_now_ms() < deadline) np_yield();
        if (dhcp_bound()) {
            char ip[20]; ip4_format(nif->ip, ip);
            line(out, n, "DHCP", true, ip);
        } else line(out, n, "DHCP", false, "no response from phone");
        return;
    }
    if (!nif->ip_valid) { line(out, n, "(skipped)", false, ""); return; }

    if (step == 2) {
        uint32_t rtt;
        int r = icmp_ping(nif->gw, 2000, &rtt);
        if (r == 0) { char t[24]; int p = 0; t[p++]='0'+(char)((rtt/100)%10); (void)p; char b2[8]; int m=0; uint32_t v=rtt; if(!v)b2[m++]='0'; while(v){b2[m++]=(char)('0'+v%10);v/=10;} char b3[9]; int k=0; while(m>0)b3[k++]=b2[--m]; b3[k]=0; cp(t,b3); cp(t+k," ms"); line(out, n, "Ping gateway", true, t); }
        else line(out, n, "Ping gateway", false, "no reply");
        return;
    }

    if (step == 3) {
        ip4_t ip;
        int r = dns_resolve("connectivitycheck.gstatic.com", &ip, 4000);
        if (r == 0) { char t[20]; ip4_format(ip, t); line(out, n, "DNS", true, t); }
        else line(out, n, "DNS", false, "no answer");
        return;
    }

    if (step == 4) {
        ip4_t ip;
        if (dns_resolve("connectivitycheck.gstatic.com", &ip, 4000) != 0) { line(out, n, "Internet (HTTP)", false, "DNS failed"); return; }
        char status[64];
        int r = http_check(ip, 4000, status, sizeof status);
        if (r == 0) line(out, n, "Internet (HTTP)", true, status);
        else line(out, n, "Internet (HTTP)", false, "connection failed");
        return;
    }

    line(out, n, "", false, "");
}
