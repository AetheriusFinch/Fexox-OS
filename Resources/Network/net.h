/* Fexox OS Project, EULA GPLv2 */
 


#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 
 
 
uint64_t np_now_ms(void);                     
uint64_t np_irq_save(void);                   
void     np_irq_restore(uint64_t flags);
void     np_yield(void);                      
void     np_idle(void);                       
int      np_thread_create(void (*fn)(void *), void *arg, const char *name);
void     np_log(const char *msg);             
uint64_t np_entropy(void);                    
void     np_usb_hotplug(void);                

 
 
 
#define NET_EBADF          9
#define NET_EAGAIN         11
#define NET_ENOMEM         12
#define NET_EFAULT         14
#define NET_EINVAL         22
#define NET_EPIPE          32
#define NET_EPROTO         71
#define NET_EMSGSIZE       90
#define NET_EPROTONOSUPPORT 93
#define NET_EOPNOTSUPP     95
#define NET_EAFNOSUPPORT   97
#define NET_EADDRINUSE     98
#define NET_EADDRNOTAVAIL  99
#define NET_ENETDOWN       100
#define NET_ENETUNREACH    101
#define NET_ECONNABORTED   103
#define NET_ECONNRESET     104
#define NET_ENOBUFS        105
#define NET_EISCONN        106
#define NET_ENOTCONN       107
#define NET_ETIMEDOUT      110
#define NET_ECONNREFUSED   111
#define NET_EHOSTUNREACH   113
#define NET_EALREADY       114
#define NET_EINPROGRESS    115
#define NET_ENOENT         2
#define NET_ENOTSOCK       88

 
 
 
typedef uint32_t ip4_t;
#define IP4(a, b, c, d) ((((uint32_t)(a)) << 24) | (((uint32_t)(b)) << 16) | (((uint32_t)(c)) << 8) | ((uint32_t)(d)))
#define IP4_ANY        0u
#define IP4_BROADCAST  0xFFFFFFFFu

typedef struct { uint8_t b[6]; } mac_t;

#define NET_MTU        1500
#define NET_FRAME_MAX  1600

static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint16_t get_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t get_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t get_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

 
uint32_t net_csum_add(uint32_t sum, const uint8_t *data, size_t len);
static inline uint16_t net_csum_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

 
 
 
typedef struct netif {
    char     name[8];                  
    mac_t    mac;
    bool     used;
    bool     link_up;                  
    uint16_t mtu;

     
    int  (*tx)(struct netif *nif, const uint8_t *frame, uint16_t len);         
    int  (*rx_poll)(struct netif *nif, uint8_t *buf, uint16_t bufsz);          
    void (*service)(struct netif *nif);                                        
    void *drv;

     
    bool  ip_valid;
    ip4_t ip, mask, gw, dns[2];

     
    uint32_t rx_frames, tx_frames, rx_bytes, tx_bytes, rx_dropped, tx_errors;
} netif_t;

 
netif_t *net_default_if(void);
netif_t *netif_create(const char *name, const mac_t *mac);    
void     netif_remove(netif_t *nif);                          
void     netif_set_link(netif_t *nif, bool up);
void     netif_set_static(netif_t *nif, ip4_t ip, ip4_t mask, ip4_t gw, ip4_t dns);

 
uint64_t net_lock(void);
void     net_unlock(uint64_t flags);
uint64_t net_now_ms(void);
uint32_t net_rand32(void);
uint16_t net_ip_id_next(void);

 
int  net_init(void);
 
int  net_poll_once(void);

 
void net_status_text(char *out, size_t n);
 
void net_selftest_text(int line, char *out, size_t n);

 
 
 
#define ETH_HLEN       14
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806

void eth_input(netif_t *nif, const uint8_t *frame, uint16_t len);
 

int  eth_send_ip(netif_t *nif, ip4_t next_hop, const uint8_t *ip_pkt, uint16_t len);
void arp_tick(netif_t *nif);
void arp_announce(netif_t *nif);                  
bool arp_lookup(ip4_t ip, mac_t *out);
void arp_flush(void);                             

 
 
 
#define IPPROTO_ICMP   1
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17

void ip_input(netif_t *nif, const uint8_t *pkt, uint16_t len);
 
int  ip_send(ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *payload, uint16_t len);
 
int  ip_send_raw_broadcast(netif_t *nif, ip4_t src, uint8_t proto, const uint8_t *payload, uint16_t len);
bool ip_is_local(ip4_t a);                        
 

