#include "fb_format.h"
#include "flat_runtime.h"
#include "flat_task.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TASK_COUNT 32
#define WORKER_COUNT 4
#define SERVER_TIMEOUT_MS 30000

typedef struct HttpServer {
    int listener;
    unsigned port;
    atomic_uint served;
    atomic_uint active;
    atomic_uint max_active;
    atomic_int failed;
} HttpServer;

typedef struct HttpConnection {
    HttpServer *server;
    int fd;
} HttpConnection;

static void pause_one_ms(void) {
    struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static int load_bytecode(const char *path, FlatBytecode *bc) {
    memset(bc, 0, sizeof(*bc));
    return flat_fb_load_file(path, bc);
}

static int send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void *http_connection_main(void *opaque) {
    HttpConnection *connection = (HttpConnection *)opaque;
    HttpServer *server = connection->server;
    int client = connection->fd;
    char request[4096];
    size_t used = 0;
    unsigned active;
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 14\r\n"
        "Connection: close\r\n\r\n"
        "worker-http-ok";

    active = atomic_fetch_add_explicit(&server->active, 1, memory_order_acq_rel) + 1;
    {
        unsigned observed = atomic_load_explicit(&server->max_active, memory_order_relaxed);
        while (active > observed &&
               !atomic_compare_exchange_weak_explicit(&server->max_active, &observed, active,
                                                      memory_order_relaxed, memory_order_relaxed)) {}
    }
    while (used + 1 < sizeof request) {
        ssize_t n = recv(client, request + used, sizeof request - used - 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) break;
    }
    /* Keep each accepted connection active long enough to prove overlap. */
    {
        struct timespec delay = {0, 50000000L};
        (void)nanosleep(&delay, NULL);
    }
    if (used == 0 || send_all(client, response, sizeof response - 1) != 0)
        atomic_store_explicit(&server->failed, 1, memory_order_release);
    (void)shutdown(client, SHUT_RDWR);
    close(client);
    atomic_fetch_sub_explicit(&server->active, 1, memory_order_acq_rel);
    atomic_fetch_add_explicit(&server->served, 1, memory_order_relaxed);
    free(connection);
    return NULL;
}

static void *http_server_main(void *opaque) {
    HttpServer *server = (HttpServer *)opaque;
    pthread_t connections[TASK_COUNT];
    unsigned created = 0;
    unsigned i;
    struct timespec start;
    (void)clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < TASK_COUNT; ++i) {
        struct pollfd pfd;
        int remaining;
        int ready;
        int client;
        struct timespec now;
        long elapsed;
        HttpConnection *connection;

        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed = (long)(now.tv_sec - start.tv_sec) * 1000L +
                  (long)(now.tv_nsec - start.tv_nsec) / 1000000L;
        remaining = SERVER_TIMEOUT_MS - (int)elapsed;
        if (remaining <= 0) {
            atomic_store_explicit(&server->failed, 1, memory_order_release);
            break;
        }
        pfd.fd = server->listener;
        pfd.events = POLLIN;
        pfd.revents = 0;
        do { ready = poll(&pfd, 1, remaining); } while (ready < 0 && errno == EINTR);
        if (ready <= 0) {
            atomic_store_explicit(&server->failed, 1, memory_order_release);
            break;
        }
        client = accept(server->listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) { --i; continue; }
            atomic_store_explicit(&server->failed, 1, memory_order_release);
            break;
        }
        connection = (HttpConnection *)malloc(sizeof *connection);
        if (!connection) {
            close(client);
            atomic_store_explicit(&server->failed, 1, memory_order_release);
            break;
        }
        connection->server = server;
        connection->fd = client;
        if (pthread_create(&connections[created], NULL, http_connection_main, connection) != 0) {
            close(client);
            free(connection);
            atomic_store_explicit(&server->failed, 1, memory_order_release);
            break;
        }
        created++;
    }
    for (i = 0; i < created; ++i)
        if (pthread_join(connections[i], NULL) != 0)
            atomic_store_explicit(&server->failed, 1, memory_order_release);
    if (created != TASK_COUNT)
        atomic_store_explicit(&server->failed, 1, memory_order_release);
    return NULL;
}

static int server_start(HttpServer *server) {
    struct sockaddr_in address;
    socklen_t address_len = sizeof address;
    int reuse = 1;
    memset(server, 0, sizeof(*server));
    server->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listener < 0) return -1;
    (void)setsockopt(server->listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server->listener, (struct sockaddr *)&address, sizeof address) != 0 ||
        listen(server->listener, TASK_COUNT) != 0 ||
        getsockname(server->listener, (struct sockaddr *)&address, &address_len) != 0) {
        close(server->listener);
        server->listener = -1;
        return -1;
    }
    server->port = (unsigned)ntohs(address.sin_port);
    atomic_init(&server->served, 0);
    atomic_init(&server->active, 0);
    atomic_init(&server->max_active, 0);
    atomic_init(&server->failed, 0);
    return 0;
}

