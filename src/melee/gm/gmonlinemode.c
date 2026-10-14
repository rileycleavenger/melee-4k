#include "gmonlinemode.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <melee/lb/forward.h>

#include "forward.h"
#include "gm_1601.h"
#include "gm_1A36.h"
#include "gm_1A3F.h"
#include "gm_unsplit.h"
#include "gmmain_lib.h"
#include "gmresult.h"
#include "gmscene.h"
#include "gmvsmelee.h"
#include "types.h"
#include <dolphin/pad.h>
#include <melee/if/if_2FD9.h>
#include <melee/lb/types.h>
#include <melee/mn/inlines.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>
#ifdef TARGET_PC
#include <aurora/card.h>
#include "pc/net.h"
#include "pc/net_code.h"
#include "pc/net_portmap.h"
#include "pc/online.h"
#include "pc/pc.h"
extern const char* pc_get_net_name(void);
extern const char* pc_get_net_target(void);
extern void pc_set_net_target(const char* code);
#endif

/* GM_ONLINE: lobby -> CSS -> SSS -> VS -> (sudden death) -> results -> CSS,
 * the vanilla VS flow (gmvsmode.c) on a private VsModeData.
 *
 * The lobby (Host Lobby / Join Lobby) runs offline: pc/online.c gathers up
 * to four players around the host. When the host presses Start, every
 * machine gets the host's game data (rules, teams, unlocks, names: struct
 * gmm_x0), the seed and its port, and enters the CSS; from the CSS's first
 * frame every scene runs in lockstep on the host's bundle of everyone's
 * inputs (pc/net.c), so every machine plays the same game. B on the CSS, or
 * a game that ends early (a player left, out of sync), brings everyone back
 * to the lobby, where each machine gets its own game data back. */

enum {
    state_lobby = 0,
    state_css = 1,
    state_sss = 2,
    state_vs = 3,
    state_sudden_death = 4,
    state_results = 5, /* last: gmVsMelee_ExitResults skips challengers */
};

static void onEnterLobby(GameModeState*);
static void onEnterCss(GameModeState*);
static void onExitCss(GameModeState*);
static void onEnterSss(GameModeState*);
static void onExitSss(GameModeState*);
static void onEnterVs(GameModeState*);
static void onExitVs(GameModeState*);
static void onEnterSuddenDeath(GameModeState*);
static void onExitSuddenDeath(GameModeState*);
static void onEnterResults(GameModeState*);
static void onExitResults(GameModeState*);

GameModeState gm_Mode_Online_States[] = {
    {
        state_lobby,
        lbDvdPreload_3,
        0,
        onEnterLobby,
        NULL,
        {
            GS_ONLINE_LOBBY,
            NULL,
            NULL,
        },
    },
    {
        state_css,
        lbDvdPreload_3,
        0,
        onEnterCss,
        onExitCss,
        {
            GS_CSS,
            &gmVsMelee_CssData,
            &gmVsMelee_CssData,
        },
    },
    {
        state_sss,
        lbDvdPreload_3,
        0,
        onEnterSss,
        onExitSss,
        {
            GS_SSS,
            &gmVsMelee_SssData,
            &gmVsMelee_SssData,
        },
    },
    {
        state_vs,
        lbDvdPreload_3,
        0,
        onEnterVs,
        onExitVs,
        {
            GS_VS,
            &gmVsMelee_StartData,
            &gmVsMelee_VsExitInfo,
        },
    },
    {
        state_sudden_death,
        lbDvdPreload_3,
        0,
        onEnterSuddenDeath,
        onExitSuddenDeath,
        {
            GS_SUDDEN_DEATH,
            &gmVsMelee_StartData,
            &gmVsMelee_SuddenDeathExitInfo,
        },
    },
    {
        state_results,
        lbDvdPreload_3,
        0,
        onEnterResults,
        onExitResults,
        {
            GS_RESULTS,
            &gmVsMelee_ResultsEnterData,
            NULL,
        },
    },
    { GM_GAMEMODESTATE_TERMINATE },
};

