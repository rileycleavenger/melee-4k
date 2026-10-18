/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Online play: transport, lobby, setup and lockstep (online.h).
 *
 * One UDP socket. The host binds 41000 (the port its router is asked to
 * open, net_portmap.c), a guest any free port. Every datagram starts with a
 * Hdr; one with another magic, protocol or build is refused before its body
 * is read, and after joining one from anyone but the host (or, for the host,
 * an unknown guest) is dropped.
 *
 *   JOIN      guest -> host   the code's token, our name, our build
 *   WELCOME   host -> guest   you are in (session id, the token back)
 *   REFUSE    host -> guest   full / other version / already playing
 *
 * Only the latest release plays online (pc_latest_release_state: GitHub's
 * newest release, checked from boot): hosting and joining wait for that
 * check and refuse an older version, and a host lets in only its own
 * build, so everyone in a lobby runs the same latest version. Players with
 * different custom fighter packs play with the base fighters only: the
 * host marks every packet then (Hdr.flags).
 *   LOBBY     host -> guests  the players and their pings, every 250 ms
 *   PING/PONG                 round trips (the host pings everyone)
 *   SETUP     host -> guest   the setup: seed, delay, ports, and the host's
 *                             persistent game data in chunks, resent until
 *   SETUPACK  guest -> host   ...every chunk has arrived and the data hashes
 *                             right
 *   GO/GOACK                  everyone has it: enter character select
 *   INPUT     guest -> host   our input for the newest frames (redundant), the
 *                             newest bundle we hold, a state hash
 *   BUNDLE    host -> guest   every port's input for the newest frames, and
 *                             the newest input of theirs the host holds
 *   END       host -> guests  the game ended (a player left, out of sync):
 *                             back to the lobby
 *   BYE                       leaving
 *
 * Inputs and bundles are sent unreliably: each packet repeats every frame
 * the other side has not acknowledged (up to 32), so a lost packet costs
 * nothing but the next one. Everything is little-endian on every platform
 * Melee 4K runs on, so structs go on the wire as they are. */
#include "online.h"

#include "fighter_pack.h"
#include "net_code.h"
#include "net_portmap.h"
#include "product.h"
#include "pc.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET OnSocket;
#define ON_INVALID INVALID_SOCKET
#define on_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int OnSocket;
#define ON_INVALID (-1)
#define on_close close
#endif

extern const char* pc_app_rev(void);
extern int pc_get_net_delay(void); /* launcher.cpp: Input Delay, -1 = Auto */

#define MAGIC 0x314B344Du /* "M4K1" */
#define JOIN_EXT 0x584B344Du /* "M4KX" */
#define PROTO 2
#define HOST_PORT 41000
#define RING 256       /* frames of inputs and bundles kept */
#define SEND_SPAN 20   /* frames repeated per input/bundle packet (a bundle stays under 1200 bytes) */
#define CHUNK 1024     /* setup data per packet */
#define MAX_CHUNKS 128 /* 128 KB of setup data at most */
#define LOBBY_SILENCE_MS 10000
#define GAME_SILENCE_MS 15000
#define JOIN_GIVE_UP_MS 20000

enum {
    T_JOIN = 1,
    T_WELCOME,
    T_REFUSE,
    T_LOBBY,
    T_PING,
    T_PONG,
    T_SETUP,
    T_CHUNK,
    T_SETUPACK,
    T_GO,
    T_GOACK,
    T_INPUT,
    T_BUNDLE,
    T_END,
    T_BYE,
};

#pragma pack(push, 1)
#define HDR_BASE_ONLY 1 /* Hdr.flags, from the host: the lobby plays base fighters only */
typedef struct Hdr {
    uint32_t magic;
    uint8_t proto, type, from, flags;
    uint32_t session;
    uint32_t build;
} Hdr;
typedef struct JoinMsg {
    Hdr h;
    uint32_t token;
    char name[ONLINE_NAME_LEN];
} JoinMsg;
/* After the JoinMsg: what a host needs to decide whether the two play with
 * custom fighters (the same packs) or with the base fighters only. */
typedef struct JoinExt {
    uint32_t magic; /* JOIN_EXT */
    uint32_t base_rev; /* unused (0) */
    uint32_t content; /* pc_fighter_packs_hash() */
    char version[16];
} JoinExt;
typedef struct WelcomeMsg {
    Hdr h;
    uint32_t token;
    uint8_t slot;
} WelcomeMsg;
typedef struct RefuseMsg {
    Hdr h;
    uint32_t token;
    uint8_t reason; /* an OnlineError */
} RefuseMsg;
typedef struct LobbyMsg {
    Hdr h;
    uint8_t count;
    struct {
        uint8_t slot;
        int16_t ping_ms;
        char name[ONLINE_NAME_LEN];
    } p[ONLINE_MAX_PLAYERS];
} LobbyMsg;
typedef struct PingMsg {
    Hdr h;
    uint64_t t;
} PingMsg;
typedef struct SetupMsg {
    Hdr h;
    uint32_t epoch, seed, size, hash;
    uint8_t players, delay, port, unlock_all, frozen_stadium, chunks;
    uint8_t slot_port[ONLINE_MAX_PLAYERS];
} SetupMsg;
typedef struct ChunkMsg {
    Hdr h;
    uint32_t epoch;
    uint16_t index, len;
    uint8_t data[CHUNK];
} ChunkMsg;
typedef struct SetupAckMsg {
    Hdr h;
    uint32_t epoch;
    uint8_t have[MAX_CHUNKS / 8];
    uint8_t done;
} SetupAckMsg;
typedef struct GoMsg {
    Hdr h;
    uint32_t epoch;
} GoMsg;
typedef struct InputMsg {
    Hdr h;
    uint32_t epoch;
    int32_t first;       /* frame of pads[0] */
    int32_t bundle_have; /* newest contiguous bundle we hold */
    int32_t ck_frame;    /* -1: no hash */
    uint32_t ck;
    uint8_t count;
    OnlinePad pads[SEND_SPAN];
} InputMsg;
typedef struct BundleMsg {
    Hdr h;
    uint32_t epoch;
    int32_t first;
    int32_t input_have; /* newest contiguous input of the receiver's we hold */
    uint8_t count, players;
    OnlinePad pads[SEND_SPAN][ONLINE_MAX_PLAYERS];
} BundleMsg;
typedef struct EndMsg {
    Hdr h;
    uint32_t epoch;
    uint8_t reason; /* an OnlineError */
} EndMsg;
#pragma pack(pop)

/* A connection: the host keeps one per guest (slots 1-3); a guest keeps the
 * host in slot 0. */
typedef struct Peer {
    bool used;
    struct sockaddr_in addr;
    char name[ONLINE_NAME_LEN];
    uint64_t heard_ms;
    int ping_ms;
    int port; /* game port in this setup */
    /* host: the setup going to this guest */
    uint8_t chunk_have[MAX_CHUNKS / 8];
    bool setup_done, go_acked;
    /* host: lockstep with this guest */
    int32_t input_have;  /* newest contiguous frame of its input we hold */
    int32_t bundle_have; /* newest contiguous bundle it holds */
    uint64_t sent_ms;
    int32_t ck_frame[64];
    uint32_t ck[64];
} Peer;