static int wait_complete(FlatRuntime *runtime) {
    unsigned i;
    for (i = 0; i < 30000; ++i) {
        if (flat_runtime_shutdown_status(runtime) == FLAT_RUNTIME_SHUTDOWN_COMPLETE) return 0;
        pause_one_ms();
    }
    return -1;
}

int main(int argc, char **argv) {
    FlatBytecode bc;
    FlatRuntime *runtime = NULL;
    FlatTaskId tasks[TASK_COUNT];
    HttpServer server;
    pthread_t server_thread;
    int server_started = 0;
    int workers_started = 0;
    char url[128];
    unsigned i;
    int rc = 1;

    memset(&bc, 0, sizeof bc);
    memset(tasks, 0, sizeof tasks);
    memset(&server, 0, sizeof server);
    server.listener = -1;
    if (argc != 2 || load_bytecode(argv[1], &bc) != 0) return 2;
    (void)signal(SIGPIPE, SIG_IGN);
    if (server_start(&server) != 0) goto done;
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%u/worker", server.port);
    (void)unsetenv("FLAT_HTTP_MOCK_FILE");
    if (setenv("FLAT_HTTP_TEST_URL", url, 1) != 0) goto done;
    if (pthread_create(&server_thread, NULL, http_server_main, &server) != 0) goto done;
    server_started = 1;

    runtime = flat_runtime_create(NULL);
    if (!runtime) goto done;
    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_create(runtime, &bc, NULL, &tasks[i]) != 0 ||
            flat_runtime_task_start(runtime, tasks[i]) != 0) goto done;
    }
    /* Worker startup preflight must initialize libcurl before any task executes. */
    if (flat_runtime_workers_start(runtime, WORKER_COUNT, 8) != 0) goto done;
    workers_started = 1;
    if (flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_DRAIN) != 0 ||
        wait_complete(runtime) != 0) goto done;

    for (i = 0; i < TASK_COUNT; ++i)
        if (flat_runtime_task_state(runtime, tasks[i]) != FLAT_TASK_COMPLETED) goto done;
    if (pthread_join(server_thread, NULL) != 0) goto done;
    server_started = 0;
    if (atomic_load_explicit(&server.failed, memory_order_acquire) != 0 ||
        atomic_load_explicit(&server.served, memory_order_relaxed) != TASK_COUNT ||
        atomic_load_explicit(&server.max_active, memory_order_relaxed) < 2) goto done;
    if (flat_runtime_workers_join(runtime) != 0) goto done;
    for (i = 0; i < TASK_COUNT; ++i) {
        if (flat_runtime_task_destroy(runtime, tasks[i]) != 0) goto done;
        tasks[i] = 0;
    }
    if (flat_runtime_destroy(runtime) != 0) goto done;
    runtime = NULL;
    rc = 0;

done:
    if (runtime) {
        (void)flat_runtime_shutdown_request(runtime, FLAT_RUNTIME_SHUTDOWN_CANCEL);
        if (workers_started) {
            (void)wait_complete(runtime);
            if (flat_runtime_lifecycle(runtime) == FLAT_RUNTIME_STOPPED)
                (void)flat_runtime_workers_join(runtime);
        } else {
            unsigned turns;
            for (turns = 0; turns < 30000 &&
                 flat_runtime_shutdown_status(runtime) != FLAT_RUNTIME_SHUTDOWN_COMPLETE; ++turns)
                (void)flat_runtime_schedule(runtime, 1, 0, NULL);
        }
        for (i = 0; i < TASK_COUNT; ++i)
            if (tasks[i]) (void)flat_runtime_task_destroy(runtime, tasks[i]);
        (void)flat_runtime_destroy(runtime);
    }
    if (server_started) (void)pthread_join(server_thread, NULL);
    if (server.listener >= 0) close(server.listener);
    flat_fb_free(&bc);
    if (rc == 0) {
        printf("worker-http-ok workers=4 requests=32 max-inflight=%u curl-preflight=passed drain=joined\n",
               atomic_load_explicit(&server.max_active, memory_order_relaxed));
        return 0;
    }
    fprintf(stderr, "worker-http-failed served=%u max-inflight=%u server_failed=%d\n",
            atomic_load_explicit(&server.served, memory_order_relaxed),
            atomic_load_explicit(&server.max_active, memory_order_relaxed),
            atomic_load_explicit(&server.failed, memory_order_relaxed));
    return 1;
}