static OnlineKind online_kind;
static VsModeData online_vs;

void gmOnline_SetKind(OnlineKind kind)
{
    online_kind = kind;
}

OnlineKind gmOnline_GetKind(void)
{
    return online_kind;
}

/* ---- the session's game data ------------------------------------------- */
#ifdef TARGET_PC
/* Set in the lobby when the setup is in: the next CSS is the game's first
 * frame. */
static bool session_pending;
#define LOBBY_HOLD_FRAMES 40 /* hold B this long to leave a lobby */
static OnlineSetup session_setup;
/* This machine's own game data, put back when the session ends. */
static struct gmm_x0 own_data;
static bool own_data_saved;

/* The function pointers in each VS mode's rules are addresses in the host's
 * process: keep ours. They are the only pointers in the data (on 64-bit
 * builds struct gmm_x0 is larger by exactly these). */
static void keepOwnPointers(void)
{
    const size_t from = offsetof(StartMeleeRules, on_unpause_override);
    for (int i = 0; i < GmVsMode_Count; i++) {
        memcpy((u8*) &gmMainLib_804D3EE0->modes.table[i].start.rules + from,
               (const u8*) &own_data.modes.table[i].start.rules + from,
               sizeof(StartMeleeRules) - from);
    }
}

/* The game data every machine starts from, without the pointers
 * (keepOwnPointers): equal on every machine, or the log says why not. */
static u32 sessionDataHash(void)
{
    static struct gmm_x0 tmp;
    const size_t from = offsetof(StartMeleeRules, on_unpause_override);
    u32 h = 2166136261u;
    memcpy(&tmp, gmMainLib_804D3EE0, sizeof tmp);
    for (int i = 0; i < GmVsMode_Count; i++) {
        memset((u8*) &tmp.modes.table[i].start.rules + from, 0, sizeof(StartMeleeRules) - from);
    }
    for (size_t i = 0; i < sizeof tmp; i++) {
        h = (h ^ ((const u8*) &tmp)[i]) * 16777619u;
    }
    return h;
}

/* ONLINE_READY: load what every machine starts from. False when the
 * host's data did not arrive whole. */
static bool sessionLoad(void)
{
    memcpy(&own_data, gmMainLib_804D3EE0, sizeof own_data);
    own_data_saved = true;
    if (!online_setup(&session_setup, gmMainLib_804D3EE0, sizeof(struct gmm_x0))) {
        memcpy(gmMainLib_804D3EE0, &own_data, sizeof own_data);
        own_data_saved = false;
        return false;
    }
    keepOwnPointers();
    /* The host's data must not be saved over this machine's, and the host's
     * own card must not be written mid-game either: a save's completion
     * would land on a different frame than everyone else's no-op. */
    aurora_card_set_read_only(true);
    pc_log_line("online: game data loaded (%s, hash %08x), %d players, we are P%d",
                online_is_host() ? "ours" : "the host's", sessionDataHash(),
                session_setup.players, session_setup.local_port + 1);
    return true;
}

static void sessionUnload(void)
{
    if (own_data_saved) {
        memcpy(gmMainLib_804D3EE0, &own_data, sizeof own_data);
        own_data_saved = false;
    }
    aurora_card_set_read_only(false);
}
#endif

void onEnterLobby(UNUSED GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_active()) {
        pc_net_session_end();
    }
    session_pending = false;
    sessionUnload();
#endif
    gm_InitVsMode(&online_vs);
}

void onEnterCss(GameModeState* state)
{
#ifdef TARGET_PC
    if (session_pending) {
        /* Same CSS start everywhere: one human door per player, nothing
         * picked, on the host's rules. */
        session_pending = false;
        gm_InitVsMode(&online_vs);
        for (int i = 0; i < GM_MAX_PLAYERS; ++i) {
            online_vs.start.players[i].slot_type =
                i < session_setup.players ? Gm_PKind_Human : Gm_PKind_NA;
        }
        pc_net_session_begin(&session_setup);
    }
    pc_log_line("online: enter CSS at frame %d", pc_net_frame());
#endif
    gmVsMelee_EnterCss(state, &online_vs, VS_MELEE);
}

