/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Router port mapping for the Host Lobby (net_portmap.h).
 *
 * NAT-PMP (RFC 6886) talks to the default gateway on UDP 5351; UPnP-IGD
 * finds the router with SSDP (UDP multicast to 239.255.255.250:1900), reads
 * its device description over HTTP and calls the WANIPConnection (or
 * WANPPPConnection) service with SOAP: GetExternalIPAddress, AddPortMapping,
 * DeletePortMapping. Both are standard parts of home routers; nothing here
 * talks to anything but the player's own router.
 *
 * Everything blocking runs on one background thread; the game reads the
 * result through the getters. */
#include "net_portmap.h"

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef SOCKET PmSocket;
#define PM_INVALID INVALID_SOCKET
#define pm_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int PmSocket;
#define PM_INVALID (-1)
#define pm_close close
#endif
#if defined(__APPLE__)
#include <net/route.h>
#include <sys/sysctl.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void pc_log_line(const char* fmt, ...) {
    (void)fmt;
}
#else
void pc_log_line(const char* fmt, ...);
#endif

#define LEASE_S 7200     /* asked for; renewed well before it runs out */
#define RENEW_MS 1800000 /* every 30 minutes while the lobby is open */

static SDL_Mutex* s_lock;
static SDL_Thread* s_thread;
static SDL_AtomicInt s_stop;
static SDL_AtomicInt s_state;
static uint32_t s_external; /* host order, under s_lock */
static uint16_t s_ext_port; /* under s_lock */
static uint16_t s_internal; /* set before the thread starts */
static const char* s_method = "";

/* What the thread mapped, so the same thread can remove it. */
static int s_mapped_by;     /* 0 none, 1 NAT-PMP, 2 UPnP */
static uint32_t s_gateway;  /* network order */
static char s_control[256]; /* UPnP control URL */
static char s_service[96];  /* UPnP service type */

static void set_result(enum PcPortmapState st, uint32_t ext, uint16_t port, const char* method) {
    SDL_LockMutex(s_lock);
    s_external = ext;
    s_ext_port = port;
    s_method = method;
    SDL_UnlockMutex(s_lock);
    SDL_SetAtomicInt(&s_state, st);
}

static bool private_address(uint32_t a) { /* host order */
    return (a >> 24) == 10 || (a >> 20) == (172 << 4 | 1) || (a >> 16) == (192 << 8 | 168) ||
           (a >> 22) == (100 << 2 | 1) /* 100.64/10, carrier-grade NAT */ || a == 0;
}

/* ---- the default gateway ------------------------------------------------ */

static uint32_t default_gateway(void) { /* network order, 0 if unknown */
#if defined(_WIN32)
    MIB_IPFORWARDROW row;
    if (GetBestRoute(inet_addr("1.1.1.1"), 0, &row) == NO_ERROR) {
        return row.dwForwardNextHop; /* a route lookup only: nothing is sent */
    }
    return 0;
#elif defined(__APPLE__)
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
    size_t len = 0;
    if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0 || len == 0) {
        return 0;
    }
    char* buf = malloc(len);
    uint32_t gw = 0;
    if (buf != NULL && sysctl(mib, 6, buf, &len, NULL, 0) == 0) {
        for (char* p = buf; p < buf + len && gw == 0;) {
            const struct rt_msghdr* rtm = (const struct rt_msghdr*)p;
            const struct sockaddr* sa = (const struct sockaddr*)(rtm + 1);
            const struct sockaddr* addrs[RTAX_MAX] = {0};
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    addrs[i] = sa;
                    const size_t step = sa->sa_len > 0 ? ((sa->sa_len + 3) & ~(size_t)3) : 4;
                    sa = (const struct sockaddr*)((const char*)sa + step);
                }
            }
            const struct sockaddr* dst = addrs[RTAX_DST];
            const struct sockaddr* gate = addrs[RTAX_GATEWAY];
            if (dst != NULL && gate != NULL && dst->sa_family == AF_INET &&
                gate->sa_family == AF_INET &&
                ((const struct sockaddr_in*)dst)->sin_addr.s_addr == 0)
            {
                gw = ((const struct sockaddr_in*)gate)->sin_addr.s_addr;
            }
            p += rtm->rtm_msglen;
        }
    }
    free(buf);
    return gw;
