/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_ONLINE_H
#define PC_ONLINE_H

/* Online play: Host Lobby / Join Lobby for 2-4 players, delay-based
 * lockstep (src/pc/online.c). docs/online.md has the design.
 *
 * The host owns the session. Guests connect to it with its lobby code
 * (net_code.h); only the host has to be reachable. At Start the host sends
 * every guest the setup: its persistent game data, the seed, the input
 * delay and each player's port. Then every machine plays from the first
 * frame of character select in lockstep: a frame is simulated only from the
 * host's bundle of everyone's input for it, so every machine feeds the game
 * the same inputs and, from the same start, plays the same game. Every 30
 * frames each machine's state hash goes to the host, which ends the session
 * on any difference rather than let it run on wrong. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONLINE_MAX_PLAYERS 4
#define ONLINE_NAME_LEN 16

typedef struct OnlinePad { /* one player's input for one frame */
    uint16_t button;
    int8_t stick_x, stick_y, sub_x, sub_y;
    uint8_t trigger_l, trigger_r;
    /* The mouse pointer in a menu's world (character and stage select), in
     * 1/256 units, when `has_ptr`: menus move the player's hand there, so it
     * travels with the input like a stick. */
    uint8_t has_ptr, unused;
    int16_t ptr_x, ptr_y;
} OnlinePad;

enum OnlineState {
    ONLINE_OFF,
    ONLINE_HOSTING,  /* host: lobby open */
    ONLINE_JOINING,  /* guest: calling the host */
    ONLINE_JOINED,   /* guest: in the host's lobby */
    ONLINE_STARTING, /* setup going out (host) or coming in (guest) */
    ONLINE_READY,    /* setup done everywhere: enter character select */
    ONLINE_PLAYING,  /* lockstep running */
    ONLINE_FAILED,   /* online_error() says why; online_leave() to reset */
};

enum OnlineError {
    ONLINE_ERR_NONE,
    ONLINE_ERR_SOCKET,    /* could not open a network port */
    ONLINE_ERR_NO_ANSWER, /* joining: the host did not answer */
    ONLINE_ERR_FULL,
    ONLINE_ERR_VERSION, /* another version of Melee 4K */
    ONLINE_ERR_STARTED, /* the host's game had already started */
    ONLINE_ERR_HOST_LEFT,
    ONLINE_ERR_PLAYER_LEFT, /* a player left mid-game: back to the lobby */
    ONLINE_ERR_TIMEOUT,     /* nothing heard for too long */
    ONLINE_ERR_DESYNC,      /* the machines' games differ */
    ONLINE_ERR_OUTDATED,    /* this copy is not the latest release */
    ONLINE_ERR_UNCHECKED,   /* the latest release could not be checked */
};

typedef struct OnlinePlayer {
    char name[ONLINE_NAME_LEN];
    int port;    /* game port this match, 0-3; -1 before the setup */
    int ping_ms; /* to the host; -1 unknown */
    bool local;
} OnlinePlayer;

/* What the setup carries besides the host's game data (online_setup_blob). */
typedef struct OnlineSetup {
    uint32_t seed;
    uint8_t players; /* 2-4 */
    uint8_t delay;   /* input delay, frames */
    uint8_t local_port;
    uint8_t unlock_all;
    uint8_t frozen_stadium;
} OnlineSetup;

/* ---- lobby ---------------------------------------------------------------- */

/* Open a lobby (true) or join one by its code. `name` is shown to the others. */
bool online_host(const char* name);
bool online_join(const char* code, const char* name);
/* Leave whatever there is: says goodbye, closes the port, resets to OFF. */
void online_leave(void);
/* Once per frame outside lockstep (the lobby scene); lockstep polls itself. */
void online_poll(void);

enum OnlineState online_state(void);
enum OnlineError online_error(void);
const char* online_error_text(void);
bool online_is_host(void);
/* Host: the lobby code ("XXXXX-XXXXX-XXXXX") once the router has answered,
 * and what it allows (a PcPortmapState). False while still asking. */
bool online_code(char out[18], int* reach);
int online_players(OnlinePlayer out[ONLINE_MAX_PLAYERS]);
/* Host: puts the lobby code on the clipboard. */
bool online_copy_code(void);
/* A lobby code on the clipboard, canonical (15 characters), or false. */
bool online_clipboard_code(char out[16]);

/* True when this lobby plays the base fighters only: its players (all on
 * the latest release) have different custom fighter packs. Custom
 * fighters are hidden then. */
bool online_base_only(void);

/* Host: true once a game can start: 2+ players, everyone's ping measured,
 * nobody still finishing the last game. */
bool online_can_start(void);
/* Host: start (2+ players). `blob` is the game's persistent data, sent to
 * every guest; it must stay valid until ONLINE_READY. */
bool online_start(const void* blob, size_t size, bool unlock_all, bool frozen_stadium);
/* ONLINE_READY: the setup everyone agreed on. A guest also gets the host's
 * data (`size` bytes into `blob`), to load before character select. */
bool online_setup(OnlineSetup* out, void* blob, size_t size);

/* ---- lockstep ---------------------------------------------------------------- */

/* Character select's first frame is frame 0. */
void online_lockstep_begin(void);
/* Back in the lobby (everyone, on the same frame): the connection stays. */
void online_lockstep_end(void);
bool online_lockstep_active(void);
/* Our input for frame f (f = the frame being simulated + the delay). */
void online_submit(int32_t frame, const OnlinePad* pad);
/* Everyone's inputs for frame f, by port, once the host's bundle is here. */
bool online_bundle(int32_t frame, OnlinePad out[ONLINE_MAX_PLAYERS]);
/* Waits (polling the network) until bundle f is here or the session fails;
 * false on failure. */
bool online_wait_bundle(int32_t frame);
/* This machine's state hash for frame f (every 30 frames is enough). */
void online_note_hash(int32_t frame, uint32_t hash);
int online_ping_ms(void); /* the worst player's, for the HUD */

#ifdef __cplusplus
}
#endif
#endif