static enum OnlineState s_state;
static enum OnlineError s_error;
static bool s_is_host;
static OnSocket s_sock = ON_INVALID;
static uint32_t s_session, s_token, s_build;
static char s_latest[32]; /* the latest release's tag, for the message */
static bool s_base_only;
static char s_name[ONLINE_NAME_LEN];
static int s_slot; /* ours: 0 host, 1-3 guest */
static Peer s_peer[ONLINE_MAX_PLAYERS];
static uint64_t s_started_ms, s_lobby_ms, s_ping_ms;
/* joining */
static PcNetLobbyCode s_target;
static uint64_t s_join_sent_ms;
/* the lobby as the host last described it (guests) */
static LobbyMsg s_lobby;
/* setup */
static uint32_t s_epoch;
static OnlineSetup s_setup;
static uint8_t s_slot_port[ONLINE_MAX_PLAYERS];
static const uint8_t* s_blob_out; /* host: its data */
static uint8_t* s_blob_in;        /* guest: the host's, arriving */
static size_t s_blob_size;
static uint32_t s_blob_hash;
static uint8_t s_chunk_have[MAX_CHUNKS / 8];
static int s_chunks;
static bool s_setup_done;
static uint64_t s_setup_sent_ms, s_ack_sent_ms;
/* lockstep */
static bool s_prepared; /* rings reset for s_epoch */
static OnlinePad s_input[ONLINE_MAX_PLAYERS][RING];
static int32_t s_input_have[ONLINE_MAX_PLAYERS]; /* host: per port */
static OnlinePad s_local[RING];
static int32_t s_local_newest;
static int32_t s_host_has_mine; /* guest: newest of our inputs the host holds */
static OnlinePad s_bundle[RING][ONLINE_MAX_PLAYERS];
static int32_t s_bundle_frame[RING];
static int32_t s_bundle_have;
static int32_t s_ck_frame[64];
static uint32_t s_ck[64];
static int32_t s_ck_pending_frame = -1;
static uint32_t s_ck_pending;
static uint64_t s_bundle_progress_ms;
static enum OnlineError s_end; /* the game ended early: why */

/* ---- small things ---------------------------------------------------------- */

static uint64_t now_ms(void) {
    return SDL_GetTicks();
}

static uint32_t fnv(uint32_t h, const void* p, size_t n) {
    const uint8_t* b = p;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static uint32_t random32(void) {
    static uint32_t x;
    if (x == 0) {
        uint64_t t = SDL_GetTicksNS();
        x = (uint32_t)(t ^ (t >> 32)) ^ (uint32_t)(uintptr_t)&x ^ 0x9E3779B9u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static const char* ip_text(const struct sockaddr_in* a) {
    static char buf[2][32];
    static int i;
    i ^= 1;
    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip);
    snprintf(buf[i], sizeof buf[i], "%s:%u", ip, ntohs(a->sin_port));
    return buf[i];
}

static bool same_addr(const struct sockaddr_in* a, const struct sockaddr_in* b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

/* ---- the socket, with an optional bad-network simulator ---------------------- */

/* MELEE_ONLINE_LOSS=percent, MELEE_ONLINE_DELAY_MS=ms, MELEE_ONLINE_JITTER_MS=ms
 * act on what this machine sends, for tests. */
static int s_sim_loss = -1, s_sim_delay, s_sim_jitter;
typedef struct Held {
    uint64_t at;
    struct sockaddr_in to;
    uint16_t len;
    uint8_t data[1600];
} Held;
static Held s_held[512];
static int s_held_n;

static void raw_send(const void* p, size_t n, const struct sockaddr_in* to) {
    sendto(s_sock, (const char*)p, (int)n, 0, (const struct sockaddr*)to, sizeof *to);
}

static void send_to(const void* p, size_t n, const struct sockaddr_in* to) {
    if (s_sock == ON_INVALID) {
        return;
    }
    if (s_sim_loss < 0) {
        const char* e;
        s_sim_loss = (e = getenv("MELEE_ONLINE_LOSS")) ? atoi(e) : 0;
        s_sim_delay = (e = getenv("MELEE_ONLINE_DELAY_MS")) ? atoi(e) : 0;
        s_sim_jitter = (e = getenv("MELEE_ONLINE_JITTER_MS")) ? atoi(e) : 0;
        if (s_sim_loss || s_sim_delay || s_sim_jitter) {
            pc_log_line("online: simulating %d%% loss, %d ms delay, %d ms jitter", s_sim_loss,
                s_sim_delay, s_sim_jitter);
        }
    }
    if (s_sim_loss > 0 && (int)(random32() % 100) < s_sim_loss) {
        return;
    }
    if ((s_sim_delay > 0 || s_sim_jitter > 0) && n <= sizeof s_held[0].data &&
        s_held_n < (int)(sizeof s_held / sizeof s_held[0]))
    {
        Held* h = &s_held[s_held_n++];
        const int jitter =
            s_sim_jitter > 0 ? (int)(random32() % (2 * s_sim_jitter + 1)) - s_sim_jitter : 0;
        const int wait = s_sim_delay + jitter;
        h->at = now_ms() + (uint64_t)(wait > 0 ? wait : 0);
        h->to = *to;
        h->len = (uint16_t)n;
        memcpy(h->data, p, n);
        return;
    }
    raw_send(p, n, to);
}

static void flush_held(void) {
    const uint64_t now = now_ms();
    int keep = 0;
    for (int i = 0; i < s_held_n; i++) {
        if (s_held[i].at <= now) {
            raw_send(s_held[i].data, s_held[i].len, &s_held[i].to);
        } else {
            s_held[keep++] = s_held[i];
        }
    }
    s_held_n = keep;
}

static bool open_socket(uint16_t port, bool broadcast) {
#ifdef _WIN32
    static bool wsa;
    if (!wsa) {
        WSADATA w;
        wsa = WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }
#endif
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock == ON_INVALID) {
        return false;
    }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (bind(s_sock, (struct sockaddr*)&a, sizeof a) != 0) {
        a.sin_port = 0; /* taken (another copy of the game): any free port */
        if (bind(s_sock, (struct sockaddr*)&a, sizeof a) != 0) {
            on_close(s_sock);
            s_sock = ON_INVALID;
            return false;
        }
    }
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s_sock, FIONBIO, &on);
    /* Windows reports an ICMP "port unreachable" (a player who just left) as
     * an error on the next receive; that is not this socket's problem. */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    BOOL report = FALSE;
    DWORD got = 0;
    WSAIoctl(s_sock, SIO_UDP_CONNRESET, &report, sizeof report, NULL, 0, &got, NULL, NULL);
#else
    fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    int yes = 1;
    if (broadcast) {
        setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
    }
    int big = 1 << 20;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&big, sizeof big);
    return true;
}

static uint16_t socket_port(void) {
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    if (s_sock == ON_INVALID || getsockname(s_sock, (struct sockaddr*)&a, &n) != 0) {
        return 0;
    }
    return ntohs(a.sin_port);
}