#else
    FILE* f = fopen("/proc/net/route", "r");
    char line[256];
    uint32_t gw = 0;
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL && gw == 0) {
        char iface[64];
        unsigned dest, gate, flags;
        if (sscanf(line, "%63s %x %x %x", iface, &dest, &gate, &flags) == 4 && dest == 0 &&
            (flags & 2))
        {
            gw = gate; /* already network order on little-endian hosts */
        }
    }
    fclose(f);
    return gw;
#endif
}

/* Our address on the route to `peer` (network order): what the router sees. */
static uint32_t local_address_toward(uint32_t peer) {
    PmSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0};
    uint32_t out = 0;
    if (s == PM_INVALID) {
        return 0;
    }
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    a.sin_addr.s_addr = peer;
    if (connect(s, (struct sockaddr*)&a, sizeof a) == 0) {
        struct sockaddr_in me;
        socklen_t n = sizeof me;
        if (getsockname(s, (struct sockaddr*)&me, &n) == 0) {
            out = me.sin_addr.s_addr;
        }
    }
    pm_close(s);
    return out;
}

/* Wait for `s` to be readable for up to `ms`. */
static bool wait_readable(PmSocket s, int ms) {
    fd_set set;
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    FD_ZERO(&set);
    FD_SET(s, &set);
    return select((int)s + 1, &set, NULL, NULL, &tv) > 0;
}

static bool stopping(void) {
    return SDL_GetAtomicInt(&s_stop) != 0;
}

/* ---- NAT-PMP -------------------------------------------------------------- */

/* One request/response exchange with retries (250 ms, doubling, 3 tries). */
static int natpmp_call(
    uint32_t gw, const uint8_t* req, int req_n, uint8_t* resp, int resp_n, uint8_t want_op) {
    PmSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in to = {0};
    int got = -1;
    if (s == PM_INVALID) {
        return -1;
    }
    to.sin_family = AF_INET;
    to.sin_port = htons(5351);
    to.sin_addr.s_addr = gw;
    for (int i = 0, wait = 250; i < 3 && got < 0 && !stopping(); i++, wait *= 2) {
        sendto(s, (const char*)req, req_n, 0, (struct sockaddr*)&to, sizeof to);
        while (wait_readable(s, wait)) {
            struct sockaddr_in from;
            socklen_t fn = sizeof from;
            const int n = (int)recvfrom(s, (char*)resp, resp_n, 0, (struct sockaddr*)&from, &fn);
            if (n >= 4 && from.sin_addr.s_addr == gw && resp[0] == 0 && resp[1] == want_op) {
                got = n;
                break;
            }
        }
    }
    pm_close(s);
    return got;
}

static bool natpmp_map(
    uint32_t gw, uint16_t internal, uint32_t lifetime, uint32_t* ext_addr, uint16_t* ext_port) {
    uint8_t req[12] = {0, 0}, resp[16];
    if (natpmp_call(gw, req, 2, resp, sizeof resp, 128) < 12 || (resp[2] | resp[3]) != 0) {
        return false;
    }
    *ext_addr =
        (uint32_t)resp[8] << 24 | (uint32_t)resp[9] << 16 | (uint32_t)resp[10] << 8 | resp[11];
    req[1] = 1; /* map UDP */
    req[4] = (uint8_t)(internal >> 8);
    req[5] = (uint8_t)internal;
    req[6] = (uint8_t)(internal >> 8); /* suggested external = internal */
    req[7] = (uint8_t)internal;
    req[8] = (uint8_t)(lifetime >> 24);
    req[9] = (uint8_t)(lifetime >> 16);
    req[10] = (uint8_t)(lifetime >> 8);
    req[11] = (uint8_t)lifetime;
    if (natpmp_call(gw, req, 12, resp, sizeof resp, 129) < 16 || (resp[2] | resp[3]) != 0) {
        return false;
    }
    *ext_port = (uint16_t)(resp[10] << 8 | resp[11]);
    return lifetime == 0 || *ext_port != 0;
}

/* ---- UPnP-IGD --------------------------------------------------------------- */

