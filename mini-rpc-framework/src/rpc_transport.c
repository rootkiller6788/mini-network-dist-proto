#include "rpc_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

static int g_socket_initialized = 0;

static int init_sockets(void) {
    if (g_socket_initialized) return 0;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    g_socket_initialized = 1;
    return 0;
}

static int set_nonblocking(SOCKET fd, bool nb) {
#ifdef _WIN32
    u_long mode = nb ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    flags = nb ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd, F_SETFL, flags);
#endif
}

void rpc_connection_init(RPCConnection *conn) {
    conn->fd = -1;
    memset(conn->address, 0, sizeof(conn->address));
    conn->port = 0;
    conn->transport_type = RPC_TRANSPORT_TCP;
    conn->connected = false;
    conn->keepalive = false;
    conn->last_active = 0;
}

void rpc_connection_close(RPCConnection *conn) {
    if (conn->fd >= 0) {
        closesocket((SOCKET)conn->fd);
        conn->fd = -1;
    }
    conn->connected = false;
    conn->keepalive = false;
}

void rpc_transport_init(RPCTransport *t) {
    t->server_socket = NULL;
    t->connection_count = 0;
    t->pool_size = 0;
    t->multiplexing = false;
    t->multiplex_id_counter = 0;
    for (int32_t i = 0; i < RPC_MAX_CONNECTIONS; i++) {
        rpc_connection_init(&t->connections[i]);
    }
    for (int32_t i = 0; i < RPC_MAX_POOL_SIZE; i++) {
        rpc_connection_init(&t->pool[i]);
    }
    init_sockets();
}

int rpc_transport_init_server(RPCTransport *t, const char *address, int32_t port,
                               RPCTransportType type) {
    (void)type;
    init_sockets();

    if (t->server_socket) {
        rpc_connection_close(t->server_socket);
        free(t->server_socket);
    }
    t->server_socket = (RPCConnection *)malloc(sizeof(RPCConnection));
    rpc_connection_init(t->server_socket);
    t->server_socket->transport_type = type;
    strncpy(t->server_socket->address, address, 255);
    t->server_socket->port = port;

    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) return -1;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listen_fd);
        return -1;
    }

    if (listen(listen_fd, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_fd);
        return -1;
    }

    t->server_socket->fd = (int32_t)listen_fd;
    t->server_socket->connected = true;
    return 0;
}

int rpc_transport_accept(RPCTransport *t) {
    if (!t->server_socket || !t->server_socket->connected) return -1;
    if (t->connection_count >= RPC_MAX_CONNECTIONS) return -1;

    SOCKET listen_fd = (SOCKET)t->server_socket->fd;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    SOCKET client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == INVALID_SOCKET) return -1;

    int32_t idx = t->connection_count;
    rpc_connection_init(&t->connections[idx]);
    t->connections[idx].fd = (int32_t)client_fd;
    t->connections[idx].transport_type = t->server_socket->transport_type;
    t->connections[idx].connected = true;
    t->connections[idx].last_active = (int32_t)time(NULL);

    char *ipstr = inet_ntoa(client_addr.sin_addr);
    strncpy(t->connections[idx].address, ipstr, 255);
    t->connections[idx].port = (int32_t)ntohs(client_addr.sin_port);

    t->connection_count++;
    return idx;
}