static void hdr(Hdr* h, uint8_t type) {
    h->magic = MAGIC;
    h->proto = PROTO;
    h->type = type;
    h->from = (uint8_t)s_slot;
    h->flags = s_is_host && s_base_only ? HDR_BASE_ONLY : 0;
    h->session = s_session;
    h->build = s_build;
}

static void send_peer(int slot, void* msg, size_t n) {
    if (slot >= 0 && slot < ONLINE_MAX_PLAYERS && s_peer[slot].used) {
        send_to(msg, n, &s_peer[slot].addr);
    }
}

/* ---- state changes ------------------------------------------------------------- */

static void fail(enum OnlineError e) {
    if (s_state == ONLINE_FAILED) {
        return;
    }
    pc_log_line("online: failed: %s", (s_error = e, online_error_text()));
    s_state = ONLINE_FAILED;
}

/* The game in progress ends for everyone; the lobby stays. */
static void end_game(enum OnlineError why) {
    if (s_end != ONLINE_ERR_NONE) {
        return;
    }
    s_end = why;
    s_error = why;
    pc_log_line("online: game ended: %s", online_error_text());
    if (s_is_host) {
        EndMsg m = {0};
        hdr(&m.h, T_END);
        m.epoch = s_epoch;
        m.reason = (uint8_t)why;
        for (int k = 0; k < 3; k++) {
            for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
                send_peer(i, &m, sizeof m);
            }
        }
    }
}

static int guest_count(void) {
    int n = 0;
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        n += s_peer[i].used;
    }
    return n;
}

static void drop_guest(int slot, enum OnlineError why) {
    if (!s_peer[slot].used) {
        return;
    }
    pc_log_line("online: %s (slot %d) %s", s_peer[slot].name, slot,
        why == ONLINE_ERR_TIMEOUT ? "stopped answering" : "left");
    const bool in_game =
        s_state == ONLINE_STARTING || s_state == ONLINE_READY || s_state == ONLINE_PLAYING;
    const bool playing_slot = s_peer[slot].port >= 0;
    memset(&s_peer[slot], 0, sizeof s_peer[slot]);
    if (in_game && playing_slot) {
        end_game(ONLINE_ERR_PLAYER_LEFT);
        if (s_state != ONLINE_PLAYING) {
            s_state = ONLINE_HOSTING; /* the setup was for them too */
        }
    }
}

/* ---- lobby ------------------------------------------------------------------------- */

static void reset_all(void) {
    if (s_sock != ON_INVALID) {
        on_close(s_sock);
        s_sock = ON_INVALID;
    }
    pc_portmap_stop();
    free(s_blob_in);
    s_blob_in = NULL;
    s_blob_out = NULL;
    memset(s_peer, 0, sizeof s_peer);
    memset(&s_lobby, 0, sizeof s_lobby);
    s_state = ONLINE_OFF;
    s_error = ONLINE_ERR_NONE;
    s_end = ONLINE_ERR_NONE;
    s_is_host = false;
    s_session = 0;
    s_slot = 0;
    s_prepared = false;
    s_held_n = 0;
}

/* The product (product.env) and its version: Melee 4K and a fork never
 * share a lobby, even at the same version number. */
static uint32_t build_of(const char* rev) {
    const uint32_t product = fnv(2166136261u, PC_PRODUCT_SLUG, strlen(PC_PRODUCT_SLUG));
    return fnv(fnv(product, rev, strlen(rev)), "proto1", 6);
}

static void set_identity(const char* name) {
    snprintf(s_name, sizeof s_name, "%s", name != NULL && name[0] ? name : "PLAYER");
    s_build = build_of(pc_app_rev());
    s_base_only = false;
}

/* From here on the session is the base game only (online.h). */
static void go_base_only(const char* why) {
    if (!s_base_only) {
        s_base_only = true;
        pc_log_line("online: base fighters only (%s)", why);
    }
}

bool online_base_only(void) {
    return s_base_only;
}

/* The version gate: only the latest release goes online. Waits up to 8 s
 * for the check boot started (hosting and joining come from the lobby
 * screen, a menu). */
static bool latest_or_fail(void) {
    char tag[32];
    int st;
    pc_latest_release_start();
    for (int i = 0; i < 160 && (st = pc_latest_release_state(tag, sizeof tag)) == PC_LATEST_CHECKING; i++) {
        SDL_Delay(50);
    }
    snprintf(s_latest, sizeof s_latest, "%s", tag);
    if (st == PC_LATEST_OK) {
        return true;
    }
    fail(st == PC_LATEST_OUTDATED ? ONLINE_ERR_OUTDATED : ONLINE_ERR_UNCHECKED);
    pc_log_line("online: refused: %s (this is %s, the latest release %s)",
        st == PC_LATEST_OUTDATED ? "not the latest version" : "could not check the latest version",
        pc_app_rev(), tag[0] ? tag : "unknown");
    return false;
}

bool online_host(const char* name) {
    online_leave();
    set_identity(name);
    if (!latest_or_fail()) {
        return false;
    }
    if (!open_socket(HOST_PORT, false)) {
        fail(ONLINE_ERR_SOCKET);
        return false;
    }
    s_is_host = true;
    s_slot = 0;
    s_session = random32() | 1;
    s_token = random32() & 0x1FFFF;
    s_started_ms = now_ms();
    s_peer[0].used = true; /* ourselves, for the player list */
    snprintf(s_peer[0].name, sizeof s_peer[0].name, "%s", s_name);
    s_peer[0].port = -1;
    s_state = ONLINE_HOSTING;
    pc_portmap_start(socket_port());
    pc_log_line(
        "online: hosting on udp/%u (build %08x, session %08x)", socket_port(), s_build, s_session);
    return true;
}

bool online_join(const char* code, const char* name) {
    online_leave();
    set_identity(name);
    if (!latest_or_fail()) {
        return false;
    }
    if (!pc_net_code_decode(code, &s_target)) {
        return false;
    }
    if (!open_socket(0, true)) {
        fail(ONLINE_ERR_SOCKET);
        return false;
    }
    s_is_host = false;
    s_slot = 0xFF;
    s_started_ms = now_ms();
    s_join_sent_ms = 0;
    s_state = ONLINE_JOINING;
    pc_log_line("online: joining %u.%u.%u.%u:%u from udp/%u (build %08x)", s_target.address >> 24,
        (s_target.address >> 16) & 255, (s_target.address >> 8) & 255, s_target.address & 255,
        s_target.port, socket_port(), s_build);
    return true;
}

void online_leave(void) {
    if (s_sock != ON_INVALID && s_state != ONLINE_OFF) {
        Hdr bye;
        hdr(&bye, T_BYE);
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < ONLINE_MAX_PLAYERS; i++) {
                if (i != s_slot || !s_is_host) {
                    send_peer(i, &bye, sizeof bye);
                }
            }
        }
        for (int i = 0; i < s_held_n; i++) {
            s_held[i].at = 0; /* the link simulator's delay: leaving, send now */
        }
        flush_held();
        pc_log_line("online: left");
    }
    reset_all();
}

enum OnlineState online_state(void) {
    return s_state;
}

enum OnlineError online_error(void) {
    return s_error;
}