/* "http://host:port/path" -> parts. */
static bool split_url(const char* url, char* host, size_t hn, uint16_t* port, const char** path) {
    if (strncmp(url, "http://", 7) != 0) {
        return false;
    }
    const char* h = url + 7;
    const char* slash = strchr(h, '/');
    const char* colon = strchr(h, ':');
    size_t len = slash != NULL ? (size_t)(slash - h) : strlen(h);
    *port = 80;
    if (colon != NULL && (slash == NULL || colon < slash)) {
        *port = (uint16_t)atoi(colon + 1);
        len = (size_t)(colon - h);
    }
    if (len == 0 || len >= hn) {
        return false;
    }
    memcpy(host, h, len);
    host[len] = '\0';
    *path = slash != NULL ? slash : "/";
    return true;
}

/* One HTTP/1.0 request (no chunked replies to decode). Body into `out`;
 * returns the status code, or -1. */
static int http(const char* url, const char* method, const char* soap_action, const char* body,
    char* out, size_t out_n) {
    char host[64], req[4096];
    uint16_t port;
    const char* path;
    if (!split_url(url, host, sizeof host, &port, &path)) {
        return -1;
    }
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &to.sin_addr) != 1) {
        return -1; /* routers give an IP in LOCATION; no DNS lookups here */
    }
    int n;
    if (body != NULL) {
        n = snprintf(req, sizeof req,
            "%s %s HTTP/1.0\r\nHost: %s:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
            "SOAPAction: \"%s\"\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
            method, path, host, port, soap_action, (unsigned)strlen(body), body);
    } else {
        n = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
            method, path, host, port);
    }
    if (n <= 0 || n >= (int)sizeof req) {
        return -1;
    }
    PmSocket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == PM_INVALID) {
        return -1;
    }
#ifdef _WIN32
    DWORD tmo = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof tmo);
#else
    struct timeval tmo = {3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof tmo);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof tmo);
#endif
    if (connect(s, (struct sockaddr*)&to, sizeof to) != 0 || send(s, req, n, 0) != n) {
        pm_close(s);
        return -1;
    }
    size_t got = 0;
    int r;
    while (got + 1 < out_n && (r = (int)recv(s, out + got, (int)(out_n - 1 - got), 0)) > 0) {
        got += (size_t)r;
    }
    out[got] = '\0';
    pm_close(s);
    int status = -1;
    if (sscanf(out, "HTTP/%*s %d", &status) != 1) {
        return -1;
    }
    char* split = strstr(out, "\r\n\r\n");
    if (split != NULL) {
        memmove(out, split + 4, strlen(split + 4) + 1);
    }
    return status;
}

/* The text of the first <tag>...</tag> after `from` into `out`. */
static const char* xml_text(const char* from, const char* tag, char* out, size_t n) {
    char open[64], close[64];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    const char* a = strstr(from, open);
    const char* b = a != NULL ? strstr(a, close) : NULL;
    if (a == NULL || b == NULL) {
        return NULL;
    }
    a += strlen(open);
    size_t len = (size_t)(b - a);
    while (len > 0 && (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t')) {
        a++;
        len--;
    }
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, a, len);
    out[len] = '\0';
    return b + strlen(close);
}

/* SSDP: the description URL of an internet gateway on this network. */
static bool ssdp_find(uint32_t gw, char* location, size_t ln) {
    static const char* const targets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
    };
    PmSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in to = {0};
    bool found = false;
    if (s == PM_INVALID) {
        return false;
    }
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    to.sin_addr.s_addr = htonl(0xEFFFFFFAu); /* 239.255.255.250 */
    for (int t = 0; t < 3; t++) {
        char msg[256];
        const int n = snprintf(msg, sizeof msg,
            "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",
            targets[t]);
        sendto(s, msg, n, 0, (struct sockaddr*)&to, sizeof to);
    }
    const uint64_t until = SDL_GetTicks() + 2500;
    while (!stopping() && SDL_GetTicks() < until) {
        if (!wait_readable(s, 200)) {
            continue;
        }
        char buf[2048];
        struct sockaddr_in from;
        socklen_t fn = sizeof from;
        const int n = (int)recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr*)&from, &fn);
        if (n <= 0) {
            continue;
        }
        buf[n] = '\0';
        for (char* p = buf; *p != '\0'; p++) {
            if ((p == buf || p[-1] == '\n') &&
#ifdef _WIN32
                _strnicmp(p, "LOCATION:", 9) == 0
#else
                strncasecmp(p, "LOCATION:", 9) == 0
#endif
            )
            {
                char* v = p + 9;
                while (*v == ' ') {
                    v++;
                }
                const size_t len = strcspn(v, "\r\n");
                if (len < ln) {
                    memcpy(location, v, len);
                    location[len] = '\0';
                    found = true;
                    /* The gateway's own answer is the one to use. */
                    if (gw == 0 || from.sin_addr.s_addr == gw) {
                        pm_close(s);
                        return true;
                    }
                }
            }
        }
    }
    pm_close(s);
    return found;
}

