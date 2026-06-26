#include "rpc_server.h"
#include "rpc_interceptor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * rpc_server.c - RPC Server Skeleton Implementation
 *
 * Implements the full server lifecycle:
 *   init -> register services -> start workers -> event loop -> shutdown
 *
 * L6 (Canonical Problem): Concurrent RPC server with graceful shutdown.
 * The server must handle concurrent connections, dispatch requests to
 * worker threads, and shut down without dropping in-flight requests.
 *
 * Graceful shutdown sequence (per gRPC/Finagle best practices):
 *   1. Stop accepting (close listen socket)
 *   2. Notify workers to drain queue
 *   3. Wait for in-flight requests to complete (with timeout)
 *   4. Close all connections
 *   5. Free resources
 */

#ifdef _WIN32
#include <windows.h>
#define THREAD_FN DWORD WINAPI
#define THREAD_RET DWORD
#define THREAD_CREATE(h, fn, arg) \
    (*(h) = CreateThread(NULL, 0, fn, arg, 0, NULL), *(h) != NULL ? 0 : -1)
#define THREAD_JOIN(h) WaitForSingleObject(h, INFINITE)
#define THREAD_SLEEP_MS(ms) Sleep(ms)
typedef HANDLE thread_t;
#else
#include <pthread.h>
#include <unistd.h>
#define THREAD_FN void*
#define THREAD_RET void*
#define THREAD_CREATE(h, fn, arg) pthread_create(h, NULL, fn, arg)
#define THREAD_JOIN(h) pthread_join(h, NULL)
#define THREAD_SLEEP_MS(ms) usleep((ms) * 1000)
typedef pthread_t thread_t;
#endif

/* --- Work Queue (Lock-free Single-Producer for event loop) ------ */

void rpc_work_queue_init(RPCWorkQueue *q) {
    if (!q) return;
    memset(q, 0, sizeof(RPCWorkQueue));
}

bool rpc_work_queue_push(RPCWorkQueue *q, const RPCWorkItem *item) {
    if (!q || !item || q->closed) return false;
    int32_t next = (q->tail + 1) % RPC_SERVER_WORK_QUEUE_SIZE;
    if (next == q->head) return false;  /* Full */
    memcpy(&q->items[q->tail], item, sizeof(RPCWorkItem));
    q->tail = next;
    q->count++;
    return true;
}

bool rpc_work_queue_pop(RPCWorkQueue *q, RPCWorkItem *item) {
    if (!q || !item) return false;
    if (q->head == q->tail) {
        /* Queue is empty (or potentially drained after close) */
        return false;
    }
    memcpy(item, &q->items[q->head], sizeof(RPCWorkItem));
    q->head = (q->head + 1) % RPC_SERVER_WORK_QUEUE_SIZE;
    q->count--;
    return true;
}

void rpc_work_queue_close(RPCWorkQueue *q) {
    if (!q) return;
    q->closed = true;
}

/* --- Server Lifecycle ------------------------------------------- */

void rpc_server_init(RPCServer *srv, const char *addr, int32_t port) {
    if (!srv) return;
    memset(srv, 0, sizeof(RPCServer));
    rpc_transport_init(&srv->transport);
    registry_init(&srv->registry);
    interceptor_chain_init(&srv->interceptor_chain, "ServerChain");
    rpc_work_queue_init(&srv->work_queue);
    srv->listen_port = port > 0 ? port : RPC_SERVER_DEFAULT_PORT;
    if (addr && addr[0]) {
        strncpy(srv->listen_addr, addr, 255);
        srv->listen_addr[255] = '\0';
    } else {
        strncpy(srv->listen_addr, "0.0.0.0", 255);
    }
    srv->transport_type = RPC_TRANSPORT_TCP;
    srv->running = false;
    srv->shutting_down = false;
    srv->start_time_ms = (int64_t)time(NULL) * 1000;
}

void rpc_server_shutdown(RPCServer *srv) {
    if (!srv) return;
    rpc_server_graceful_shutdown(srv, RPC_SERVER_SHUTDOWN_TIMEOUT);
}