const char* online_error_text(void) {
    switch (s_error) {
    case ONLINE_ERR_SOCKET:
        return "Can't open a network port";
    case ONLINE_ERR_NO_ANSWER:
        return "The host isn't answering. Check the code";
    case ONLINE_ERR_FULL:
        return "That lobby is full";
    case ONLINE_ERR_VERSION:
        return "The host has another " PC_PRODUCT_NAME " version: both need the latest";
    case ONLINE_ERR_OUTDATED: {
        static char text[96];
        snprintf(text, sizeof text, "Online needs the latest " PC_PRODUCT_NAME " (%s): Check for Updates in the menu",
            s_latest[0] ? s_latest : "newer");
        return text;
    }
    case ONLINE_ERR_UNCHECKED:
        return "Couldn't reach GitHub to check for the latest version";
    case ONLINE_ERR_STARTED:
        return "That lobby's game has already started";
    case ONLINE_ERR_HOST_LEFT:
        return "The host closed the lobby";
    case ONLINE_ERR_PLAYER_LEFT:
        return "A player left the game";
    case ONLINE_ERR_TIMEOUT:
        return "Lost the connection";
    case ONLINE_ERR_DESYNC:
        return "Out of sync - back to the lobby";
    default:
        return "";
    }
}

bool online_copy_code(void) {
    char code[18];
    return online_code(code, NULL) && SDL_SetClipboardText(code);
}

bool online_clipboard_code(char out[PC_NET_CODE_LEN + 1]) {
    if (!SDL_HasClipboardText()) {
        return false;
    }
    char* text = SDL_GetClipboardText();
    if (text == NULL) {
        return false;
    }
    /* A code pasted from a chat often has words around it: try each
     * whitespace-separated piece. */
    bool found = false;
    for (char* piece = strtok(text, " \t\r\n"); piece != NULL && !found;
         piece = strtok(NULL, " \t\r\n"))
    {
        found = pc_net_code_normalize(piece, out) && pc_net_code_decode(out, &(PcNetLobbyCode){0});
    }
    SDL_free(text);
    return found;
}

bool online_is_host(void) {
    return s_is_host;
}

bool online_code(char out[18], int* reach) {
    if (!s_is_host || s_state == ONLINE_OFF || s_state == ONLINE_FAILED) {
        return false;
    }
    const enum PcPortmapState st = pc_portmap_state();
    if (reach) {
        *reach = st;
    }
    if (st == PC_PORTMAP_WORKING && now_ms() - s_started_ms < 6000) {
        return false; /* still asking the router */
    }
    PcNetLobbyCode c = {.address = 0, .port = socket_port(), .token = s_token};
    if (st == PC_PORTMAP_OPEN || st == PC_PORTMAP_CLOSED) {
        c.address = pc_portmap_external_address();
        if (st == PC_PORTMAP_OPEN) {
            c.port = pc_portmap_external_port();
        }
    }
    char code[PC_NET_CODE_LEN + 1];
    pc_net_code_encode(&c, code);
    pc_net_code_show(code, out, 18);
    return true;
}

int online_players(OnlinePlayer out[ONLINE_MAX_PLAYERS]) {
    int n = 0;
    if (s_is_host) {
        for (int i = 0; i < ONLINE_MAX_PLAYERS; i++) {
            if (s_peer[i].used) {
                snprintf(out[n].name, sizeof out[n].name, "%s", s_peer[i].name);
                out[n].port = s_peer[i].port;
                out[n].ping_ms = i == 0 ? 0 : s_peer[i].ping_ms;
                out[n].local = i == 0;
                n++;
            }
        }
    } else if (s_state != ONLINE_JOINING) {
        for (int i = 0; i < s_lobby.count && i < ONLINE_MAX_PLAYERS; i++) {
            snprintf(
                out[n].name, sizeof out[n].name, "%.*s", ONLINE_NAME_LEN - 1, s_lobby.p[i].name);
            out[n].port = s_slot_port[s_lobby.p[i].slot % ONLINE_MAX_PLAYERS];
            out[n].ping_ms = s_lobby.p[i].ping_ms;
            out[n].local = s_lobby.p[i].slot == s_slot;
            n++;
        }
    }
    return n;
}

/* ---- setup ------------------------------------------------------------------------- */

static int delay_for_ping(int worst_ms) {
    /* A guest's input reaches every machine through the host: its round
     * trip, plus a frame of slack for pacing differences. */
    const int frames = worst_ms <= 0 ? 2 : (worst_ms * 60 + 999) / 1000 + 1;
    return frames < 2 ? 2 : frames > 8 ? 8 : frames;
}

/* Host: every guest has every bundle of the last game, so none of them is
 * still playing it. */
static bool players_caught_up(void) {
    if (!s_prepared) {
        return true;
    }
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        if (s_peer[i].used && s_peer[i].port >= 0 && s_peer[i].bundle_have < s_bundle_have) {
            return false;
        }
    }
    return true;
}

bool online_can_start(void) {
    if (!s_is_host || s_state != ONLINE_HOSTING || guest_count() == 0 || !players_caught_up()) {
        return false;
    }
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        if (s_peer[i].used && s_peer[i].ping_ms < 0) {
            return false; /* the input delay needs everyone's round trip */
        }
    }
    return true;
}

bool online_start(const void* blob, size_t size, bool unlock_all, bool frozen_stadium) {
    if (!online_can_start() || size > (size_t)MAX_CHUNKS * CHUNK) {
        return false;
    }
    s_epoch++;
    s_end = ONLINE_ERR_NONE;
    s_error = ONLINE_ERR_NONE;
    s_blob_out = blob;
    s_blob_size = size;
    s_blob_hash = fnv(2166136261u, blob, size);
    s_chunks = (int)((size + CHUNK - 1) / CHUNK);
    int worst = 0, port = 0;
    memset(s_slot_port, 0xFF, sizeof s_slot_port);
    for (int i = 0; i < ONLINE_MAX_PLAYERS; i++) {
        if (s_peer[i].used) {
            s_slot_port[i] = (uint8_t)port;
            s_peer[i].port = port++;
            if (s_peer[i].ping_ms > worst) {
                worst = s_peer[i].ping_ms;
            }
            memset(s_peer[i].chunk_have, 0, sizeof s_peer[i].chunk_have);
            s_peer[i].setup_done = s_peer[i].go_acked = (i == 0);
        } else {
            s_peer[i].port = -1;
        }
    }
    s_setup.seed = random32();
    s_setup.players = (uint8_t)port;
    const int chosen = pc_get_net_delay();
    s_setup.delay = (uint8_t)(chosen > 0 ? (chosen > 8 ? 8 : chosen) : delay_for_ping(worst));
    s_setup.local_port = 0;
    s_setup.unlock_all = unlock_all;
    s_setup.frozen_stadium = frozen_stadium;
    s_setup_sent_ms = 0;
    s_prepared = false;
    s_state = ONLINE_STARTING;
    pc_log_line("online: starting: %d players, delay %d (worst ping %d ms), seed %08x, data %zu "
                "bytes hash %08x",
        port, s_setup.delay, worst, s_setup.seed, size, s_blob_hash);
    return true;
}

