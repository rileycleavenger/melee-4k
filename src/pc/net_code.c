/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lobby codes: see net_code.h. */
#include "net_code.h"

#include <ctype.h>
#include <string.h>

static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/* A 10-bit check over the 65 data bits: CRC with polynomial 0x233
 * (x^10 + x^9 + x^5 + x^4 + x + 1), which catches every single-character
 * error and every swap of two neighbouring characters in a 15-character
 * base32 string. */
static uint32_t check10(uint64_t hi, uint32_t lo_bit) {
    uint32_t crc = 0;
    for (int i = 63; i >= -1; i--) {
        const uint32_t bit = i >= 0 ? (uint32_t)((hi >> i) & 1) : (lo_bit & 1);
        const uint32_t top = ((crc >> 9) & 1) ^ bit;
        crc = (crc << 1) & 0x3FF;
        if (top) {
            crc ^= 0x233;
        }
    }
    return crc;
}

uint32_t pc_net_code_token(const uint8_t public_key[32]) {
    /* FNV-1a over the whole key, folded to 17 bits. */
    uint32_t h = 2166136261u;
    for (int i = 0; i < 32; i++) {
        h = (h ^ public_key[i]) * 16777619u;
    }
    return (h ^ (h >> 17)) & 0x1FFFF;
}

/* The 75 bits as hi (top 64) and the last 11: data = address(32) port(16)
 * token(17), then check(10). */
void pc_net_code_encode(const PcNetLobbyCode* code, char out[PC_NET_CODE_LEN + 1]) {
    const uint64_t data_hi = ((uint64_t)code->address << 32) | ((uint64_t)code->port << 16) |
                             ((code->token >> 1) & 0xFFFF); /* 64 bits */
    const uint32_t data_lo = code->token & 1;               /* the 65th */
    const uint32_t check = check10(data_hi, data_lo);
    /* 75 bits, most significant first, five per character. */
    for (int c = 0; c < PC_NET_CODE_LEN; c++) {
        uint32_t v = 0;
        for (int b = 0; b < 5; b++) {
            const int bit = c * 5 + b; /* 0 = most significant of 75 */
            uint32_t x;
            if (bit < 64) {
                x = (uint32_t)((data_hi >> (63 - bit)) & 1);
            } else if (bit == 64) {
                x = data_lo;
            } else {
                x = (check >> (74 - bit)) & 1;
            }
            v = (v << 1) | x;
        }
        out[c] = ALPHABET[v];
    }
    out[PC_NET_CODE_LEN] = '\0';
}

bool pc_net_code_normalize(const char* text, char out[PC_NET_CODE_LEN + 1]) {
    int n = 0;
    for (const char* p = text; p != NULL && *p != '\0'; p++) {
        char ch = (char)toupper((unsigned char)*p);
        if (ch == '-' || ch == ' ' || ch == '#') {
            continue;
        }
        if (ch == '0') {
            ch = 'O';
        } else if (ch == '1') {
            ch = 'I';
        } else if (ch == '8') {
            ch = 'B';
        }
        if (strchr(ALPHABET, ch) == NULL || n >= PC_NET_CODE_LEN) {
            return false;
        }
        out[n++] = ch;
    }
    out[n] = '\0';
    return n == PC_NET_CODE_LEN;
}

bool pc_net_code_decode(const char* text, PcNetLobbyCode* out) {
    char code[PC_NET_CODE_LEN + 1];
    if (!pc_net_code_normalize(text, code)) {
        return false;
    }
    uint64_t hi = 0;
    uint32_t lo = 0, check = 0;
    for (int c = 0; c < PC_NET_CODE_LEN; c++) {
        const uint32_t v = (uint32_t)(strchr(ALPHABET, code[c]) - ALPHABET);
        for (int b = 0; b < 5; b++) {
            const int bit = c * 5 + b;
            const uint32_t x = (v >> (4 - b)) & 1;
            if (bit < 64) {
                hi = (hi << 1) | x;
            } else if (bit == 64) {
                lo = x;
            } else {
                check = (check << 1) | x;
            }
        }
    }
    if (check != check10(hi, lo)) {
        return false;
    }
    out->address = (uint32_t)(hi >> 32);
    out->port = (uint16_t)(hi >> 16);
    out->token = (uint32_t)(((hi & 0xFFFF) << 1) | lo);
    return out->port != 0;
}

void pc_net_code_show(const char* code, char* out, size_t n) {
    size_t o = 0;
    if (n == 0) {
        return;
    }
    for (size_t i = 0; code[i] != '\0'; i++) {
        const size_t need = (i > 0 && i % 5 == 0) ? 2 : 1;
        if (o + need >= n) {
            break;
        }
        if (need == 2) {
            out[o++] = '-';
        }
        out[o++] = code[i];
    }
    out[o] = '\0';
}