/* The WAN connection service of the gateway described at `location`. */
static bool upnp_service(const char* location, char* control, size_t cn, char* service, size_t sn) {
    static char desc[65536];
    if (http(location, "GET", NULL, NULL, desc, sizeof desc) != 200) {
        return false;
    }
    char base[256] = "";
    xml_text(desc, "URLBase", base, sizeof base);
    for (const char* p = desc; (p = strstr(p, "<service>")) != NULL; p++) {
        char type[128], url[256];
        const char* end = strstr(p, "</service>");
        if (end == NULL || xml_text(p, "serviceType", type, sizeof type) == NULL ||
            xml_text(p, "controlURL", url, sizeof url) == NULL)
        {
            continue;
        }
        if (strstr(type, "WANIPConnection:") == NULL && strstr(type, "WANPPPConnection:") == NULL) {
            continue;
        }
        snprintf(service, sn, "%s", type);
        if (strncmp(url, "http://", 7) == 0) {
            snprintf(control, cn, "%s", url);
        } else {
            /* Relative to URLBase, else to the description's host. */
            char host[64];
            uint16_t port;
            const char* path;
            const char* root = base[0] != '\0' ? base : location;
            if (!split_url(root, host, sizeof host, &port, &path)) {
                return false;
            }
            snprintf(control, cn, "http://%s:%u%s%s", host, port, url[0] == '/' ? "" : "/", url);
        }
        return true;
    }
    return false;
}

static int soap(const char* action, const char* args, char* out, size_t n) {
    static char body[2048];
    char header[192];
    snprintf(body, sizeof body,
        "<?xml version=\"1.0\"?>\r\n<s:Envelope "
        "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
        action, s_service, args, action);
    snprintf(header, sizeof header, "%s#%s", s_service, action);
    return http(s_control, "POST", header, body, out, n);
}

static bool upnp_external(uint32_t* ext) {
    char out[4096], ip[32];
    struct in_addr a;
    if (soap("GetExternalIPAddress", "", out, sizeof out) != 200 ||
        xml_text(out, "NewExternalIPAddress", ip, sizeof ip) == NULL ||
        inet_pton(AF_INET, ip, &a) != 1)
    {
        return false;
    }
    *ext = ntohl(a.s_addr);
    return true;
}

static bool upnp_add(uint16_t ext, uint16_t internal, uint32_t client, int lease) {
    char args[768], out[4096], ip[INET_ADDRSTRLEN];
    struct in_addr a = {client};
    inet_ntop(AF_INET, &a, ip, sizeof ip);
    snprintf(args, sizeof args,
        "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort>"
        "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>Melee 4K</NewPortMappingDescription>"
        "<NewLeaseDuration>%d</NewLeaseDuration>",
        ext, internal, ip, lease);
    return soap("AddPortMapping", args, out, sizeof out) == 200;
}

static void upnp_delete(uint16_t ext) {
    char args[256], out[2048];
    snprintf(args, sizeof args,
        "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol>",
        ext);
    soap("DeletePortMapping", args, out, sizeof out);
}

/* ---- the thread ---------------------------------------------------------------- */

static bool try_map(uint32_t* ext, uint16_t* ext_port, bool* know_address) {
    const uint32_t gw = s_gateway;
    *know_address = false;
    /* MELEE_PORTMAP_UPNP=1: skip NAT-PMP, to test the UPnP path. */
    if (gw != 0 && getenv("MELEE_PORTMAP_UPNP") == NULL &&
        natpmp_map(gw, s_internal, LEASE_S, ext, ext_port))
    {
        s_mapped_by = 1;
        *know_address = true;
        return true;
    }
    if (s_control[0] == '\0') {
        char location[256];
        if (!ssdp_find(gw, location, sizeof location) ||
            !upnp_service(location, s_control, sizeof s_control, s_service, sizeof s_service))
        {
            s_control[0] = '\0';
            return false;
        }
        pc_log_line("portmap: UPnP gateway %s (%s)", s_control, s_service);
    }
    *know_address = upnp_external(ext);
    const uint32_t client = local_address_toward(gw != 0 ? gw : htonl(0xEFFFFFFAu));
    /* The same external port first, then a few above it if it is taken; a
     * lease of 0 (permanent until removed) for routers that only take that. */
    for (int i = 0; i < 5 && !stopping(); i++) {
        const uint16_t want = (uint16_t)(s_internal + i);
        if (upnp_add(want, s_internal, client, LEASE_S) || upnp_add(want, s_internal, client, 0)) {
            s_mapped_by = 2;
            *ext_port = want;
            return true;
        }
    }
    return false;
}

