/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lobby codes (src/pc/net_code.c): round trip, the forms a player types,
 * and that every single-character typo and every swap of two neighbouring
 * characters is rejected rather than called. */
#undef NDEBUG
#include "pc/net_code.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int main(void) {
    srand(1);
    long typos = 0;
    for (int t = 0; t < 4000; t++) {
        PcNetLobbyCode c = {(uint32_t)rand() * 2654435761u, (uint16_t)(1 + rand() % 65535),
                           (uint32_t)rand() & 0x1FFFF},
                       d;
        char s[PC_NET_CODE_LEN + 1];
        pc_net_code_encode(&c, s);
        assert(pc_net_code_decode(s, &d));
        assert(d.address == c.address && d.port == c.port && d.token == c.token);
        for (int i = 0; i < PC_NET_CODE_LEN; i++) {
            for (int k = 0; k < 32; k++) {
                char m[PC_NET_CODE_LEN + 1];
                if (ALPHABET[k] == s[i]) {
                    continue;
                }
                strcpy(m, s);
                m[i] = ALPHABET[k];
                assert(!pc_net_code_decode(m, &d));
                typos++;
            }
            if (i + 1 < PC_NET_CODE_LEN && s[i] != s[i + 1]) {
                char m[PC_NET_CODE_LEN + 1];
                strcpy(m, s);
                m[i] = s[i + 1];
                m[i + 1] = s[i];
                assert(!pc_net_code_decode(m, &d));
            }
        }
    }
    /* Typed forms: dashes, spaces, lower case, and 0/1/8 for O/I/B. */
    PcNetLobbyCode c = {0x60FF2662u, 41000, 0x1ABCD}, d;
    char s[PC_NET_CODE_LEN + 1], shown[PC_NET_CODE_SHOWN_LEN + 1], typed[32];
    pc_net_code_encode(&c, s);
    pc_net_code_show(s, shown, sizeof shown);
    assert(strlen(shown) == PC_NET_CODE_SHOWN_LEN && shown[5] == '-' && shown[11] == '-');
    snprintf(typed, sizeof typed, " %s ", shown);
    for (char* p = typed; *p; p++) {
        *p = (char)(*p == 'O' ? '0' : *p == 'I' ? '1' : *p == 'B' ? '8' : *p | 0x20);
    }
    assert(pc_net_code_decode(typed, &d) && d.address == c.address && d.port == c.port);
    assert(!pc_net_code_decode("ABCDE-FGHIJ", &d)); /* too short */
    printf("PASS: lobby codes (%ld typos rejected)\n", typos);
    return 0;
}