void rpc_server_free(RPCServer *srv) {
    if (!srv) return;
    rpc_server_stop_workers(srv);
    rpc_transport_close(&srv->transport);
    memset(srv, 0, sizeof(RPCServer));
}

/* --- Service Registration --------------------------------------- */

int rpc_server_register_service(RPCServer *srv, const char *service_name,
                                 const char *method_name,
                                 RPCHandlerFn handler, int32_t timeout_ms) {
    if (!srv || !service_name || !method_name || !handler) return -1;
    if (srv->dispatch_count >= RPC_SERVER_MAX_SERVICES * 32) return -1;

    /* Register service descriptor in registry */
    ServiceDescriptor sd;
    memset(&sd, 0, sizeof(sd));
    strncpy(sd.service_name, service_name, RPC_MAX_NAME_LEN - 1);
    sd.version = 1;
    sd.method_count = 1;
    strncpy(sd.methods[0].name, method_name, RPC_MAX_NAME_LEN - 1);
    sd.methods[0].handler = handler;
    sd.methods[0].timeout_ms = timeout_ms;
    registry_register(&srv->registry, &sd);

    /* Add to dispatch table */
    RPCDispatchEntry *entry = &srv->dispatch_table[srv->dispatch_count];
    strncpy(entry->method_name, method_name, RPC_MAX_METHOD_NAME - 1);
    entry->handler = handler;
    entry->method_hash = rpc_fnv1a_hash(method_name,
        (int32_t)strlen(method_name));
    entry->timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;
    srv->dispatch_count++;
    return srv->dispatch_count - 1;
}

int rpc_server_unregister_service(RPCServer *srv, const char *service_name,
                                   const char *method_name) {
    if (!srv || !service_name || !method_name) return -1;
    /* Remove from dispatch table */
    for (int32_t i = 0; i < srv->dispatch_count; i++) {
        if (strcmp(srv->dispatch_table[i].method_name, method_name) == 0) {
            /* Shift remaining entries */
            for (int32_t j = i; j < srv->dispatch_count - 1; j++) {
                memcpy(&srv->dispatch_table[j], &srv->dispatch_table[j + 1],
                       sizeof(RPCDispatchEntry));
            }
            srv->dispatch_count--;
            registry_unregister(&srv->registry, service_name);
            return 0;
        }
    }
    return -1;
}

int rpc_server_lookup_method(RPCServer *srv, const char *method_name) {
    if (!srv || !method_name) return -1;
    int32_t hash = rpc_fnv1a_hash(method_name, (int32_t)strlen(method_name));
    for (int32_t i = 0; i < srv->dispatch_count; i++) {
        if (srv->dispatch_table[i].method_hash == hash
            && strcmp(srv->dispatch_table[i].method_name, method_name) == 0) {
            return i;
        }
    }
    return -1;
}

/* --- Dispatch --------------------------------------------------- */