void onExitCss(GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_peer_status() != PC_NET_PEER_OK) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
#endif
    CSSData* css = gm_GetGameModeStateExitData(state);
    if (css->pending_scene_change == CSSPendingSceneChange_2) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
    gmVsMelee_ExitCss(state, &online_vs);
}

void onEnterSss(GameModeState* state)
{
#ifdef TARGET_PC
    pc_log_line("online: enter SSS at frame %d", pc_net_frame());
#endif
    gmVsMelee_EnterSss(state, &online_vs);
}

void onExitSss(GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_peer_status() != PC_NET_PEER_OK) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
#endif
    gmVsMelee_ExitSss(state, &online_vs, state_css);
}

void onEnterVs(GameModeState* state)
{
#ifdef TARGET_PC
    /* MELEE_ONLINE_TEST_STAGE=<stage kind>: every game on that stage, for
     * tests (set on every machine alike). */
    if (getenv("MELEE_ONLINE_TEST_STAGE") != NULL) {
        online_vs.start.rules.stkind = (u8) atoi(getenv("MELEE_ONLINE_TEST_STAGE"));
    }
    {
        const PlayerInitData* pl = online_vs.start.players;
        pc_log_line("online: enter VS at frame %d: stage %d, characters %d %d %d %d",
                    pc_net_frame(), online_vs.start.rules.stkind,
                    pl[0].slot_type == Gm_PKind_NA ? -1 : pl[0].ckind,
                    pl[1].slot_type == Gm_PKind_NA ? -1 : pl[1].ckind,
                    pl[2].slot_type == Gm_PKind_NA ? -1 : pl[2].ckind,
                    pl[3].slot_type == Gm_PKind_NA ? -1 : pl[3].ckind);
    }
#endif
    gmVsMelee_EnterVs(state, &online_vs, NULL, NULL);
}

void onExitVs(GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_peer_status() != PC_NET_PEER_OK) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
#endif
    MatchExitInfo* mei;
    ssize_t i;

    gmVsMelee_ExitVs(state, state_results, state_sudden_death);
    mei = gm_GetGameModeStateExitData(state);
    for (i = 0; i < GM_MAX_PLAYERS; i++) {
        if (mei->match_end.player_standings[i].pkind != Gm_PKind_NA) {
            gm_80162A98(mei->match_end.player_standings[i].x20);
            gm_RecordSelfDestructs(
                mei->match_end.player_standings[i].self_destructs);
            gm_80162A4C(mei->match_end.player_standings[i].x44);
        }
    }
}

void onEnterSuddenDeath(GameModeState* state)
{
    gmVsMelee_EnterSuddenDeath(state, &online_vs, NULL, NULL);
}

void onExitSuddenDeath(GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_peer_status() != PC_NET_PEER_OK) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
#endif
    gmVsMelee_ExitSuddenDeath(state);
}

void onEnterResults(GameModeState* state)
{
#ifdef TARGET_PC
    pc_log_line("online: enter RESULTS at frame %d", pc_net_frame());
#endif
    gmVsMelee_EnterResults(state);
}

void onExitResults(GameModeState* state)
{
#ifdef TARGET_PC
    if (pc_net_peer_status() != PC_NET_PEER_OK) {
        gm_SetNextGameModeStateId(state_lobby);
        return;
    }
#endif
    gmVsMelee_ExitResults(state, &online_vs, state_css);
    if (!gm_WasMatchCanceled(gmVsMelee_ResultsEnterData.match_end.outcome)) {
        gm_801623A4(&gmVsMelee_ResultsEnterData.match_end);
    }
}

/* ---- lobby scene ------------------------------------------------------- */
#ifdef TARGET_PC
static OnlineDirectRole lobby_role;

void gmOnline_SetDirectRole(OnlineDirectRole role)
{
    lobby_role = role;
}

