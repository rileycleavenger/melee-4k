/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_PRODUCT_H
#define PC_PRODUCT_H

/* The product this build is (product.env, passed by CMakeLists.txt): the
 * name players see, the release zips' and app's names, the per-product
 * files in the settings folder, and the GitHub repo whose releases are its
 * updates and online's latest version. The values below stand in only for
 * compiles outside CMake (lint, tests). */
#ifndef PC_PRODUCT_NAME
#define PC_PRODUCT_NAME "Melee 4K"
#endif
#ifndef PC_PRODUCT_FILE
#define PC_PRODUCT_FILE "Melee4K"
#endif
#ifndef PC_PRODUCT_SLUG
#define PC_PRODUCT_SLUG "melee4k"
#endif
#ifndef PC_PRODUCT_REPO
#define PC_PRODUCT_REPO "rileycleavenger/melee-4k"
#endif
#ifndef PC_PRODUCT_FIGHTER_PACKS
#define PC_PRODUCT_FIGHTER_PACKS 1
#endif

#endif
