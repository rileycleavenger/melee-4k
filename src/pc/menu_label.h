/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_MENU_LABEL_H
#define PC_MENU_LABEL_H

#include <stdbool.h>
#include <sysdolphin/baselib/tobj.h>

/* A main-menu label texture for `text`, styled like Melee's own (see
 * menu_label.c). Cached; valid until 24 other labels have been requested.
 * NULL if the label font cannot be loaded. */
HSD_ImageDesc* pc_menu_label(const char* text);

/* Character select's CUSTOM switch tile (ON or OFF), shaped like the HD
 * character icons; NULL if the label font cannot be loaded. */
HSD_ImageDesc* pc_menu_toggle_tile(bool on);
/* The same tile with a page: 0 OFF, else "page/pages" (ON for one page). */
HSD_ImageDesc* pc_menu_page_tile(int page, int pages);

#endif
