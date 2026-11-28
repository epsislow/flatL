#include "flat_ws_server_transport.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

typedef struct ClientArgs { uint16_t port; int result; char response[1024]; } ClientArgs;
#ifdef _WIN32
static DWORD WINAPI client_main(LPVOID opaque) {
#else
static void *client_main(void *opaque) {
#endif
    ClientArgs *a = (ClientArgs *)opaque;
    struct sockaddr_in addr;
    const char request[] = "GET /chat HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); int n;
    if (fd == INVALID_SOCKET) { a->result = -1; return 0; }
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0); ssize_t n;
    if (fd < 0) { a->result = -1; return NULL; }
#endif
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET; addr.sin_port = htons(a->port); addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) { a->result = -1; goto done; }
    if (send(fd, request, (int)strlen(request), 0) != (int)strlen(request)) { a->result = -1; goto done; }
    n = recv(fd, a->response, (int)sizeof a->response - 1, 0);
    if (n <= 0) { a->result = -1; goto done; }
    a->response[n] = 0; a->result = 0;
done:
#ifdef _WIN32
    closesocket(fd); return 0;
#else
    close(fd); return NULL;
#endif
}
int main(void) {
    FlatWsServerListener *listener = NULL; intptr_t peer = (intptr_t)-1; uint16_t port = 0;
    char error[256] = {0}; ClientArgs args; int rc = 1;
#ifdef _WIN32
    WSADATA data; HANDLE thread; if (WSAStartup(MAKEWORD(2,2), &data) != 0) return 2;
#else
    pthread_t thread;
#endif
    memset(&args, 0, sizeof args); args.result = -1;
    if (flat_ws_server_listener_open("127.0.0.1", 0, 4, &listener, &port, error, sizeof error) != 0 || !port) goto done;
    args.port = port;
#ifdef _WIN32
    thread = CreateThread(NULL, 0, client_main, &args, 0, NULL); if (!thread) goto done;
#else
    if (pthread_create(&thread, NULL, client_main, &args) != 0) goto done;
#endif
    if (flat_ws_server_listener_accept(listener, 2000, &peer, error, sizeof error) != 0) goto join;
    if (flat_ws_server_peer_accept_upgrade(peer, 2000, error, sizeof error) != 0) goto join;
join:
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    if (args.result != 0 || !strstr(args.response, "HTTP/1.1 101 Switching Protocols") ||
        !strstr(args.response, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")) goto done;
    rc = 0; puts("websocket-server-upgrade-ok");
done:
    if (peer != (intptr_t)-1) flat_ws_server_peer_close(peer);
    if (listener) flat_ws_server_listener_close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    if (rc) fprintf(stderr, "websocket server upgrade test failed: %s\n", error);
    return rc;
}