static int portmap_thread(void* unused) {
    (void)unused;
    s_gateway = default_gateway();
    {
        struct in_addr g = {s_gateway};
        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &g, ip, sizeof ip);
        pc_log_line("portmap: gateway %s, port %u", s_gateway ? ip : "unknown", s_internal);
    }
    while (!stopping()) {
        uint32_t ext = 0;
        uint16_t ext_port = 0;
        bool know = false;
        const bool mapped = try_map(&ext, &ext_port, &know);
        const char* method = s_mapped_by == 1 ? "NAT-PMP" : s_control[0] ? "UPnP" : "";
        enum PcPortmapState st;
        if (!know) {
            st = PC_PORTMAP_NONE;
        } else if (private_address(ext)) {
            st = PC_PORTMAP_SHARED;
        } else {
            st = mapped ? PC_PORTMAP_OPEN : PC_PORTMAP_CLOSED;
        }
        set_result(st, know ? ext : 0, mapped ? ext_port : s_internal, method);
        pc_log_line("portmap: %s via %s, external %u.%u.%u.%u:%u",
            st == PC_PORTMAP_OPEN   ? "open" :
            st == PC_PORTMAP_CLOSED ? "router refused the mapping" :
            st == PC_PORTMAP_SHARED ? "router has a private address (shared NAT)" :
                                      "no router answered",
            method[0] ? method : "-", ext >> 24, (ext >> 16) & 255, (ext >> 8) & 255, ext & 255,
            mapped ? ext_port : s_internal);
        /* Renew a mapping; retry a failure less often. */
        const uint64_t next = SDL_GetTicks() + (mapped ? RENEW_MS : 60000);
        while (!stopping() && SDL_GetTicks() < next) {
            SDL_Delay(100);
        }
    }
    if (s_mapped_by == 1) {
        uint32_t ext;
        uint16_t port;
        natpmp_map(s_gateway, s_internal, 0, &ext, &port);
    } else if (s_mapped_by == 2) {
        upnp_delete(pc_portmap_external_port());
    }
    s_mapped_by = 0;
    return 0;
}

void pc_portmap_start(uint16_t internal_port) {
    pc_portmap_stop();
    if (s_lock == NULL) {
        s_lock = SDL_CreateMutex();
    }
    s_internal = internal_port;
    s_control[0] = '\0';
    s_mapped_by = 0;
    set_result(PC_PORTMAP_WORKING, 0, 0, "");
    SDL_SetAtomicInt(&s_stop, 0);
    s_thread = SDL_CreateThread(portmap_thread, "portmap", NULL);
    if (s_thread == NULL) {
        set_result(PC_PORTMAP_NONE, 0, 0, "");
    }
}

void pc_portmap_stop(void) {
    if (s_thread != NULL) {
        SDL_SetAtomicInt(&s_stop, 1);
        SDL_WaitThread(s_thread, NULL); /* removes the mapping on the way out */
        s_thread = NULL;
    }
    if (s_lock != NULL) {
        set_result(PC_PORTMAP_OFF, 0, 0, "");
    }
}

enum PcPortmapState pc_portmap_state(void) {
    return (enum PcPortmapState)SDL_GetAtomicInt(&s_state);
}

uint32_t pc_portmap_external_address(void) {
    uint32_t v = 0;
    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
        v = s_external;
        SDL_UnlockMutex(s_lock);
    }
    return v;
}

uint16_t pc_portmap_external_port(void) {
    uint16_t v = 0;
    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
        v = s_ext_port;
        SDL_UnlockMutex(s_lock);
    }
    return v;
}

const char* pc_portmap_method(void) {
    const char* v = "";
    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
        v = s_method;
        SDL_UnlockMutex(s_lock);
    }
    return v;
}