int rpc_server_dispatch(RPCServer *srv, const RPCWorkItem *item) {
    if (!srv || !item) return -1;

    if (item->conn_idx < 0 || item->conn_idx >= RPC_MAX_CONNECTIONS) return -1;

    RPCConnection *conn = &srv->transport.connections[item->conn_idx];
    if (!conn->connected) return -1;

    /* Receive message from connection */
    RPCBuffer recv_buf;
    rpc_buffer_init(&recv_buf);
    if (rpc_recv_message(conn, &recv_buf) != 0) {
        rpc_buffer_free(&recv_buf);
        return -1;
    }

    RPCMessage request;
    rpc_message_init(&request);
    if (rpc_decode_json(&recv_buf, &request) != 0) {
        rpc_buffer_free(&recv_buf);
        return -1;
    }

    /* Run interceptor chain (before) */
    RPCMessage req_processed;
    rpc_message_init(&req_processed);
    int ic_ret = interceptor_before_invoke(&srv->interceptor_chain,
                                            &request, &req_processed);
    if (ic_ret != 0) {
        /* Interceptor rejected the request */
        RPCMessage err_resp;
        rpc_message_init(&err_resp);
        err_resp.id = request.id;
        err_resp.is_request = false;
        err_resp.is_error = true;
        strncpy(err_resp.error_msg, "interceptor rejected", 255);

        RPCBuffer send_buf;
        rpc_buffer_init(&send_buf);
        rpc_encode_json(&err_resp, &send_buf);
        rpc_send_message(conn, &send_buf);

        rpc_buffer_free(&send_buf);
        rpc_message_free(&req_processed);
        rpc_message_free(&request);
        rpc_buffer_free(&recv_buf);
        srv->total_errors++;
        return ic_ret;
    }

    /* Look up handler */
    int32_t method_idx = rpc_server_lookup_method(srv,
        req_processed.method_name);
    RPCMessage response;
    rpc_message_init(&response);
    response.id = req_processed.id;
    response.is_request = false;

    if (method_idx >= 0) {
        /* Invoke handler (L2: business logic invocation) */
        int handler_ret = srv->dispatch_table[method_idx].handler(
            &req_processed, &response);
        if (handler_ret != 0) {
            response.is_error = true;
            snprintf(response.error_msg, 255,
                     "handler error code %d", handler_ret);
            srv->total_errors++;
        }
    } else {
        /* Method not found */
        response.is_error = true;
        snprintf(response.error_msg, 255,
                 "method not found: %s", req_processed.method_name);
        srv->total_errors++;
    }

    /* Run interceptor chain (after) */
    RPCMessage resp_processed;
    rpc_message_init(&resp_processed);
    interceptor_after_invoke(&srv->interceptor_chain,
                              &req_processed, &response, &resp_processed);

    /* Encode and send response */
    RPCBuffer send_buf;
    rpc_buffer_init(&send_buf);
    rpc_encode_json(&resp_processed, &send_buf);
    rpc_send_message(conn, &send_buf);

    /* Cleanup */
    rpc_buffer_free(&send_buf);
    rpc_message_free(&resp_processed);
    rpc_message_free(&response);
    rpc_message_free(&req_processed);
    rpc_message_free(&request);
    rpc_buffer_free(&recv_buf);

    srv->total_completed++;
    return 0;
}

/* --- Thread Pool ------------------------------------------------ */

static THREAD_FN worker_thread_fn(void *arg) {
    RPCServer *srv = (RPCServer *)arg;
    if (!srv) { THREAD_RET ret = 0; return ret; }

    /* Find our worker slot. The worker_id is passed implicitly
     * since we need to identify which worker this is.
     * We iterate to find a matching thread_handle (simplified). */
    int32_t wid = -1;
    for (int32_t i = 0; i < srv->worker_count; i++) {
        /* We can't easily match thread_handle here in cross-platform
         * code without additional thread identification. Use a simple
         * approach: assign worker_id via a dedicated field. */
        if (srv->workers[i].running && wid < 0) {
            /* For simplified C99, use the first running worker.
             * In production, use TLS or explicit thread arg. */
            wid = i;
        }
    }
    if (wid < 0) wid = 0;

    /* Run the worker loop */
    rpc_server_worker_loop(srv, wid);

    THREAD_RET ret = 0;
    return ret;
}

int rpc_server_start_workers(RPCServer *srv, int32_t num_workers) {
    if (!srv) return -1;
    if (num_workers <= 0) num_workers = 1;
    if (num_workers > RPC_SERVER_MAX_WORKERS)
        num_workers = RPC_SERVER_MAX_WORKERS;

    srv->worker_count = num_workers;

    for (int32_t i = 0; i < num_workers; i++) {
        srv->workers[i].worker_id = i;
        srv->workers[i].running = true;
        srv->workers[i].tasks_processed = 0;
        srv->workers[i].total_latency_us = 0;

        /*
         * L4: Amdahl's Law - maximum theoretical speedup
         * For N workers with serial fraction S:
         *   speedup(N) = 1 / (S + (1-S)/N)
         *
         * With S=0.05 (5% serial work: queue operations, I/O):
         *   speedup(4)  = 1/(0.05+0.2375) = 3.48x
         *   speedup(16) = 1/(0.05+0.0594) = 9.14x
         *
         * Practically, I/O-bound RPC server hits diminishing
         * returns after ~N=8 due to context switching overhead.
         */
        int ret = THREAD_CREATE(&srv->workers[i].thread_handle,
                                 worker_thread_fn, srv);
        if (ret != 0) {
            /* Thread creation failed; mark as not running */
            srv->workers[i].running = false;
            srv->workers[i].thread_handle = NULL;
        }
    }
    return 0;
}

