#include "rpc_server.h"
#include "rpc_interceptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * rpc_server_demo.c - RPC Server Demonstration
 *
 * Demonstrates:
 * - Server lifecycle: init -> register -> stats -> shutdown (L6)
 * - Thread pool with work queue (L3, L5)
 * - Graceful shutdown sequence (L6)
 * - Little's Law validation (L4)
 * - Interceptor chain integration (L7)
 *
 * This demo runs entirely in-process without actual network I/O,
 * demonstrating the server architecture and lifecycle management.
 */

/* ── Sample Handlers ──────────────────────────────────────────── */

static int calculator_add(const RPCMessage *req, RPCMessage *resp) {
    resp->id = req->id;
    resp->is_request = false;
    resp->param_count = 1;
    resp->params[0].type = RPC_TYPE_INT32;

    if (req->param_count >= 2
        && req->params[0].type == RPC_TYPE_INT32
        && req->params[1].type == RPC_TYPE_INT32) {
        resp->params[0].value.v_int32 =
            req->params[0].value.v_int32 + req->params[1].value.v_int32;
    } else {
        resp->params[0].value.v_int32 = 0;
        resp->is_error = true;
        strncpy(resp->error_msg, "invalid params for add", 255);
    }
    return 0;
}

static int calculator_multiply(const RPCMessage *req, RPCMessage *resp) {
    resp->id = req->id;
    resp->is_request = false;
    resp->param_count = 1;
    resp->params[0].type = RPC_TYPE_INT32;

    if (req->param_count >= 2
        && req->params[0].type == RPC_TYPE_INT32
        && req->params[1].type == RPC_TYPE_INT32) {
        resp->params[0].value.v_int32 =
            req->params[0].value.v_int32 * req->params[1].value.v_int32;
    } else {
        resp->params[0].value.v_int32 = 0;
        resp->is_error = true;
        strncpy(resp->error_msg, "invalid params for multiply", 255);
    }
    return 0;
}

static int user_get_info(const RPCMessage *req, RPCMessage *resp) {
    (void)req;
    resp->id = req->id;
    resp->is_request = false;
    resp->param_count = 2;

    resp->params[0].type = RPC_TYPE_STRING;
    resp->params[0].value.v_string = strdup("John Doe");

    resp->params[1].type = RPC_TYPE_INT32;
    resp->params[1].value.v_int32 = 42;
    return 0;
}

static int health_check(const RPCMessage *req, RPCMessage *resp) {
    (void)req;
    resp->id = req->id;
    resp->is_request = false;
    resp->param_count = 1;
    resp->params[0].type = RPC_TYPE_STRING;
    resp->params[0].value.v_string = strdup("OK");
    return 0;
}

/* ── Main Demo ────────────────────────────────────────────────── */

