/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_NET_PORTMAP_H
#define PC_NET_PORTMAP_H

/* Opening the Host Lobby's UDP port on the host's own router, so friends on
 * other networks can reach it with nothing but the lobby code: no server,
 * no third-party service. Tries NAT-PMP (Apple and many others), then
 * UPnP-IGD (most home routers), on a background thread; the mapping is
 * renewed while the lobby is open and removed when it closes.
 *
 * Also how the host learns its public address, which goes into the code. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PcPortmapState {
    PC_PORTMAP_OFF,
    PC_PORTMAP_WORKING, /* asking the router */
    PC_PORTMAP_OPEN,    /* mapped: reachable from the internet */
    PC_PORTMAP_CLOSED,  /* the router told us its address but would not map */
    PC_PORTMAP_SHARED,  /* the router's own address is private (carrier-grade
                         * NAT or a second router): internet joins can't reach us */
    PC_PORTMAP_NONE,    /* no router answered */
};

void pc_portmap_start(uint16_t internal_port);
void pc_portmap_stop(void);
enum PcPortmapState pc_portmap_state(void);
/* Public address (host byte order) and port; 0 when not known. */
uint32_t pc_portmap_external_address(void);
uint16_t pc_portmap_external_port(void);
/* "NAT-PMP" / "UPnP" / "", for the log and the lobby. */
const char* pc_portmap_method(void);

#ifdef __cplusplus
}
#endif
#endif