void rpc_server_stop_workers(RPCServer *srv) {
    if (!srv) return;
    rpc_work_queue_close(&srv->work_queue);

    for (int32_t i = 0; i < srv->worker_count; i++) {
        if (srv->workers[i].running) {
            srv->workers[i].running = false;
            if (srv->workers[i].thread_handle) {
                THREAD_JOIN(srv->workers[i].thread_handle);
                srv->workers[i].thread_handle = NULL;
            }
        }
    }
    srv->worker_count = 0;
}

int64_t rpc_server_worker_loop(RPCServer *srv, int32_t worker_id) {
    if (!srv || worker_id < 0) return 0;

    int64_t processed = 0;

    while (srv->workers[worker_id].running && !srv->shutting_down) {
        RPCWorkItem item;
        if (rpc_work_queue_pop(&srv->work_queue, &item)) {
            int64_t start_us = (int64_t)time(NULL) * 1000000;
            rpc_server_dispatch(srv, &item);
            int64_t end_us = (int64_t)time(NULL) * 1000000;
            srv->workers[worker_id].total_latency_us += (end_us - start_us);
            srv->workers[worker_id].tasks_processed++;
            processed++;
        } else if (srv->work_queue.closed) {
            /* Queue closed; exit after draining */
            break;
        } else {
            /* No work available; brief sleep to avoid busy-wait */
            THREAD_SLEEP_MS(1);
        }
    }
    return processed;
}

/* --- Event Loop ------------------------------------------------- */

int rpc_server_run(RPCServer *srv) {
    if (!srv) return -1;

    /* Start listening */
    if (rpc_transport_init_server(&srv->transport, srv->listen_addr,
                                   srv->listen_port,
                                   srv->transport_type) != 0) {
        fprintf(stderr, "[SERVER] Failed to bind to %s:%d\n",
                srv->listen_addr, srv->listen_port);
        return -1;
    }

    srv->running = true;
    printf("[SERVER] Listening on %s:%d with %d workers\n",
           srv->listen_addr, srv->listen_port, srv->worker_count);

    /* Add a default logging interceptor for observability */
    RPCInterceptor log_ic = interceptor_make_logging();
    interceptor_chain_add(&srv->interceptor_chain, &log_ic);

    /* Main event loop */
    while (srv->running && !srv->shutting_down) {
        rpc_server_poll(srv);
        /* Small sleep to prevent 100% CPU in single-threaded mode */
        THREAD_SLEEP_MS(10);
    }

    return 0;
}

int rpc_server_poll(RPCServer *srv) {
    if (!srv || !srv->running) return 0;

    int processed = 0;

    /* 1. Accept new connections */
    int32_t new_conn = rpc_transport_accept(&srv->transport);
    if (new_conn >= 0) {
        if (srv->worker_count > 0) {
            /* Multi-threaded: enqueue for worker */
            RPCWorkItem item;
            memset(&item, 0, sizeof(item));
            item.conn_idx = new_conn;
            item.arrival_time = (int64_t)time(NULL) * 1000;
            item.id = (int32_t)srv->total_requests;
            srv->total_requests++;

            if (!rpc_work_queue_push(&srv->work_queue, &item)) {
                /* Queue full; drop request
                 * L8: In production, apply back-pressure or return
                 * HTTP 503 Service Unavailable */
                fprintf(stderr, "[SERVER] Work queue full, dropping request\n");
                srv->total_errors++;
            }
        } else {
            /* Single-threaded: handle directly */
            RPCWorkItem item;
            memset(&item, 0, sizeof(item));
            item.conn_idx = new_conn;
            item.arrival_time = (int64_t)time(NULL) * 1000;
            item.id = (int32_t)srv->total_requests;
            srv->total_requests++;
            rpc_server_dispatch(srv, &item);
            processed++;
        }
    }

    /* 2. Check existing connections for incoming data
     * (in a real event loop, this would use epoll/kqueue/IOCP) */
    for (int32_t i = 0; i < srv->transport.connection_count; i++) {
        RPCConnection *conn = &srv->transport.connections[i];
        if (conn->connected) {
            /* Check keepalive */
            if (rpc_keepalive_check(conn) != 0) {
                rpc_connection_close(conn);
            }
        }
    }

    /* 3. Clean up stale connections */
    rpc_transport_pool_shrink(&srv->transport);

    return processed;
}

