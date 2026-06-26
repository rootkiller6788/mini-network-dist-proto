#ifndef RPC_TRANSPORT_H
#define RPC_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"

#define RPC_MAX_CONNECTIONS  64
#define RPC_MAX_POOL_SIZE    16
#define RPC_DEFAULT_PORT     8080
#define RPC_RECV_TIMEOUT_MS  5000
#define RPC_KEEPALIVE_SEC    30

typedef enum {
    RPC_TRANSPORT_TCP         = 0,
    RPC_TRANSPORT_HTTP        = 1,
    RPC_TRANSPORT_UNIX_SOCKET = 2
} RPCTransportType;

typedef struct {
    int32_t          fd;
    char             address[256];
    int32_t          port;
    RPCTransportType transport_type;
    bool             connected;
    bool             keepalive;
    int32_t          last_active;
} RPCConnection;

typedef struct {
    RPCConnection  *server_socket;
    RPCConnection   connections[RPC_MAX_CONNECTIONS];
    int32_t         connection_count;
    RPCConnection   pool[RPC_MAX_POOL_SIZE];
    int32_t         pool_size;
    bool            multiplexing;
    int32_t         multiplex_id_counter;
} RPCTransport;

void rpc_connection_init(RPCConnection *conn);
void rpc_connection_close(RPCConnection *conn);

void rpc_transport_init(RPCTransport *t);

int  rpc_transport_init_server(RPCTransport *t, const char *address, int32_t port,
                               RPCTransportType type);
int  rpc_transport_accept(RPCTransport *t);
int  rpc_transport_connect(RPCTransport *t, const char *address, int32_t port,
                           RPCTransportType type);

int  rpc_send_message(RPCConnection *conn, const RPCBuffer *buf);
int  rpc_recv_message(RPCConnection *conn, RPCBuffer *out);

int  rpc_keepalive(RPCConnection *conn);
int  rpc_keepalive_check(RPCConnection *conn);

int  rpc_transport_pool_acquire(RPCTransport *t, const char *address, int32_t port);
void rpc_transport_pool_release(RPCTransport *t, int32_t idx);
void rpc_transport_pool_shrink(RPCTransport *t);

void rpc_transport_close(RPCTransport *t);

#endif