bool online_setup(OnlineSetup* out, void* blob, size_t size) {
    if (s_state != ONLINE_READY && s_state != ONLINE_PLAYING) {
        return false;
    }
    *out = s_setup;
    if (!s_is_host && blob != NULL) {
        if (size != s_blob_size || s_blob_in == NULL) {
            return false;
        }
        memcpy(blob, s_blob_in, size);
    }
    return true;
}

static void host_send_setup(void) {
    const uint64_t now = now_ms();
    if (now - s_setup_sent_ms < 150) {
        return;
    }
    s_setup_sent_ms = now;
    bool all_done = true, all_go = true;
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        Peer* p = &s_peer[i];
        if (!p->used) {
            continue;
        }
        if (!p->setup_done) {
            all_done = false;
            SetupMsg m = {0};
            hdr(&m.h, T_SETUP);
            m.epoch = s_epoch;
            m.seed = s_setup.seed;
            m.size = (uint32_t)s_blob_size;
            m.hash = s_blob_hash;
            m.players = s_setup.players;
            m.delay = s_setup.delay;
            m.port = (uint8_t)p->port;
            m.unlock_all = s_setup.unlock_all;
            m.frozen_stadium = s_setup.frozen_stadium;
            m.chunks = (uint8_t)s_chunks;
            memcpy(m.slot_port, s_slot_port, sizeof m.slot_port);
            send_peer(i, &m, sizeof m);
            int sent = 0;
            for (int c = 0; c < s_chunks && sent < 24; c++) {
                if (p->chunk_have[c / 8] & (1 << (c % 8))) {
                    continue;
                }
                ChunkMsg k;
                hdr(&k.h, T_CHUNK);
                k.epoch = s_epoch;
                k.index = (uint16_t)c;
                const size_t off = (size_t)c * CHUNK;
                k.len = (uint16_t)(s_blob_size - off < CHUNK ? s_blob_size - off : CHUNK);
                memcpy(k.data, s_blob_out + off, k.len);
                send_peer(i, &k, sizeof k - CHUNK + k.len);
                sent++;
            }
        } else if (!p->go_acked) {
            all_go = false;
        }
    }
    if (all_done) {
        GoMsg g;
        hdr(&g.h, T_GO);
        g.epoch = s_epoch;
        for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
            if (s_peer[i].used && !s_peer[i].go_acked) {
                send_peer(i, &g, sizeof g);
            }
        }
        if (all_go) {
            s_state = ONLINE_READY;
            pc_log_line("online: everyone has the setup: go");
        }
    }
}

/* Lockstep rings for the session starting: before anyone can send a frame. */
static void prepare_lockstep(void) {
    if (s_prepared) {
        return;
    }
    s_prepared = true;
    const int32_t first = s_setup.delay - 1; /* frames before the delay are neutral */
    memset(s_input, 0, sizeof s_input);
    memset(s_local, 0, sizeof s_local);
    memset(s_bundle, 0, sizeof s_bundle);
    for (int i = 0; i < RING; i++) {
        s_bundle_frame[i] = -1;
    }
    for (int p = 0; p < ONLINE_MAX_PLAYERS; p++) {
        s_input_have[p] = first;
    }
    for (int i = 0; i < ONLINE_MAX_PLAYERS; i++) {
        s_peer[i].input_have = first;
        s_peer[i].bundle_have = -1;
        for (int k = 0; k < 64; k++) {
            s_peer[i].ck_frame[k] = -1;
        }
    }
    for (int k = 0; k < 64; k++) {
        s_ck_frame[k] = -1;
    }
    s_local_newest = first;
    s_host_has_mine = first;
    s_bundle_have = -1;
    s_ck_pending_frame = -1;
    s_end = ONLINE_ERR_NONE;
    s_bundle_progress_ms = now_ms();
}

/* ---- receiving ------------------------------------------------------------------------- */

static void host_lobby_tick(void);

static void on_join(const JoinMsg* m, int n, const struct sockaddr_in* from) {
    if (m->token != s_token) {
        return; /* another lobby's code, met through the broadcast */
    }
    int slot = -1;
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        if (s_peer[i].used && same_addr(&s_peer[i].addr, from)) {
            slot = i; /* a repeat */
        }
    }
    RefuseMsg r = {0};
    hdr(&r.h, T_REFUSE);
    r.token = s_token;
    if (slot < 0) {
        /* Only our own build (the latest release: both checked it); the
         * same custom fighters play with them, others with the base
         * fighters only, for the whole lobby. */
        const JoinExt* ext = n >= (int)(sizeof *m + sizeof(JoinExt)) &&
                                     ((const JoinExt*)(m + 1))->magic == JOIN_EXT
                                 ? (const JoinExt*)(m + 1)
                                 : NULL;
        const bool ok = ext != NULL && m->h.build == s_build;
        if (ok && ext->content != pc_fighter_packs_hash()) {
            go_base_only("different custom fighters");
        }
        if (!ok) {
            r.reason = ONLINE_ERR_VERSION;
        } else if (s_state != ONLINE_HOSTING) {
            r.reason = ONLINE_ERR_STARTED;
        } else {
            for (int i = 1; i < ONLINE_MAX_PLAYERS && slot < 0; i++) {
                if (!s_peer[i].used) {
                    slot = i;
                }
            }
            if (slot < 0) {
                r.reason = ONLINE_ERR_FULL;
            }
        }
        if (slot < 0) {
            send_to(&r, sizeof r, from);
            return;
        }
        Peer* p = &s_peer[slot];
        memset(p, 0, sizeof *p);
        p->used = true;
        p->addr = *from;
        p->port = -1;
        p->ping_ms = -1;
        snprintf(p->name, sizeof p->name, "%.*s", ONLINE_NAME_LEN - 1, m->name);
        pc_log_line("online: %s joined from %s (slot %d)", p->name, ip_text(from), slot);
    }
    s_peer[slot].heard_ms = now_ms();
    WelcomeMsg w;
    hdr(&w.h, T_WELCOME);
    w.token = s_token;
    w.slot = (uint8_t)slot;
    send_to(&w, sizeof w, from);
    s_lobby_ms = 0; /* tell everyone now */
}

