#include "flat_ws_transport.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    FlatWsConnection *connection = NULL;
    FlatWsMessage message;
    char error[256] = {0};
    int rc = 1;
    memset(&message, 0, sizeof message);

    if (flat_ws_prepare() != 0) goto done;
    if (flat_ws_connect("http://127.0.0.1/", 100, &connection, error, sizeof error) == 0 ||
        connection != NULL || error[0] == '\0') goto done;
    error[0] = '\0';
    if (flat_ws_connect("ws://127.0.0.1:1/", 150, &connection, error, sizeof error) == 0 ||
        connection != NULL || error[0] == '\0') goto done;
    if (flat_ws_send(NULL, FLAT_WS_TEXT, "x", 1, 100, error, sizeof error) == 0) goto done;
    if (flat_ws_receive(NULL, 100, 1024, &message, error, sizeof error) == 0) goto done;
    flat_ws_message_free(&message);
    puts("websocket-transport-validation-ok");
    rc = 0;

done:
    if (connection) flat_ws_close(connection, 100);
    flat_ws_message_free(&message);
    if (rc) fprintf(stderr, "websocket transport validation failed: %s\n", error);
    return rc;
}