/* Join Lobby's code entry: the host's fifteen characters, A-Z and 2-7, on a
 * grid (it wraps, so any key is at most six moves away), typed on a
 * keyboard or pasted. */
static const char code_keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static bool code_entry;           /* the keyboard owns the screen */
static int code_key;              /* keyboard cell */
static char code_typed[PC_NET_CODE_LEN + 1];
static char code_note[ONLINE_LOBBY_MSG_LEN];
static char last_game[ONLINE_LOBBY_MSG_LEN]; /* why the last game ended early */
static unsigned lobby_frames;

static void codeEntryBegin(void)
{
    code_entry = true;
    code_key = 0;
    code_note[0] = '\0';
    code_typed[0] = '\0';
    /* A code copied from a chat is already typed in; else the last one. */
    if (online_clipboard_code(code_typed)) {
        snprintf(code_note, sizeof code_note, "Code pasted from your clipboard - START: join");
    } else if (pc_net_code_normalize(pc_get_net_target(), code_typed)) {
        snprintf(code_note, sizeof code_note, "Your last code - START: join");
    } else {
        code_typed[0] = '\0';
    }
}

/* One typed character: upper case, and 0/1/8 read as O/I/B, as the code
 * itself does (net_code.c). */
static void codeType(char ch)
{
    int len = (int) strlen(code_typed);
    ch = (char) toupper((unsigned char) ch);
    ch = ch == '0' ? 'O' : ch == '1' ? 'I' : ch == '8' ? 'B' : ch;
    if (strchr(code_keys, ch) != NULL && ch != '\0' && len < PC_NET_CODE_LEN) {
        code_typed[len++] = ch;
        code_typed[len] = '\0';
        code_note[0] = '\0';
    }
}

static void codePaste(void)
{
    if (online_clipboard_code(code_typed)) {
        snprintf(code_note, sizeof code_note, "Code pasted - START: join");
    } else {
        snprintf(code_note, sizeof code_note, "No lobby code on your clipboard");
    }
}

static void lobbyJoin(const char* code)
{
    code_entry = false;
    pc_set_net_target(code);
    last_game[0] = '\0';
    pc_log_line("lobby: joining %s", code);
    online_join(code, pc_get_net_name());
}

/* One frame of the code keyboard. False once a code was entered and the
 * screen moves on to joining. */