static void host_receive(const uint8_t* buf, int n, const struct sockaddr_in* from) {
    const Hdr* h = (const Hdr*)buf;
    if (h->type == T_JOIN && n >= (int)sizeof(JoinMsg)) {
        on_join((const JoinMsg*)buf, n, from);
        return;
    }
    if (h->session != s_session || h->from == 0 || h->from >= ONLINE_MAX_PLAYERS) {
        return;
    }
    Peer* p = &s_peer[h->from];
    if (!p->used || !same_addr(&p->addr, from)) {
        return;
    }
    p->heard_ms = now_ms();
    switch (h->type) {
    case T_PONG:
        if (n >= (int)sizeof(PingMsg)) {
            const uint64_t sent = ((const PingMsg*)buf)->t;
            const int sample = (int)((SDL_GetTicksNS() - sent) / 1000000);
            /* Up at once, down slowly: the input delay is chosen from this,
             * and a link's slow moments are what it has to cover. */
            p->ping_ms =
                p->ping_ms < 0 || sample > p->ping_ms ? sample : (p->ping_ms * 7 + sample) / 8;
        }
        break;
    case T_SETUPACK:
        if (n >= (int)sizeof(SetupAckMsg) && ((const SetupAckMsg*)buf)->epoch == s_epoch) {
            const SetupAckMsg* a = (const SetupAckMsg*)buf;
            memcpy(p->chunk_have, a->have, sizeof p->chunk_have);
            if (a->done && !p->setup_done) {
                p->setup_done = true;
                pc_log_line("online: %s has the setup", p->name);
            }
        }
        break;
    case T_GOACK:
        if (n >= (int)sizeof(GoMsg) && ((const GoMsg*)buf)->epoch == s_epoch) {
            p->go_acked = true;
        }
        break;
    case T_INPUT: {
        const InputMsg* m = (const InputMsg*)buf;
        if (n < (int)(sizeof *m - sizeof m->pads) || m->epoch != s_epoch || !s_prepared ||
            p->port < 0 || m->count > SEND_SPAN)
        {
            break;
        }
        const int port = p->port;
        for (int i = 0; i < m->count; i++) {
            const int32_t f = m->first + i;
            if (f == s_input_have[port] + 1) {
                s_input[port][f & (RING - 1)] = m->pads[i];
                s_input_have[port] = f;
            }
        }
        p->input_have = s_input_have[port];
        if (m->bundle_have > p->bundle_have) {
            p->bundle_have = m->bundle_have;
        }
        if (m->ck_frame >= 0) {
            const int k = m->ck_frame & 63;
            p->ck_frame[k] = m->ck_frame;
            p->ck[k] = m->ck;
            if (s_ck_frame[k] == m->ck_frame && s_ck[k] != m->ck) {
                pc_log_line("online: OUT OF SYNC with %s at frame %d (%08x here, %08x there)",
                    p->name, m->ck_frame, s_ck[k], m->ck);
                end_game(ONLINE_ERR_DESYNC);
            }
        }
        break;
    }
    case T_BYE:
        drop_guest(h->from, ONLINE_ERR_PLAYER_LEFT);
        break;
    default:
        break;
    }
}

static void guest_receive(const uint8_t* buf, int n, const struct sockaddr_in* from) {
    const Hdr* h = (const Hdr*)buf;
    if (s_state == ONLINE_JOINING) {
        if (h->type == T_WELCOME && n >= (int)sizeof(WelcomeMsg) &&
            ((const WelcomeMsg*)buf)->token == s_target.token)
        {
            const WelcomeMsg* w = (const WelcomeMsg*)buf;
            s_peer[0].used = true;
            s_peer[0].addr = *from;
            s_peer[0].heard_ms = now_ms();
            s_session = h->session;
            s_slot = w->slot;
            s_state = ONLINE_JOINED;
            pc_log_line("online: joined the lobby at %s as slot %d", ip_text(from), s_slot);
        } else if (h->type == T_REFUSE && n >= (int)sizeof(RefuseMsg) &&
                   ((const RefuseMsg*)buf)->token == s_target.token)
        {
            fail((enum OnlineError)((const RefuseMsg*)buf)->reason);
        }
        return;
    }
    if (!s_peer[0].used || !same_addr(&s_peer[0].addr, from) || h->session != s_session ||
        h->from != 0)
    {
        return;
    }
    s_peer[0].heard_ms = now_ms();
    switch (h->type) {
    case T_LOBBY:
        if (n >= (int)sizeof(LobbyMsg)) {
            s_lobby = *(const LobbyMsg*)buf;
        }
        break;
    case T_PING:
        if (n >= (int)sizeof(PingMsg)) {
            PingMsg r = *(const PingMsg*)buf;
            hdr(&r.h, T_PONG);
            send_peer(0, &r, sizeof r);
        }
        break;
    case T_SETUP: {
        const SetupMsg* m = (const SetupMsg*)buf;
        if (n < (int)sizeof *m || m->size > (uint32_t)MAX_CHUNKS * CHUNK) {
            break;
        }
        if (m->epoch != s_epoch || s_blob_in == NULL) {
            s_epoch = m->epoch;
            free(s_blob_in);
            s_blob_in = malloc(m->size ? m->size : 1);
            s_blob_size = m->size;
            s_blob_hash = m->hash;
            s_chunks = m->chunks;
            memset(s_chunk_have, 0, sizeof s_chunk_have);
            s_setup_done = false;
            s_prepared = false;
            s_end = ONLINE_ERR_NONE;
            s_error = ONLINE_ERR_NONE;
            s_setup.seed = m->seed;
            s_setup.players = m->players;
            s_setup.delay = m->delay;
            s_setup.local_port = m->port;
            s_setup.unlock_all = m->unlock_all;
            s_setup.frozen_stadium = m->frozen_stadium;
            memcpy(s_slot_port, m->slot_port, sizeof s_slot_port);
            s_state = ONLINE_STARTING;
            pc_log_line("online: setup arriving: %d players, we are port %d, delay %d", m->players,
                m->port + 1, m->delay);
        }
        s_ack_sent_ms = 0;
        break;
    }
    case T_CHUNK: {
        const ChunkMsg* k = (const ChunkMsg*)buf;
        if (n < (int)(sizeof *k - CHUNK) || k->epoch != s_epoch || s_blob_in == NULL ||
            k->index >= s_chunks || (size_t)k->index * CHUNK + k->len > s_blob_size)
        {
            break;
        }
        memcpy(s_blob_in + (size_t)k->index * CHUNK, k->data, k->len);
        s_chunk_have[k->index / 8] |= (uint8_t)(1 << (k->index % 8));
        break;
    }
    case T_GO:
        if (n >= (int)sizeof(GoMsg) && ((const GoMsg*)buf)->epoch == s_epoch && s_setup_done) {
            GoMsg a;
            hdr(&a.h, T_GOACK);
            a.epoch = s_epoch;
            send_peer(0, &a, sizeof a);
            if (s_state == ONLINE_STARTING) {
                prepare_lockstep();
                s_state = ONLINE_READY;
                pc_log_line("online: go");
            }
        }
        break;
    case T_BUNDLE: {
        const BundleMsg* m = (const BundleMsg*)buf;
        if (n < (int)(sizeof *m - sizeof m->pads) || m->epoch != s_epoch || !s_prepared ||
            m->count > SEND_SPAN)
        {
            break;
        }
        if (m->input_have > s_host_has_mine) {
            s_host_has_mine = m->input_have;
        }
        for (int i = 0; i < m->count; i++) {
            const int32_t f = m->first + i;
            if (f > s_bundle_have && f <= s_bundle_have + RING) {
                memcpy(s_bundle[f & (RING - 1)], m->pads[i], sizeof s_bundle[0]);
                s_bundle_frame[f & (RING - 1)] = f;
            }
        }
        while (s_bundle_frame[(s_bundle_have + 1) & (RING - 1)] == s_bundle_have + 1) {
            s_bundle_have++;
            s_bundle_progress_ms = now_ms();
        }
        break;
    }
    case T_END:
        if (n >= (int)sizeof(EndMsg) && ((const EndMsg*)buf)->epoch == s_epoch) {
            end_game((enum OnlineError)((const EndMsg*)buf)->reason);
        }
        break;
    case T_BYE:
        fail(ONLINE_ERR_HOST_LEFT);
        break;
    default:
        break;
    }
}

