#ifndef FLAT_WS_SERVER_REGISTRY_H
#define FLAT_WS_SERVER_REGISTRY_H
#include "flat_ws_server_transport.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FlatWsServerRegistry FlatWsServerRegistry;
/* Registry owns every listener/peer inserted into it. IDs are monotonic and
 * never reused during the registry lifetime. All operations are thread-safe.
 * Closing an entry rejects new operations, interrupts blocking I/O, waits for
 * in-flight operations, then releases the underlying descriptor. Concurrent
 * receives are serialized independently per peer; explicit sends and automatic
 * PONG/CLOSE replies share the peer send lock. */
FlatWsServerRegistry *flat_ws_server_registry_create(void);
int flat_ws_server_registry_listen(FlatWsServerRegistry *, const char *, uint16_t,
                                   int, uint64_t *, uint16_t *, char *, unsigned long);
int flat_ws_server_registry_accept(FlatWsServerRegistry *, uint64_t, long,
                                   uint64_t *, char *, unsigned long);
int flat_ws_server_registry_upgrade(FlatWsServerRegistry *, uint64_t, long,
                                    char *, unsigned long);
int flat_ws_server_registry_send(FlatWsServerRegistry *, uint64_t, FlatWsServerOpcode,
                                 const void *, unsigned long, char *, unsigned long);
int flat_ws_server_registry_receive(FlatWsServerRegistry *, uint64_t, long, unsigned long,
                                    FlatWsServerMessage *, char *, unsigned long);
int flat_ws_server_registry_close_peer(FlatWsServerRegistry *, uint64_t);
int flat_ws_server_registry_close_listener(FlatWsServerRegistry *, uint64_t);
/* Returns -1 if called from an operation using this registry; otherwise waits
 * for all in-flight operations and destroys all remaining resources. */
int flat_ws_server_registry_destroy(FlatWsServerRegistry *);
#ifdef __cplusplus
}
#endif
#endif