static bool codeEntryFrame(OnlineLobbyView* view, u64 input)
{
    u64 repeat = gm_801A36C0(PAD_MAX_CONTROLLERS);
    char typed[64];
    pc_keyboard_text_want();
    const int n = pc_keyboard_take_text(typed, sizeof typed);
    for (int i = 0; i < n; i++) {
        if (typed[i] == '\b') {
            const int l = (int) strlen(code_typed);
            if (l > 0) {
                code_typed[l - 1] = '\0';
            }
        } else if (typed[i] == 0x16) {
            codePaste();
        } else if (typed[i] == '\r' || typed[i] == '\n') {
            input |= HSD_PAD_START;
        } else {
            codeType(typed[i]);
        }
    }
    if (input & HSD_PAD_Y) {
        codePaste();
        sfxForward();
    }
    const int keys = (int) sizeof code_keys - 1;
    int len = (int) strlen(code_typed);
    if (repeat & PAD_ANY_LEFT) {
        code_key = code_key / ONLINE_LOBBY_KEY_COLS * ONLINE_LOBBY_KEY_COLS +
                   (code_key + ONLINE_LOBBY_KEY_COLS - 1) % ONLINE_LOBBY_KEY_COLS;
        sfxMove();
    } else if (repeat & PAD_ANY_RIGHT) {
        code_key = code_key / ONLINE_LOBBY_KEY_COLS * ONLINE_LOBBY_KEY_COLS +
                   (code_key + 1) % ONLINE_LOBBY_KEY_COLS;
        sfxMove();
    } else if (repeat & PAD_ANY_UP) {
        code_key = (code_key + keys - ONLINE_LOBBY_KEY_COLS) % keys;
        sfxMove();
    } else if (repeat & PAD_ANY_DOWN) {
        code_key = (code_key + ONLINE_LOBBY_KEY_COLS) % keys;
        sfxMove();
    }
    if ((input & HSD_PAD_A) && len < PC_NET_CODE_LEN) {
        code_typed[len++] = code_keys[code_key];
        code_typed[len] = '\0';
        code_note[0] = '\0';
        sfxForward();
    } else if (input & (HSD_PAD_B | PAD_CANCEL)) {
        if (len > 0) {
            code_typed[--len] = '\0';
            sfxBack();
        }
    } else if (input & HSD_PAD_START) {
        PcNetLobbyCode code;
        if (len == PC_NET_CODE_LEN && pc_net_code_decode(code_typed, &code)) {
            sfxForward();
            lobbyJoin(code_typed);
            return false;
        }
        snprintf(code_note, sizeof code_note,
                 len == PC_NET_CODE_LEN ? "That code isn't right - check it" :
                                          "A code has 15 characters");
        sfxBack();
    }
    view->screen = LOBBY_SCREEN_KEYS;
    view->keys = code_keys;
    view->key_cursor = code_key;
    view->menu_count = 1;
    char slots[PC_NET_CODE_LEN + 1];
    for (int i = 0; i < PC_NET_CODE_LEN; i++) {
        slots[i] = i < len ? code_typed[i] : '_';
    }
    slots[PC_NET_CODE_LEN] = '\0';
    snprintf(view->menu[0], sizeof view->menu[0], "Host's code   %.5s-%.5s-%.5s", slots,
             slots + 5, slots + 10);
    snprintf(view->message, sizeof view->message, "%s",
             code_note[0] ? code_note : "Type the code the host shows, or paste it");
    view->hint = "A: type   B: delete   Y: paste   START: join   Hold B / Esc: menu";
    view->phase = LOBBY_PHASE_FOUND;
    return true;
}

static void lobbyOpen(void)
{
    if (lobby_role == ONLINE_DIRECT_HOST) {
        online_host(pc_get_net_name());
        return;
    }
    codeEntryBegin();
    /* MELEE_ONLINE_JOIN=<code>: typed in for you, for tests. */
    const char* join = getenv("MELEE_ONLINE_JOIN");
    if (join != NULL && join[0] != '\0') {
        char code[PC_NET_CODE_LEN + 1];
        if (pc_net_code_normalize(join, code)) {
            lobbyJoin(code);
        }
    }
}

/* MELEE_ONLINE_AUTOSTART=<n>: the host starts once n players are in, for
 * tests. */
static int lobbyAutostart(void)
{
    static int n = -1;
    if (n < 0) {
        const char* e = getenv("MELEE_ONLINE_AUTOSTART");
        n = e != NULL ? atoi(e) : 0;
    }
    return n;
}

static void lobbyStart(void)
{
    /* The host's own settings that live outside the game data. */
    const bool unlock_all = pc_is_unlock_all_enabled();
    const bool frozen = pc_is_frozen_stadium_enabled();
    if (online_start(gmMainLib_804D3EE0, sizeof(struct gmm_x0), unlock_all, frozen)) {
        sfxForward();
        last_game[0] = '\0';
        pc_net_peer_status_clear();
    }
}

static void lobbyPlayers(OnlineLobbyView* view)
{
    OnlinePlayer players[ONLINE_MAX_PLAYERS];
    const int n = online_players(players);
    view->player_count = 0;
    for (int i = 0; i < n && i < ONLINE_LOBBY_MAX_PLAYERS; i++) {
        OnlineLobbyPlayer* p = &view->players[view->player_count++];
        snprintf(p->name, sizeof p->name, "%s", players[i].name);
        p->ping_ms = players[i].local ? -1 : players[i].ping_ms;
        p->is_host = i == 0;
        p->is_local = players[i].local;
    }
}

