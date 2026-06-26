#include "rpc_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void rpc_stub_init(RPCStub *stub, const char *service_name,
                    RPCTransport *transport, ServiceRegistry *registry) {
    memset(stub, 0, sizeof(RPCStub));
    strncpy(stub->service_name, service_name, RPC_MAX_STUB_NAME - 1);
    stub->transport = transport;
    stub->registry = registry;
    stub->active_conn = NULL;
    stub->timeout_ms = RPC_DEFAULT_TIMEOUT_MS;
    stub->retries = 0;
    stub->call_id_counter = 0;
    stub->use_registry = (registry != NULL);
    memset(stub->method_name, 0, sizeof(stub->method_name));
}

void rpc_stub_set_method(RPCStub *stub, const char *method_name) {
    strncpy(stub->method_name, method_name, RPC_MAX_METHOD_NAME - 1);
}

void rpc_stub_set_timeout(RPCStub *stub, int32_t timeout_ms) {
    stub->timeout_ms = timeout_ms;
}

void rpc_stub_set_retries(RPCStub *stub, int32_t retries) {
    stub->retries = retries;
}

static int resolve_connection(RPCStub *stub) {
    if (stub->use_registry && stub->registry) {
        int32_t inst_idx = registry_lb_select(stub->registry, stub->service_name);
        if (inst_idx < 0) return -1;

        ServiceInstance instances[16];
        int found = registry_discover(stub->registry, stub->service_name,
                                       instances, 16);
        if (found <= 0 || inst_idx >= found) return -1;

        int32_t conn_idx = rpc_transport_connect(stub->transport,
                                                  instances[inst_idx].host,
                                                  instances[inst_idx].port,
                                                  RPC_TRANSPORT_TCP);
        if (conn_idx < 0) return -1;
        stub->active_conn = &stub->transport->connections[conn_idx];
        return 0;
    }
    return -1;
}

int stub_call(RPCStub *stub, const char *method, const RPCMessage *req,
               RPCMessage *resp) {
    if (!stub || !method || !req || !resp) return -1;

    if (!stub->active_conn || !stub->active_conn->connected) {
        if (resolve_connection(stub) != 0) return -1;
    }

    RPCMessage send_msg;
    rpc_message_init(&send_msg);
    send_msg.id = stub->call_id_counter++;
    strncpy(send_msg.method_name, method, RPC_MAX_METHOD_NAME - 1);
    send_msg.param_count = req->param_count;
    send_msg.is_request = true;
    send_msg.return_type = req->return_type;

    for (int32_t i = 0; i < req->param_count && i < RPC_MAX_PARAMS; i++) {
        send_msg.params[i] = req->params[i];
    }

    RPCBuffer send_buf;
    rpc_buffer_init(&send_buf);
    if (rpc_encode_json(&send_msg, &send_buf) != 0) {
        rpc_buffer_free(&send_buf);
        return -1;
    }

    if (rpc_send_message(stub->active_conn, &send_buf) != 0) {
        rpc_buffer_free(&send_buf);
        stub_reconnect(stub);
        return -1;
    }
    rpc_buffer_free(&send_buf);

    RPCBuffer recv_buf;
    rpc_buffer_init(&recv_buf);
    if (rpc_recv_message(stub->active_conn, &recv_buf) != 0) {
        rpc_buffer_free(&recv_buf);
        stub_reconnect(stub);
        return -1;
    }

    rpc_message_init(resp);
    if (rpc_decode_json(&recv_buf, resp) != 0) {
        rpc_buffer_free(&recv_buf);
        return -1;
    }
    rpc_buffer_free(&recv_buf);
    return 0;
}

static RPCAsyncContext g_async_contexts[32];
static int32_t g_async_count = 0;

int stub_async_call(RPCStub *stub, const char *method, const RPCMessage *req,
                     RPCAsyncCallback cb, void *user_data) {
    if (!stub || !method || !req || !cb) return -1;
    if (g_async_count >= 32) return -1;

    if (!stub->active_conn || !stub->active_conn->connected) {
        if (resolve_connection(stub) != 0) return -1;
    }

    RPCMessage send_msg;
    rpc_message_init(&send_msg);
    send_msg.id = stub->call_id_counter++;
    strncpy(send_msg.method_name, method, RPC_MAX_METHOD_NAME - 1);
    send_msg.param_count = req->param_count;
    send_msg.is_request = true;

    for (int32_t i = 0; i < req->param_count && i < RPC_MAX_PARAMS; i++) {
        send_msg.params[i] = req->params[i];
    }

    RPCBuffer send_buf;
    rpc_buffer_init(&send_buf);
    rpc_encode_json(&send_msg, &send_buf);
    rpc_send_message(stub->active_conn, &send_buf);
    rpc_buffer_free(&send_buf);

    RPCAsyncContext *ctx = &g_async_contexts[g_async_count];
    ctx->callback = cb;
    ctx->user_data = user_data;
    ctx->call_id = send_msg.id;
    ctx->timeout_ms = stub->timeout_ms;
    ctx->deadline = (int32_t)time(NULL) * 1000 + stub->timeout_ms;
    ctx->completed = false;

    RPCBuffer recv_buf;
    rpc_buffer_init(&recv_buf);
    if (rpc_recv_message(stub->active_conn, &recv_buf) == 0) {
        RPCMessage resp;
        rpc_message_init(&resp);
        if (rpc_decode_json(&recv_buf, &resp) == 0) {
            ctx->callback(&resp, 0, ctx->user_data);
            ctx->completed = true;
        }
        rpc_message_free(&resp);
    } else {
        RPCMessage empty_resp;
        rpc_message_init(&empty_resp);
        ctx->callback(&empty_resp, -1, ctx->user_data);
        ctx->completed = true;
    }
    rpc_buffer_free(&recv_buf);

    int32_t assigned = g_async_count;
    g_async_count++;
    return assigned;
}

int stub_timeout_handle(RPCStub *stub) {
    if (!stub) return -1;
    int32_t now_ms = (int32_t)time(NULL) * 1000;
    int32_t handled = 0;

    for (int32_t i = 0; i < g_async_count; i++) {
        if (!g_async_contexts[i].completed && now_ms > g_async_contexts[i].deadline) {
            RPCMessage err_resp;
            rpc_message_init(&err_resp);
            err_resp.is_error = true;
            strncpy(err_resp.error_msg, "timeout", 255);
            g_async_contexts[i].callback(&err_resp, -2, g_async_contexts[i].user_data);
            g_async_contexts[i].completed = true;
            handled++;
        }
    }

    for (int32_t i = 0; i < g_async_count; i++) {
        if (g_async_contexts[i].completed) {
            g_async_contexts[i] = g_async_contexts[g_async_count - 1];
            g_async_count--;
            i--;
        }
    }
    return handled;
}

int stub_reconnect(RPCStub *stub) {
    if (!stub) return -1;
    if (stub->active_conn) {
        rpc_connection_close(stub->active_conn);
        stub->active_conn = NULL;
    }
    return resolve_connection(stub);
}

int32_t calc_add_i32(int32_t a, int32_t b) {
    return a + b;
}

int64_t calc_add_i64(int64_t a, int64_t b) {
    return a + b;
}