/* --- Graceful Shutdown (L6: Canonical Problem) ------------------ */

int rpc_server_graceful_shutdown(RPCServer *srv, int32_t timeout_ms) {
    if (!srv) return -1;

    printf("[SERVER] Initiating graceful shutdown (timeout=%dms)...\n",
           timeout_ms);

    /* Phase 1: Stop accepting new connections */
    srv->shutting_down = true;
    srv->running = false;
    rpc_work_queue_close(&srv->work_queue);

    /* Phase 2: Drain work queue (wait for in-flight requests) */
    int64_t deadline = (int64_t)time(NULL) * 1000 + timeout_ms;
    int32_t drained = 0;

    while ((int64_t)time(NULL) * 1000 < deadline) {
        /* Check if queue is empty */
        if (srv->work_queue.head == srv->work_queue.tail) {
            drained = 1;
            break;
        }
        THREAD_SLEEP_MS(10);
    }

    if (!drained) {
        fprintf(stderr, "[SERVER] Shutdown timeout: %d items still in queue\n",
                srv->work_queue.count);
    }

    /* Phase 3: Stop workers */
    rpc_server_stop_workers(srv);

    /* Phase 4: Close all connections */
    rpc_transport_close(&srv->transport);

    printf("[SERVER] Graceful shutdown complete. "
           "Requests: total=%lld completed=%lld errors=%lld\n",
           (long long)srv->total_requests,
           (long long)srv->total_completed,
           (long long)srv->total_errors);

    return 0;
}

/* --- Statistics (Little's Law Validation) ----------------------- */

void rpc_server_stats(const RPCServer *srv,
                       double *throughput_rps,
                       double *avg_latency_ms,
                       int32_t *queue_depth,
                       double *little_l_prediction) {
    if (!srv) return;

    int64_t now_ms = (int64_t)time(NULL) * 1000;
    double elapsed_s = (double)(now_ms - srv->start_time_ms) / 1000.0;
    if (elapsed_s < 0.001) elapsed_s = 0.001;

    /* Throughput = total completed / elapsed time */
    if (throughput_rps) {
        *throughput_rps = (double)srv->total_completed / elapsed_s;
    }

    /* Average latency across all workers */
    if (avg_latency_ms) {
        int64_t total_tasks = 0;
        int64_t total_latency = 0;
        for (int32_t i = 0; i < srv->worker_count; i++) {
            total_tasks += srv->workers[i].tasks_processed;
            total_latency += srv->workers[i].total_latency_us;
        }
        if (total_tasks > 0) {
            /* Convert microseconds to milliseconds */
            *avg_latency_ms = (double)total_latency
                            / (double)total_tasks / 1000.0;
        } else {
            *avg_latency_ms = 0.0;
        }
    }

    /* Current queue depth */
    if (queue_depth) {
        *queue_depth = srv->work_queue.count;
    }

    /*
     * Little's Law prediction:
     * L = lambda * W
     * where lambda = throughput (req/s), W = avg_latency (seconds)
     * Expected queue length = lambda * W
     */
    if (little_l_prediction) {
        double lambda = (throughput_rps ? *throughput_rps : 0.0);
        double w_sec = (avg_latency_ms ? *avg_latency_ms : 0.0) / 1000.0;
        *little_l_prediction = lambda * w_sec;
    }
}
