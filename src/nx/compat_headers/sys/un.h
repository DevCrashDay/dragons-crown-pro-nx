#pragma once
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifndef ESHUTDOWN
#define ESHUTDOWN 108
#endif

struct sockaddr_un {
    sa_family_t sun_family;
    char sun_path[108];
};

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct ipv6_mreq {
    uint8_t ipv6mr_multiaddr[16];
    unsigned int ipv6mr_interface;
};

static inline char *if_indextoname(unsigned int ifindex, char *ifname) {
    (void)ifindex;
    (void)ifname;
    return NULL;
}

static inline unsigned int if_nametoindex(const char *ifname) {
    (void)ifname;
    return 0;
}
