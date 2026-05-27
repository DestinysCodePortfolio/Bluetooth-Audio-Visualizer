#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// 1. Tell lwIP it is running bare-metal background (No Operating System)
#define NO_SYS                      1

// 2. Disable lwIP's custom sockets/sequential APIs to avoid 'struct timeval' conflicts
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// 3. Match structural sanity checks (Fixes MEMP_NUM_TCP_SEG error)
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_PBUF               10
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            32 // Increased to match or exceed TCP_SND_QUEUELEN
#define SYS_LIGHTWEIGHT_PROT        1
#define PBUF_POOL_SIZE              8

// Enabled features
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUE_LEN             16

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1

#endif