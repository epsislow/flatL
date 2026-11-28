#include "flat_ws_server_transport.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

typedef struct ClientArgs { uint16_t port; int result; } ClientArgs;
#ifdef _WIN32
static DWORD WINAPI client_main(LPVOID opaque) {
#else
static void *client_main(void *opaque) {
#endif
    ClientArgs *args = (ClientArgs *)opaque;
    struct sockaddr_in address;
#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) { args->result = -1; return 0; }
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { args->result = -1; return NULL; }
#endif
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(args->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    args->result = connect(fd, (struct sockaddr *)&address, sizeof address) == 0 ? 0 : -1;
#ifdef _WIN32
    closesocket(fd); return 0;
#else
    close(fd); return NULL;
#endif
}

int main(void) {
    FlatWsServerListener *listener = NULL;
    uint16_t port = 0;
    intptr_t peer = (intptr_t)-1;
    char error[256] = {0};
    ClientArgs args;
#ifdef _WIN32
    HANDLE thread; WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 2;
#else
    pthread_t thread;
#endif
    int rc = 1;
    if (flat_ws_server_listener_open("127.0.0.1", 0, 8, &listener, &port,
                                     error, sizeof error) != 0 || !listener || !port) goto done;
    if (flat_ws_server_listener_accept(listener, 0, &peer, error, sizeof error) != 1) goto done;
    args.port = port; args.result = -1;
#ifdef _WIN32
    thread = CreateThread(NULL, 0, client_main, &args, 0, NULL);
    if (!thread) goto done;
#else
    if (pthread_create(&thread, NULL, client_main, &args) != 0) goto done;
#endif
    if (flat_ws_server_listener_accept(listener, 2000, &peer, error, sizeof error) != 0 || peer == (intptr_t)-1) {
#ifdef _WIN32
        WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
#else
        pthread_join(thread, NULL);
#endif
        goto done;
    }
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    if (args.result != 0) goto done;
    flat_ws_server_peer_close(peer); peer = (intptr_t)-1;
    puts("websocket-server-listener-ok"); rc = 0;
done:
    if (peer != (intptr_t)-1) flat_ws_server_peer_close(peer);
    if (listener) flat_ws_server_listener_close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    if (rc) fprintf(stderr, "websocket server listener test failed: %s\n", error);
    return rc;
}