static void receive_all(void) {
    if (s_sock == ON_INVALID) {
        return;
    }
    for (int k = 0; k < 256; k++) {
        uint8_t buf[2048];
        struct sockaddr_in from;
        socklen_t fn = sizeof from;
        const int n =
            (int)recvfrom(s_sock, (char*)buf, sizeof buf, 0, (struct sockaddr*)&from, &fn);
        if (n < (int)sizeof(Hdr)) {
            if (n < 0) {
                break;
            }
            continue;
        }
        const Hdr* h = (const Hdr*)buf;
        if (h->magic != MAGIC || h->proto != PROTO) {
            continue;
        }
        if (h->build != s_build && h->type != T_JOIN && h->type != T_REFUSE) {
            continue; /* another build: only the refusal that says so gets through */
        }
        /* A host whose lobby went base-only (a player with other custom
         * fighters joined) says so on every packet. */
        if (!s_is_host && (h->flags & HDR_BASE_ONLY) && h->type != T_REFUSE) {
            go_base_only("the host's lobby is base-only");
        }
        if (s_is_host) {
            host_receive(buf, n, &from);
        } else {
            guest_receive(buf, n, &from);
        }
    }
}

/* ---- sending ---------------------------------------------------------------------------- */

static void host_lobby_tick(void) {
    const uint64_t now = now_ms();
    if (now - s_lobby_ms >= 250) {
        s_lobby_ms = now;
        LobbyMsg m = {0};
        hdr(&m.h, T_LOBBY);
        for (int i = 0; i < ONLINE_MAX_PLAYERS; i++) {
            if (s_peer[i].used) {
                m.p[m.count].slot = (uint8_t)i;
                m.p[m.count].ping_ms = (int16_t)(i == 0 ? 0 : s_peer[i].ping_ms);
                snprintf(m.p[m.count].name, ONLINE_NAME_LEN, "%s", s_peer[i].name);
                m.count++;
            }
        }
        for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
            send_peer(i, &m, sizeof m);
        }
    }
    if (now - s_ping_ms >= 250) {
        s_ping_ms = now;
        PingMsg p;
        hdr(&p.h, T_PING);
        p.t = SDL_GetTicksNS();
        for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
            send_peer(i, &p, sizeof p);
        }
    }
    /* Guests that went quiet. */
    const uint64_t limit = s_state == ONLINE_PLAYING ? GAME_SILENCE_MS : LOBBY_SILENCE_MS;
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        if (s_peer[i].used && now - s_peer[i].heard_ms > limit) {
            drop_guest(i, ONLINE_ERR_TIMEOUT);
        }
    }
}

static void guest_tick(void) {
    const uint64_t now = now_ms();
    if (s_state == ONLINE_JOINING) {
        if (now - s_join_sent_ms >= 500) {
            s_join_sent_ms = now;
            struct {
                JoinMsg j;
                JoinExt x;
            } m;
            memset(&m, 0, sizeof m);
            hdr(&m.j.h, T_JOIN);
            m.j.token = s_target.token;
            snprintf(m.j.name, sizeof m.j.name, "%s", s_name);
            m.x.magic = JOIN_EXT;
            m.x.base_rev = 0;
            m.x.content = pc_fighter_packs_hash();
            snprintf(m.x.version, sizeof m.x.version, "%s", pc_app_rev());
            struct sockaddr_in to = {0};
            to.sin_family = AF_INET;
            /* The code's address (the host's router), and a broadcast on
             * this network: a host on the same Wi-Fi answers it even where
             * the router will not loop a call to its own address back. */
            if (s_target.address != 0) {
                to.sin_addr.s_addr = htonl(s_target.address);
                to.sin_port = htons(s_target.port);
                send_to(&m, sizeof m, &to);
            }
            to.sin_addr.s_addr = htonl(0xFFFFFFFFu);
            to.sin_port = htons(HOST_PORT);
            send_to(&m, sizeof m, &to);
            if (s_target.port != HOST_PORT) {
                to.sin_port = htons(s_target.port);
                send_to(&m, sizeof m, &to);
            }
        }
        if (now - s_started_ms > JOIN_GIVE_UP_MS) {
            fail(ONLINE_ERR_NO_ANSWER);
        }
        return;
    }
    if (s_state == ONLINE_STARTING && s_blob_in != NULL && !s_setup_done &&
        now - s_ack_sent_ms >= 100)
    {
        s_ack_sent_ms = now;
        int have = 0;
        for (int c = 0; c < s_chunks; c++) {
            have += (s_chunk_have[c / 8] >> (c % 8)) & 1;
        }
        SetupAckMsg a = {0};
        hdr(&a.h, T_SETUPACK);
        a.epoch = s_epoch;
        memcpy(a.have, s_chunk_have, sizeof a.have);
        if (have == s_chunks) {
            if (fnv(2166136261u, s_blob_in, s_blob_size) == s_blob_hash) {
                s_setup_done = true;
                a.done = 1;
                pc_log_line("online: have the setup (%zu bytes)", s_blob_size);
            } else {
                memset(s_chunk_have, 0, sizeof s_chunk_have); /* start over */
                memset(a.have, 0, sizeof a.have);
            }
        }
        send_peer(0, &a, sizeof a);
    } else if (s_state == ONLINE_STARTING && s_setup_done && now - s_ack_sent_ms >= 250) {
        s_ack_sent_ms = now; /* keep saying so until GO */
        SetupAckMsg a = {0};
        hdr(&a.h, T_SETUPACK);
        a.epoch = s_epoch;
        memset(a.have, 0xFF, sizeof a.have);
        a.done = 1;
        send_peer(0, &a, sizeof a);
    }
    const uint64_t limit = s_state == ONLINE_PLAYING ? GAME_SILENCE_MS : LOBBY_SILENCE_MS;
    if (now - s_peer[0].heard_ms > limit) {
        fail(ONLINE_ERR_TIMEOUT);
    }
}

/* Guest: our newest inputs the host may not have, the newest bundle we hold
 * and the latest state hash. */
static void guest_send_inputs(void) {
    if (!s_prepared) {
        return;
    }
    InputMsg m;
    hdr(&m.h, T_INPUT);
    m.epoch = s_epoch;
    int32_t first = s_host_has_mine + 1;
    if (first < s_local_newest - SEND_SPAN + 1) {
        first = s_local_newest - SEND_SPAN + 1;
    }
    if (first < 0) {
        first = 0;
    }
    m.first = first;
    m.count = 0;
    for (int32_t f = first; f <= s_local_newest; f++) {
        m.pads[m.count++] = s_local[f & (RING - 1)];
    }
    m.bundle_have = s_bundle_have;
    m.ck_frame = s_ck_pending_frame;
    m.ck = s_ck_pending;
    send_peer(0, &m, sizeof m - sizeof m.pads + m.count * sizeof m.pads[0]);
}

