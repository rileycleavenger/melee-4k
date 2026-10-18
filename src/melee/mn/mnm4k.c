#include "mnm4k.h"

#include "forward.h"
#include "inlines.h"
#include "mnmain.h"
#include "pc/product.h"
#include "types.h"
#include <melee/lb/lbaudio_ax.h>
#include <sysdolphin/baselib/gobj.h>

#include <stdio.h>

/* Options > Melee 4K. Up/Down picks a setting, Left/Right or A changes it,
 * B returns to Options. The values are the port's preferences
 * (src/pc/launcher.cpp), applied and saved on every change. */

int pc_m4k_rows(void);
void pc_m4k_label(int row, char* out, int size);
const char* pc_m4k_description(int row);
void pc_m4k_step(int row, int dir);

#define M4K_MAX_ROWS 10 /* option slots, mn_803EAE68 */

const char* mnM4K_Label(MenuKind kind, int selection)
{
    static char labels[M4K_MAX_ROWS][48];

    if (kind == MENU_KIND_SETTINGS && selection == SEL_SETTINGS_M4K) {
        return PC_PRODUCT_NAME;
    }
    if (kind == MENU_KIND_M4K && selection >= 0 && selection < pc_m4k_rows() &&
        selection < M4K_MAX_ROWS)
    {
        pc_m4k_label(selection, labels[selection], sizeof labels[selection]);
        return labels[selection];
    }
    return NULL;
}

const char* mnM4K_Description(MenuKind kind, int selection)
{
    if (kind == MENU_KIND_SETTINGS && selection == SEL_SETTINGS_M4K) {
        return "Screen, resolution, frame rate, textures.";
    }
    if (kind == MENU_KIND_M4K && selection >= 0 && selection < pc_m4k_rows()) {
        return pc_m4k_description(selection);
    }
    return NULL;
}

/// @brief Melee 4K page think, after mn_8022D104 (Options)
void mnM4K_Think(HSD_GObj* gp)
{
    u32 buttons = mn_80229624(4);
    int count = pc_m4k_rows();
    int row = mn_804A04F0.hovered_selection;

    mn_804A04F0.buttons = buttons;
    if (buttons & (MenuInput_Confirm | MenuInput_Right)) {
        sfxForward();
        pc_m4k_step(row, 1);
    } else if (buttons & MenuInput_Left) {
        sfxForward();
        pc_m4k_step(row, -1);
    } else if (buttons & MenuInput_Back) {
        sfxBack();
        mn_804A04F0.entering_menu = 0;
        mn_80229894(MENU_KIND_SETTINGS, SEL_SETTINGS_M4K, 3);
    } else if (buttons & MenuInput_Up) {
        sfxMove();
        mn_804A04F0.hovered_selection = (row + count - 1) % count;
    } else if (buttons & MenuInput_Down) {
        sfxMove();
        mn_804A04F0.hovered_selection = (row + 1) % count;
    }
}