int rpc_transport_connect(RPCTransport *t, const char *address, int32_t port,
                           RPCTransportType type) {
    (void)type;
    init_sockets();

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);

    if (inet_pton(AF_INET, address, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(address);
        if (!he) { closesocket(fd); return -1; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(fd);
        return -1;
    }

    if (t->connection_count >= RPC_MAX_CONNECTIONS) {
        closesocket(fd);
        return -1;
    }

    int32_t idx = t->connection_count;
    rpc_connection_init(&t->connections[idx]);
    t->connections[idx].fd = (int32_t)fd;
    t->connections[idx].transport_type = type;
    strncpy(t->connections[idx].address, address, 255);
    t->connections[idx].port = port;
    t->connections[idx].connected = true;
    t->connections[idx].last_active = (int32_t)time(NULL);
    t->connection_count++;
    return idx;
}

int rpc_send_message(RPCConnection *conn, const RPCBuffer *buf) {
    if (!conn || !conn->connected || conn->fd < 0) return -1;

    SOCKET fd = (SOCKET)conn->fd;
    uint32_t len_be = htonl((uint32_t)buf->len);

    if (send(fd, (const char *)&len_be, 4, 0) != 4) return -1;

    size_t total_sent = 0;
    while (total_sent < buf->len) {
        int sent = send(fd, (const char *)(buf->data + total_sent),
                        (int)(buf->len - total_sent), 0);
        if (sent <= 0) return -1;
        total_sent += (size_t)sent;
    }

    conn->last_active = (int32_t)time(NULL);
    return 0;
}

int rpc_recv_message(RPCConnection *conn, RPCBuffer *out) {
    if (!conn || !conn->connected || conn->fd < 0) return -1;

    SOCKET fd = (SOCKET)conn->fd;
    uint32_t len_be = 0;

    int rcvd = recv(fd, (char *)&len_be, 4, MSG_WAITALL);
    if (rcvd != 4) return -1;

    uint32_t msg_len = ntohl(len_be);
    if (msg_len > 64 * 1024 * 1024) return -1;

    rpc_buffer_reserve(out, (size_t)msg_len + 1);
    out->len = 0;

    size_t total_rcvd = 0;
    while (total_rcvd < msg_len) {
        int n = recv(fd, (char *)(out->data + total_rcvd),
                     (int)(msg_len - total_rcvd), 0);
        if (n <= 0) return -1;
        total_rcvd += (size_t)n;
    }
    out->len = (size_t)msg_len;
    out->data[msg_len] = 0;

    conn->last_active = (int32_t)time(NULL);
    return 0;
}

int rpc_keepalive(RPCConnection *conn) {
    if (!conn || conn->fd < 0) return -1;

    SOCKET fd = (SOCKET)conn->fd;
    int optval = 1;
    int optlen = sizeof(optval);

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&optval, optlen);

#ifdef TCP_KEEPIDLE
    int idle = RPC_KEEPALIVE_SEC;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (const char *)&idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 5;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const char *)&intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    int cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (const char *)&cnt, sizeof(cnt));
#endif

    conn->keepalive = true;
    conn->last_active = (int32_t)time(NULL);
    return 0;
}

int rpc_keepalive_check(RPCConnection *conn) {
    if (!conn || !conn->connected || conn->fd < 0) return -1;
    int32_t now = (int32_t)time(NULL);
    if (now - conn->last_active > RPC_KEEPALIVE_SEC * 3) {
        conn->connected = false;
        return -1;
    }
    return 0;
}

int rpc_transport_pool_acquire(RPCTransport *t, const char *address, int32_t port) {
    for (int32_t i = 0; i < t->pool_size; i++) {
        if (t->pool[i].connected
            && strcmp(t->pool[i].address, address) == 0
            && t->pool[i].port == port) {
            return i;
        }
    }

    if (t->pool_size >= RPC_MAX_POOL_SIZE) {
        rpc_transport_pool_shrink(t);
    }

    int32_t idx = t->pool_size;
    rpc_connection_init(&t->pool[idx]);
    if (rpc_transport_connect(t, address, port, RPC_TRANSPORT_TCP) < 0) return -1;

    int32_t conn_idx = t->connection_count - 1;
    t->pool[idx] = t->connections[conn_idx];
    t->connection_count--;
    t->pool_size++;
    rpc_keepalive(&t->pool[idx]);
    return idx;
}

void rpc_transport_pool_release(RPCTransport *t, int32_t idx) {
    if (idx < 0 || idx >= t->pool_size) return;
    t->pool[idx].last_active = (int32_t)time(NULL);
}

void rpc_transport_pool_shrink(RPCTransport *t) {
    int32_t now = (int32_t)time(NULL);
    int32_t write = 0;
    for (int32_t i = 0; i < t->pool_size; i++) {
        if (t->pool[i].connected
            && (now - t->pool[i].last_active) < RPC_KEEPALIVE_SEC * 6) {
            if (write != i) t->pool[write] = t->pool[i];
            write++;
        } else {
            rpc_connection_close(&t->pool[i]);
        }
    }
    t->pool_size = write;
}

void rpc_transport_close(RPCTransport *t) {
    if (t->server_socket) {
        rpc_connection_close(t->server_socket);
        free(t->server_socket);
        t->server_socket = NULL;
    }
    for (int32_t i = 0; i < t->connection_count; i++) {
        rpc_connection_close(&t->connections[i]);
    }
    t->connection_count = 0;
    for (int32_t i = 0; i < t->pool_size; i++) {
        rpc_connection_close(&t->pool[i]);
    }
    t->pool_size = 0;
#ifdef _WIN32
    WSACleanup();
#endif
    g_socket_initialized = 0;
}