uint16_t net_l4_checksum(ip4_t src, ip4_t dst, uint8_t proto, const uint8_t *seg, uint16_t len);
ip4_t ip_route_next_hop(ip4_t dst);               

 
int  icmp_ping(ip4_t dst, uint32_t timeout_ms, uint32_t *rtt_ms);
void icmp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *data, uint16_t len);

 
 
 
#define UDP_MAX_SOCKS  8
#define UDP_RXQ        8
#define UDP_MAX_DGRAM  1472

void udp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *data, uint16_t len);
int  udp_open(void);                                                    
int  udp_bind(int s, ip4_t ip, uint16_t port);                          
int  udp_sendto(int s, ip4_t dst, uint16_t dport, const void *buf, uint16_t len);
 
int  udp_recv_nb(int s, void *buf, uint16_t bufsz, ip4_t *from, uint16_t *from_port);
int  udp_close(int s);
uint16_t udp_local_port(int s);
bool udp_is_open(int s);
bool udp_port_in_use(uint16_t port);
 
int  udp_send_bcast_noip(netif_t *nif, uint16_t sport, uint16_t dport, const uint8_t *data, uint16_t len);
bool tcp_port_in_use(uint16_t port);

 
 
 
void dhcp_start(netif_t *nif);                    
void dhcp_stop(void);
void dhcp_tick(netif_t *nif);
bool dhcp_bound(void);

 
int  dns_resolve(const char *name, ip4_t *out, uint32_t timeout_ms);
int  ip4_parse(const char *s, ip4_t *out);        
void ip4_format(ip4_t ip, char *out);             

 
 
 
#define TCP_MAX_SOCKS  10
#define TCP_BUF        16384

void tcp_input(netif_t *nif, ip4_t src, ip4_t dst, const uint8_t *seg, uint16_t len);
void tcp_tick(void);
void tcp_reset_all(void);                         

int  tcp_open(void);                              
int  tcp_bind(int s, ip4_t ip, uint16_t port);
int  tcp_listen(int s, int backlog);
int  tcp_accept_nb(int s, ip4_t *rip, uint16_t *rport);    
int  tcp_connect_start(int s, ip4_t ip, uint16_t port);    
int  tcp_send_nb(int s, const void *buf, size_t len);      
int  tcp_recv_nb(int s, void *buf, size_t len);            
int  tcp_shutdown_wr(int s);                      
int  tcp_close(int s);                            
int  tcp_getnames(int s, ip4_t *lip, uint16_t *lport, ip4_t *rip, uint16_t *rport);

enum { TCPS_CLOSED = 0, TCPS_LISTEN, TCPS_SYN_SENT, TCPS_SYN_RCVD, TCPS_ESTABLISHED,
       TCPS_FIN_WAIT1, TCPS_FIN_WAIT2, TCPS_CLOSE_WAIT, TCPS_CLOSING, TCPS_LAST_ACK, TCPS_TIME_WAIT };
int  tcp_state(int s);                            
int  tcp_error(int s);                            
size_t tcp_tx_space(int s);
size_t tcp_rx_avail(int s);

 
 
 
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM  2
#define NET_MAX_SOCKETS 24

int  net_socket(int type);
int  net_bind(int sock, ip4_t ip, uint16_t port);
int  net_listen(int sock, int backlog);
int  net_accept(int sock, ip4_t *rip, uint16_t *rport, uint32_t timeout_ms);
int  net_connect(int sock, ip4_t ip, uint16_t port, uint32_t timeout_ms);
int  net_send(int sock, const void *buf, size_t len, uint32_t timeout_ms);
int  net_recv(int sock, void *buf, size_t len, uint32_t timeout_ms);
int  net_sendto(int sock, const void *buf, size_t len, ip4_t ip, uint16_t port);
int  net_recvfrom(int sock, void *buf, size_t len, ip4_t *ip, uint16_t *port, uint32_t timeout_ms);
int  net_shutdown(int sock, int how);
int  net_close(int sock);
int  net_getsockname(int sock, ip4_t *ip, uint16_t *port);
int  net_getpeername(int sock, ip4_t *ip, uint16_t *port);
bool net_sock_valid(int sock);

 
 
 
struct usb_hcd;
 
int  usbnet_probe(struct usb_hcd *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                  const uint8_t *cfg, uint16_t cfg_len);
 
void usbnet_detach(struct usb_hcd *hcd, uint8_t addr);

#endif  