int main(void) {
    printf("=== RPC Server Architecture Demo ===\n\n");

    /* ── Phase 1: Server Initialization ────────────────────────── */
    printf("[Phase 1] Server Initialization\n");

    RPCServer srv;
    rpc_server_init(&srv, "127.0.0.1", 9090);
    printf("  Server initialized: addr=%s port=%d\n",
           srv.listen_addr, srv.listen_port);
    printf("  Work queue capacity: %d\n", RPC_SERVER_WORK_QUEUE_SIZE);
    printf("  Max workers: %d\n\n", RPC_SERVER_MAX_WORKERS);

    /* ── Phase 2: Service Registration ─────────────────────────── */
    printf("[Phase 2] Service Registration\n");

    /* Register Calculator methods */
    int idx1 = rpc_server_register_service(&srv, "Calculator",
                                            "add", calculator_add, 1000);
    int idx2 = rpc_server_register_service(&srv, "Calculator",
                                            "multiply", calculator_multiply, 1000);
    int idx3 = rpc_server_register_service(&srv, "UserService",
                                            "getInfo", user_get_info, 500);
    int idx4 = rpc_server_register_service(&srv, "HealthService",
                                            "check", health_check, 2000);

    printf("  Registered %d handlers:\n", srv.dispatch_count);
    for (int i = 0; i < srv.dispatch_count; i++) {
        printf("    [%d] %s (hash=0x%08X, timeout=%dms)\n",
               i,
               srv.dispatch_table[i].method_name,
               srv.dispatch_table[i].method_hash,
               srv.dispatch_table[i].timeout_ms);
    }
    (void)idx1; (void)idx2; (void)idx3; (void)idx4;
    printf("\n");

    /* ── Phase 3: Method Lookup ────────────────────────────────── */
    printf("[Phase 3] Method Dispatch Lookup\n");

    const char *test_methods[] = {"add", "multiply", "getInfo",
                                   "check", "nonexistent"};
    for (int i = 0; i < 5; i++) {
        int lookup = rpc_server_lookup_method(&srv, test_methods[i]);
        printf("  Lookup \"%s\": ", test_methods[i]);
        if (lookup >= 0) {
            printf("FOUND at index %d (handler=%p)\n",
                   lookup, (void *)(uintptr_t)srv.dispatch_table[lookup].handler);
        } else {
            printf("NOT FOUND\n");
        }
    }
    printf("\n");

    /* ── Phase 4: Interceptor Chain ────────────────────────────── */
    printf("[Phase 4] Interceptor Chain Setup\n");

    RPCInterceptor log_ic  = interceptor_make_logging();
    RPCInterceptor auth_ic = interceptor_make_auth("demo-api-key-2024");
    RPCInterceptor metrics_ic = interceptor_make_metrics();
    RPCInterceptor tracing_ic = interceptor_make_tracing("demo-trace-001");

    interceptor_chain_add(&srv.interceptor_chain, &auth_ic);
    interceptor_chain_add(&srv.interceptor_chain, &tracing_ic);
    interceptor_chain_add(&srv.interceptor_chain, &metrics_ic);
    interceptor_chain_add(&srv.interceptor_chain, &log_ic);

    printf("  Chain \"%s\": %d interceptors\n",
           srv.interceptor_chain.name, srv.interceptor_chain.count);
    for (int i = 0; i < srv.interceptor_chain.count; i++) {
        printf("    [%d] %s (priority=%d, enabled=%s)\n",
               i,
               srv.interceptor_chain.interceptors[i].name,
               srv.interceptor_chain.interceptors[i].priority,
               srv.interceptor_chain.interceptors[i].enabled ? "yes" : "no");
    }
    printf("\n");

    /* ── Phase 5: Thread Pool and Work Queue ───────────────────── */
    printf("[Phase 5] Thread Pool & Work Queue\n");

    /*
     * Amdahl's Law (L4) demonstration:
     * For an RPC server with serial fraction S=0.15 (15% serial:
     * queue operations, interceptor chain, I/O), and N workers:
     *   speedup(1) = 1.00x  (baseline)
     *   speedup(2) = 1.74x
     *   speedup(4) = 2.76x
     *   speedup(8) = 3.90x
     */
    double S = 0.15;
    printf("  Amdahl's Law prediction (serial fraction S=%.2f):\n", S);
    for (int n = 1; n <= 8; n *= 2) {
        double speedup = 1.0 / (S + (1.0 - S) / (double)n);
        printf("    N=%d workers -> speedup = %.2fx\n", n, speedup);
    }
    printf("\n");

    /* Start 2 workers (in-process, for demo) */
    int workers = 2;
    printf("  Starting %d worker threads (in-process mode)...\n", workers);
    rpc_server_start_workers(&srv, workers);
    printf("  Workers started: %d\n\n", srv.worker_count);

    /* Simulate queue operations (no real network) */
    printf("  Simulating work queue with 5 requests...\n");
    for (int i = 0; i < 5; i++) {
        RPCWorkItem item;
        memset(&item, 0, sizeof(item));
        item.id = i;
        item.conn_idx = i % 10;
        item.arrival_time = (int64_t)1000;
        bool pushed = rpc_work_queue_push(&srv.work_queue, &item);
        printf("    Request #%d: %s\n", i, pushed ? "enqueued" : "DROPPED");
    }
    printf("  Queue depth after push: %d\n\n", srv.work_queue.count);

    /* ── Phase 6: Server Statistics ─────────────────────────────── */
    printf("[Phase 6] Server Statistics (Little's Law)\n");

    double throughput, avg_latency, little_l;
    int32_t qdepth;
    rpc_server_stats(&srv, &throughput, &avg_latency,
                      &qdepth, &little_l);

    printf("  Throughput:       %.2f req/s\n", throughput);
    printf("  Avg latency:      %.2f ms\n", avg_latency);
    printf("  Queue depth:      %d\n", qdepth);

    /*
     * Little's Law (L4): L = lambda * W
     *   L = expected number of requests in system
     *   lambda = arrival rate (throughput)
     *   W = average time in system (latency)
     *
     * For lambda=100 req/s and W=10ms:
     *   L = 100 * 0.010 = 1 request in system on average.
     *
     * The queue should be sized at L * safety_factor to handle bursts.
     */
    double lambda = 100.0;   /* hypothetical: 100 req/s */
    double W_sec = 0.010;    /* hypothetical: 10ms */
    double L_little = lambda * W_sec;
    printf("  Little's Law (lambda=%.0f rps, W=%.0fms): L = %.2f requests\n",
           lambda, W_sec * 1000.0, L_little);
    printf("  Queue capacity: %d (safety factor: %.0fx)\n\n",
           RPC_SERVER_WORK_QUEUE_SIZE,
           (double)RPC_SERVER_WORK_QUEUE_SIZE / L_little);

    /* ── Phase 7: Graceful Shutdown ─────────────────────────────── */
    printf("[Phase 7] Graceful Shutdown\n");

    int shutdown_ret = rpc_server_graceful_shutdown(&srv, 3000);
    printf("  Shutdown result: %d (0=success)\n", shutdown_ret);
    printf("  Total requests:  %lld\n",
           (long long)srv.total_requests);
    printf("  Total completed: %lld\n",
           (long long)srv.total_completed);
    printf("  Total errors:    %lld\n\n",
           (long long)srv.total_errors);

    /* Final cleanup */
    rpc_server_free(&srv);
    printf("  Server freed. All resources released.\n\n");

    /* ── Summary ────────────────────────────────────────────────── */
    printf("=== Server Architecture Demo Complete ===\n");
    printf("Covered:\n");
    printf("  L1: Server struct, work queue, dispatch table\n");
    printf("  L2: Request dispatch loop, handler invocation\n");
    printf("  L3: Thread pool with work queue\n");
    printf("  L4: Amdahl's Law (speedup), Little's Law (queue sizing)\n");
    printf("  L5: Method dispatch via FNV-1a hash lookup\n");
    printf("  L6: Graceful shutdown sequence\n");
    printf("  L7: Interceptor chain integration\n");
    printf("  L8: Thread pool scheduling, back-pressure concept\n");

    return 0;
}