/* Host: every bundle that is complete, then to each guest what it lacks. */
static void host_build_and_send(bool force) {
    if (!s_prepared) {
        return;
    }
    for (;;) {
        const int32_t f = s_bundle_have + 1;
        bool complete = true;
        for (int port = 0; port < s_setup.players; port++) {
            if (s_input_have[port] < f) {
                complete = false;
            }
        }
        if (!complete) {
            break;
        }
        for (int port = 0; port < ONLINE_MAX_PLAYERS; port++) {
            static const OnlinePad none;
            s_bundle[f & (RING - 1)][port] =
                port < s_setup.players ? s_input[port][f & (RING - 1)] : none;
        }
        s_bundle_frame[f & (RING - 1)] = f;
        s_bundle_have = f;
        s_bundle_progress_ms = now_ms();
        force = true;
    }
    const uint64_t now = now_ms();
    for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
        Peer* p = &s_peer[i];
        if (!p->used || p->port < 0) {
            continue;
        }
        if (!force && now - p->sent_ms < 8) {
            continue;
        }
        p->sent_ms = now;
        BundleMsg m;
        hdr(&m.h, T_BUNDLE);
        m.epoch = s_epoch;
        m.players = s_setup.players;
        m.input_have = s_input_have[p->port];
        int32_t first = p->bundle_have + 1;
        if (first < s_bundle_have - SEND_SPAN + 1) {
            first = s_bundle_have - SEND_SPAN + 1;
        }
        if (first < 0) {
            first = 0;
        }
        m.first = first;
        m.count = 0;
        for (int32_t f = first; f <= s_bundle_have && m.count < SEND_SPAN; f++) {
            memcpy(m.pads[m.count++], s_bundle[f & (RING - 1)], sizeof m.pads[0]);
        }
        send_peer(i, &m, sizeof m - sizeof m.pads + m.count * sizeof m.pads[0]);
    }
}

void online_poll(void) {
    if (s_state == ONLINE_OFF || s_sock == ON_INVALID) {
        return;
    }
    receive_all();
    if (s_state == ONLINE_FAILED) {
        flush_held();
        return;
    }
    if (s_is_host) {
        host_lobby_tick();
        if (s_state == ONLINE_STARTING) {
            host_send_setup();
            if (s_state == ONLINE_READY) {
                prepare_lockstep();
            }
        }
        if (s_state == ONLINE_READY || s_state == ONLINE_PLAYING || s_state == ONLINE_HOSTING) {
            host_build_and_send(false);
        }
    } else {
        guest_tick();
        if ((s_state == ONLINE_READY || s_state == ONLINE_PLAYING || s_state == ONLINE_JOINED) &&
            now_ms() - s_peer[0].sent_ms >= 8)
        {
            s_peer[0].sent_ms = now_ms();
            guest_send_inputs();
        }
    }
    flush_held();
}

/* ---- lockstep ---------------------------------------------------------------------------------
 */

void online_lockstep_begin(void) {
    if (s_state != ONLINE_READY) {
        return;
    }
    prepare_lockstep();
    s_state = ONLINE_PLAYING;
    pc_log_line("online: lockstep from frame 0 as port %d of %d, delay %d", s_setup.local_port + 1,
        s_setup.players, s_setup.delay);
}

/* The rings stay (s_prepared): a machine that is still a few frames behind
 * gets the rest of the game's bundles while the others wait in the lobby,
 * and the host starts no new game until it has them (players_caught_up). */
void online_lockstep_end(void) {
    if (s_state == ONLINE_PLAYING || s_state == ONLINE_READY || s_state == ONLINE_STARTING) {
        pc_log_line("online: back to the lobby");
        s_state = s_is_host ? ONLINE_HOSTING : ONLINE_JOINED;
    }
}

bool online_lockstep_active(void) {
    return s_state == ONLINE_PLAYING && s_end == ONLINE_ERR_NONE;
}

void online_submit(int32_t frame, const OnlinePad* pad) {
    if (!s_prepared) {
        return;
    }
    if (frame <= s_local_newest) {
        return; /* a frame already sent never changes */
    }
    for (int32_t f = s_local_newest + 1; f <= frame; f++) {
        s_local[f & (RING - 1)] = *pad;
    }
    s_local_newest = frame;
    if (s_is_host) {
        const int port = s_setup.local_port;
        for (int32_t f = s_input_have[port] + 1; f <= frame; f++) {
            s_input[port][f & (RING - 1)] = *pad;
        }
        s_input_have[port] = frame;
        host_build_and_send(false);
    } else {
        s_peer[0].sent_ms = now_ms();
        guest_send_inputs();
    }
    flush_held();
}

bool online_bundle(int32_t frame, OnlinePad out[ONLINE_MAX_PLAYERS]) {
    if (!s_prepared || frame > s_bundle_have || s_bundle_frame[frame & (RING - 1)] != frame) {
        return false;
    }
    memcpy(out, s_bundle[frame & (RING - 1)], sizeof s_bundle[0]);
    return true;
}

bool online_wait_bundle(int32_t frame) {
    static uint64_t logged_ms;
    const uint64_t t0 = now_ms();
    uint64_t pumped_ms = t0;
    while (online_lockstep_active() && s_state != ONLINE_FAILED) {
        online_poll();
        if (frame <= s_bundle_have && s_bundle_frame[frame & (RING - 1)] == frame) {
            return true;
        }
        const uint64_t now = now_ms();
        if (now - s_bundle_progress_ms > GAME_SILENCE_MS) {
            end_game(ONLINE_ERR_TIMEOUT);
            break;
        }
        if (now - t0 > 1000 && now - logged_ms > 2000) {
            logged_ms = now;
            pc_log_line("online: waiting for frame %d (have %d)", frame, s_bundle_have);
        }
        /* A long wait would leave the window's messages unread, and Windows
         * marks such a window "not responding" after about 5 s. Pumping only
         * queues them: the next frame boundary handles them as usual. */
        if (now - t0 > 100 && now - pumped_ms >= 16) {
            pumped_ms = now;
            SDL_PumpEvents();
        }
        SDL_DelayNS(250000);
    }
    return false;
}

void online_note_hash(int32_t frame, uint32_t hash) {
    const int k = frame & 63;
    s_ck_frame[k] = frame;
    s_ck[k] = hash;
    if (s_is_host) {
        for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
            const Peer* p = &s_peer[i];
            if (p->used && p->port >= 0 && p->ck_frame[k] == frame && p->ck[k] != hash) {
                pc_log_line("online: OUT OF SYNC with %s at frame %d (%08x here, %08x there)",
                    p->name, frame, hash, p->ck[k]);
                end_game(ONLINE_ERR_DESYNC);
            }
        }
    } else {
        s_ck_pending_frame = frame;
        s_ck_pending = hash;
    }
}

int online_ping_ms(void) {
    int worst = 0;
    if (s_is_host) {
        for (int i = 1; i < ONLINE_MAX_PLAYERS; i++) {
            if (s_peer[i].used && s_peer[i].ping_ms > worst) {
                worst = s_peer[i].ping_ms;
            }
        }
    } else {
        for (int i = 0; i < s_lobby.count && i < ONLINE_MAX_PLAYERS; i++) {
            if (s_lobby.p[i].slot == s_slot && s_lobby.p[i].ping_ms > worst) {
                worst = s_lobby.p[i].ping_ms;
            }
        }
    }
    return worst;
}
