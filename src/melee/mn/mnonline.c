#include "mnonline.h"

#include "forward.h"
#include "inlines.h"
#include "mnm4k.h"
#include "mnmain.h"
#include "types.h"
#include <melee/gm/gmonlinemode.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/types.h>
#include <melee/lb/lbaudio_ax.h>
#include <sysdolphin/baselib/gobj.h>

/* VS Mode > Online. Each entry selects its GM_ONLINE lobby mode. */

/* Two entries, both Direct Connect (gmonlinemode.c): Host shows your code,
 * Join types a friend's. Works over the internet and on one network. LAN,
 * Ranked, Unranked and Profile are still in gmonlinemode.c, unreachable. */
static const char* const online_labels[] = {
    "Host Lobby",
    "Join Lobby",
};

/* <= 44 chars: that is what fits the bottom bar at mnmain.c's font size */
static const char* const online_descriptions[] = {
    "Open a lobby and share your code.",
    "Type a friend's code to join their lobby.",
};

static const char* notice;

const char* mnOnline_Label(MenuKind kind, int selection)
{
    const char* m4k = mnM4K_Label(kind, selection);
    if (m4k != NULL) {
        return m4k; /* Options > Melee 4K shares this text path */
    }
    if (kind == MENU_KIND_VS && selection == SEL_VS_ONLINE) {
        return "Online";
    }
    if (kind == MENU_KIND_ONLINE && selection >= 0 &&
        selection < (int) ARRAY_SIZE(online_labels))
    {
        return online_labels[selection];
    }
    return NULL;
}

const char* mnOnline_Description(MenuKind kind, int selection)
{
    const char* m4k = mnM4K_Description(kind, selection);
    if (m4k != NULL) {
        return m4k;
    }
    if (kind == MENU_KIND_VS && selection == SEL_VS_ONLINE) {
        return "Play a friend online, on Mac or PC.";
    }
    if (kind == MENU_KIND_ONLINE && selection >= 0 &&
        selection < (int) ARRAY_SIZE(online_descriptions))
    {
        return online_descriptions[selection];
    }
    return NULL;
}

const char* mnOnline_TakeNotice(void)
{
    const char* s = notice;
    notice = NULL;
    return s;
}

static void enterOnline(OnlineKind kind)
{
    MenuExitData* data = gm_GetCurrentSceneExitData();
    sfxForward();
    gmOnline_SetKind(kind);
    data->pending_mode = GM_ONLINE;
    gm_801A4B60();
}

/// @brief Online menu think, after mn_8022D594
void mnOnline_Think(HSD_GObj* gp)
{
    u32 buttons = mn_80229624(4);
    int count = ARRAY_SIZE(online_labels);

    mn_804A04F0.buttons = buttons;
    if (buttons & MenuInput_Confirm) {
        gmOnline_SetDirectRole(mn_804A04F0.hovered_selection == 0 ? ONLINE_DIRECT_HOST
                                                                  : ONLINE_DIRECT_JOIN);
        enterOnline(ONLINE_KIND_DIRECT);
    } else if (buttons & MenuInput_Back) {
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_80229894(MENU_KIND_VS, SEL_VS_ONLINE, 3);
    } else if (buttons & MenuInput_Up) {
        sfxMove();
        mn_804A04F0.hovered_selection =
            (mn_804A04F0.hovered_selection + count - 1) % count;
    } else if (buttons & MenuInput_Down) {
        sfxMove();
        mn_804A04F0.hovered_selection =
            (mn_804A04F0.hovered_selection + 1) % count;
    }
}