void gm_Scene_OnlineLobby_OnEnter(UNUSED void* unused)
{
    mnOnlineLobby_Create();
    lobby_frames = 0;
    code_entry = false;
    const int status = pc_net_peer_status();
    if (status != PC_NET_PEER_OK && online_state() != ONLINE_OFF) {
        /* Back from a game that ended early. */
        snprintf(last_game, sizeof last_game, "%s", online_error_text());
    }
    pc_net_peer_status_clear();
    if (online_state() == ONLINE_OFF) {
        last_game[0] = '\0';
        /* MELEE_TEST_LOCAL_RULES=1: this machine's own rules differ (teams,
         * one stock), as a joiner's did in the v1.1.0-rc1 test; the game
         * must still start on the host's. */
        if (getenv("MELEE_TEST_LOCAL_RULES") != NULL) {
            gmMainLib_804D3EE0->x1850.stock_count = 1;
            gmMainLib_804D3EE0->modes.table[GmVsMode_Melee].start.rules.is_teams = true;
            pc_log_line("lobby: test: local rules changed (teams, 1 stock)");
        }
        lobbyOpen();
    }
}

void gm_Scene_OnlineLobby_OnExit(UNUSED void* unused)
{
    mnOnlineLobby_Destroy();
}
#else
void gmOnline_SetDirectRole(OnlineDirectRole role)
{
    (void) role;
}

void gm_Scene_OnlineLobby_OnEnter(UNUSED void* unused)
{
    mnOnlineLobby_Create();
}

void gm_Scene_OnlineLobby_OnExit(UNUSED void* unused)
{
    mnOnlineLobby_Destroy();
}
#endif

