#ifndef FLAT_WS_SERVER_FRAMES_INTERNAL_H
#define FLAT_WS_SERVER_FRAMES_INTERNAL_H
#include "flat_ws_server_transport.h"
#include "flat_sync.h"
int flat_ws_server_peer_receive_internal(intptr_t peer, long timeout_ms, unsigned long max_message_bytes, FlatWsServerMessage *out_message, char *error, unsigned long error_capacity, FlatMutex *send_mutex);
#endif
