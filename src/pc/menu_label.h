/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_MENU_LABEL_H
#define PC_MENU_LABEL_H

#include <sysdolphin/baselib/tobj.h>

/* A main-menu label texture for `text`, styled like Melee's own (see
 * menu_label.c). Cached; valid until 24 other labels have been requested.
 * NULL if the label font cannot be loaded. */
HSD_ImageDesc* pc_menu_label(const char* text);

#endif