void gm_Scene_OnlineLobby_OnFrame(void)
{
#ifdef TARGET_PC
    static char last_status[ONLINE_LOBBY_MSG_LEN];
    OnlineLobbyView view;
    u64 input = gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS);
    bool leave = false;
    lobby_frames++;
    memset(&view, 0, sizeof view);
    view.title = lobby_role == ONLINE_DIRECT_HOST ? "HOST LOBBY" : "JOIN LOBBY";

    online_poll();
    const enum OnlineState st = online_state();
    if (code_entry) {
        codeEntryFrame(&view, input);
        input = 0; /* typed, or the START that joined */
    }
    if (!code_entry) {
        const unsigned secs = lobby_frames / 60;
        char code[18];
        int reach = PC_PORTMAP_WORKING;
        lobbyPlayers(&view);
        if (online_is_host() && st != ONLINE_FAILED) {
            if (online_code(code, &reach)) {
                static char logged[18];
                snprintf(view.subtitle, sizeof view.subtitle, "CODE %s", code);
                if (strcmp(code, logged) != 0) {
                    memcpy(logged, code, sizeof logged);
                    pc_log_line("lobby: code %s", code);
                }
            } else {
                snprintf(view.subtitle, sizeof view.subtitle, "CODE ...");
            }
        }
        view.phase = LOBBY_PHASE_FOUND;
        switch (st) {
        case ONLINE_HOSTING:
            if (view.player_count < 2) {
                view.phase = LOBBY_PHASE_SEARCHING;
                if (!online_code(code, &reach)) {
                    snprintf(view.message, sizeof view.message, "Opening your lobby...");
                } else if (reach == PC_PORTMAP_OPEN) {
                    snprintf(view.message, sizeof view.message,
                             "Send your friends the code. Waiting for players  %u:%02u",
                             secs / 60, secs % 60);
                } else if (reach == PC_PORTMAP_CLOSED) {
                    snprintf(view.message, sizeof view.message,
                             "Same Wi-Fi only: your router said no. Forward UDP port %u",
                             (unsigned) pc_portmap_external_port());
                } else {
                    snprintf(view.message, sizeof view.message,
                             "Lobby open on this Wi-Fi only  %u:%02u", secs / 60, secs % 60);
                }
            } else {
                if (!online_can_start()) {
                    snprintf(view.message, sizeof view.message,
                             "%d players - checking everyone's connection...",
                             view.player_count);
                } else {
                    snprintf(view.message, sizeof view.message, "%d players - START: play%s%s",
                             view.player_count, last_game[0] ? "   Last game: " : "",
                             last_game);
                }
                if ((input & HSD_PAD_START) ||
                    (lobbyAutostart() > 0 && view.player_count >= lobbyAutostart()))
                {
                    lobbyStart();
                }
            }
            view.hint = "START: play   Y: copy the code   Hold B / Esc: close the lobby";
            if (input & HSD_PAD_Y) {
                if (online_copy_code()) {
                    snprintf(view.message, sizeof view.message,
                             "Code copied - send it to your friends");
                }
                sfxForward();
            }
            break;
        case ONLINE_JOINING:
            view.phase = LOBBY_PHASE_CONNECTING;
            snprintf(view.message, sizeof view.message, "Connecting to the host...  %u:%02u",
                     secs / 60, secs % 60);
            view.hint = "Hold B / Esc: stop, back to the menu";
            break;
        case ONLINE_JOINED:
            snprintf(view.message, sizeof view.message, "In the lobby - the host starts the game%s%s",
                     last_game[0] ? "   Last game: " : "", last_game);
            view.hint = "Hold B / Esc: leave, back to the menu";
            break;
        case ONLINE_STARTING:
            view.phase = LOBBY_PHASE_STARTING;
            snprintf(view.message, sizeof view.message, "Starting...");
            break;
        case ONLINE_READY:
        case ONLINE_PLAYING:
            view.phase = LOBBY_PHASE_STARTING;
            snprintf(view.message, sizeof view.message, "Starting...");
            if (!session_pending) {
                if (sessionLoad()) {
                    session_pending = true;
                    pc_log_line("lobby: entering CSS");
                    gm_801A4B60();
                } else {
                    pc_log_line("lobby: the host's game data did not fit");
                    online_leave();
                    snprintf(last_game, sizeof last_game,
                             "The host's game data did not match this version");
                }
            }
            break;
        case ONLINE_FAILED:
            view.phase = LOBBY_PHASE_ERROR;
            snprintf(view.message, sizeof view.message, "%s - START: try again",
                     online_error_text());
            view.hint = "START: try again   Hold B / Esc: back to the menu";
            if (input & HSD_PAD_START) {
                sfxForward();
                online_leave();
                lobbyOpen();
            }
            break;
        default:
            view.phase = LOBBY_PHASE_ERROR;
            snprintf(view.message, sizeof view.message, "%s",
                     last_game[0] ? last_game : "Not connected - START: try again");
            view.hint = "START: try again   Hold B / Esc: back to the menu";
            if (input & HSD_PAD_START) {
                sfxForward();
                lobbyOpen();
            }
            break;
        }
    }
    /* Hold B (or Esc, which still works while letters type) to go back to
     * the menu from any lobby screen; a tap of B deletes in code entry. */
    {
        /* Counts only once B has been up in the lobby: the B held on
         * character select to come back here is not a second request. */
        static int held;
        const bool down = (gm_GetButtonsPressed(PAD_MAX_CONTROLLERS) & HSD_PAD_B) ||
                          pc_keyboard_escape_held();
        if (lobby_frames == 1) {
            held = down ? -1 : 0;
        }
        held = !down ? 0 : held < 0 ? -1 : held + 1;
        if (held > 0 && !session_pending) {
            snprintf(view.message, sizeof view.message, "Keep holding to go back to the menu");
        }
        if (held >= LOBBY_HOLD_FRAMES) {
            held = 0;
            leave = true;
        }
    }
    mnOnlineLobby_Update(&view);
    if (strcmp(view.message, last_status) != 0) {
        memcpy(last_status, view.message, sizeof last_status);
        pc_log_line("lobby: %s", view.message);
    }
    if (leave && !session_pending) {
        sfxBack();
        online_leave();
        pc_net_peer_status_clear();
        gm_ChangeGameModeAfterCurrentScene(GM_MENU);
        gm_801A4B60();
    }
#endif
}
