/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_NET_CODE_H
#define PC_NET_CODE_H

/* Lobby codes (Host Lobby / Join Lobby). A code is where the host can be
 * reached plus a check that it is the right host, typed by the friend:
 *
 *   75 bits = public IPv4 (32) + UDP port (16) + key token (17) + check (10)
 *
 * written as 15 characters of the on-screen keyboard's alphabet (RFC 4648
 * base32, A-Z 2-7) in groups of five: "K3XQ2-M7ABD-RT5WY". The check catches
 * a mistyped or misread character before anything is sent. An address of 0
 * means the host's router did not tell us its public address: the code then
 * works only on the host's own network, where the host is found by its code
 * over mDNS (net_lan.c). The key token is 17 bits of the host's public key,
 * so a stranger now at that address is not taken for the host. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_NET_CODE_LEN 15       /* characters, without the dashes */
#define PC_NET_CODE_SHOWN_LEN 17 /* "XXXXX-XXXXX-XXXXX" */

typedef struct PcNetLobbyCode {
    uint32_t address; /* host byte order; 0 = same network only */
    uint16_t port;
    uint32_t token; /* 17 bits */
} PcNetLobbyCode;

/* The token for a public key (the low 17 bits of its first three bytes, mixed). */
uint32_t pc_net_code_token(const uint8_t public_key[32]);

/* `out` gets PC_NET_CODE_LEN characters and a terminator. */
void pc_net_code_encode(const PcNetLobbyCode* code, char out[PC_NET_CODE_LEN + 1]);

/* Accepts the code with or without dashes and spaces, in either case, and
 * reads 0, 1 and 8 as O, I and B (characters the alphabet does not have).
 * False when it is not PC_NET_CODE_LEN characters of the alphabet or the
 * check fails. */
bool pc_net_code_decode(const char* text, PcNetLobbyCode* out);

/* The canonical form of a typed code (PC_NET_CODE_LEN characters), or false. */
bool pc_net_code_normalize(const char* text, char out[PC_NET_CODE_LEN + 1]);

/* "K3XQ2M7ABDRT5WY" -> "K3XQ2-M7ABD-RT5WY"; a partial code is grouped as far
 * as it goes. */
void pc_net_code_show(const char* code, char* out, size_t n);

#ifdef __cplusplus
}
#endif
#endif
