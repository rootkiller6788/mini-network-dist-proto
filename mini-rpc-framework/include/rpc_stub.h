#ifndef RPC_STUB_H
#define RPC_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"
#include "rpc_transport.h"
#include "rpc_registry.h"

#define RPC_DEFAULT_TIMEOUT_MS 3000
#define RPC_MAX_STUB_NAME      128

typedef void (*RPCAsyncCallback)(const RPCMessage *resp, int error_code, void *user_data);

typedef struct {
    char        method[RPC_MAX_METHOD_NAME];
    RPCMessage  request;
    int32_t     timeout_ms;
    bool        fire_and_forget;
} RPCCall;

typedef struct {
    char              service_name[RPC_MAX_STUB_NAME];
    char              method_name[RPC_MAX_METHOD_NAME];
    RPCTransport     *transport;
    ServiceRegistry  *registry;
    RPCConnection    *active_conn;
    int32_t           timeout_ms;
    int32_t           retries;
    int32_t           call_id_counter;
    bool              use_registry;
} RPCStub;

typedef struct {
    RPCAsyncCallback  callback;
    void             *user_data;
    int32_t           call_id;
    int32_t           timeout_ms;
    int32_t           deadline;
    bool              completed;
} RPCAsyncContext;

void rpc_stub_init(RPCStub *stub, const char *service_name,
                   RPCTransport *transport, ServiceRegistry *registry);
void rpc_stub_set_method(RPCStub *stub, const char *method_name);
void rpc_stub_set_timeout(RPCStub *stub, int32_t timeout_ms);
void rpc_stub_set_retries(RPCStub *stub, int32_t retries);

int  stub_call(RPCStub *stub, const char *method, const RPCMessage *req,
               RPCMessage *resp);

int  stub_async_call(RPCStub *stub, const char *method, const RPCMessage *req,
                     RPCAsyncCallback cb, void *user_data);

int  stub_timeout_handle(RPCStub *stub);
int  stub_reconnect(RPCStub *stub);

int32_t calc_add_i32(int32_t a, int32_t b);
int64_t calc_add_i64(int64_t a, int64_t b);

#endif
