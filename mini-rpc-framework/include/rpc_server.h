#ifndef RPC_SERVER_H
#define RPC_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "rpc_encoding.h"
#include "rpc_transport.h"
#include "rpc_registry.h"
#include "rpc_interceptor.h"

/*
 * rpc_server.h - RPC Server Skeleton
 *
 * L1 (Definitions): Server struct, thread pool, dispatch table
 * L2 (Core Concepts): Request dispatch loop, worker thread model
 * L3 (Engineering): Thread pool with lock-free work-stealing queue
 * L4 (Standards): Little's Law: L = lambda * W (queue length = arrival_rate * wait_time)
 *     Amdahl's Law: speedup <= 1 / (S + (1-S)/N) for N workers
 * L5 (Algorithms): Round-robin dispatch, thread pool scheduling
 * L6 (Canonical Problems): Concurrent RPC server with graceful shutdown
 * L7 (Applications): Microservice backend, API gateway
 * L8 (Advanced): Lock-free MPMC work queue, NUMA-aware scheduling
 */

#define RPC_SERVER_MAX_WORKERS       16
#define RPC_SERVER_MAX_PENDING       1024
#define RPC_SERVER_WORK_QUEUE_SIZE   256
#define RPC_SERVER_MAX_SERVICES      64
#define RPC_SERVER_DEFAULT_PORT      8080
#define RPC_SERVER_SHUTDOWN_TIMEOUT  5000  /* ms */

/*
 * Little's Law (L4):
 * In a stable system, the average number of requests L in the system
 * equals the average arrival rate lambda multiplied by the average
 * time W each request spends in the system:
 *
 *   L = lambda * W
 *
 * For an RPC server with lambda = 1000 req/s and W = 10ms avg latency:
 *   L = 1000 * 0.010 = 10 requests in-flight on average.
 *
 * The work queue should be sized at L * safety_factor (e.g., 10 * 20 = 200)
 * to handle bursts and avoid dropping requests.
 */

typedef struct {
    RPCMessage request;
    int32_t    conn_idx;      /* index into transport connections */
    int64_t    arrival_time;  /* monotonic timestamp for Little's Law tracking */
    int32_t    id;
} RPCWorkItem;

/*
 * Lock-free bounded MPMC (Multi-Producer Multi-Consumer) work queue.
 * Uses Lamport's circular buffer algorithm with sequence counters
 * to avoid locks. Suitable for high-throughput dispatch.
 *
 * L8: This is a simplified version of the Disruptor pattern used in
 * LMAX and other low-latency trading systems.
 */
typedef struct {
    RPCWorkItem items[RPC_SERVER_WORK_QUEUE_SIZE];
    int32_t      head;        /* consumer index */
    int32_t      tail;        /* producer index */
    int32_t      count;       /* approximate count (non-atomic for C99) */
    bool         closed;
} RPCWorkQueue;

/*
 * Thread pool: M workers pulling from a shared work queue.
 * Each worker runs rpc_server_worker_loop().
 */
typedef struct {
    void    *thread_handle;  /* platform-specific; NULL if not running */
    int32_t  worker_id;
    bool     running;
    int64_t  tasks_processed;
    int64_t  total_latency_us;
} RPCWorker;

/*
 * Method dispatch entry: maps a method name to a handler function
 * with a hash for fast lookup.
 */
typedef struct {
    char          method_name[RPC_MAX_METHOD_NAME];
    RPCHandlerFn  handler;
    int32_t       method_hash;
    int32_t       timeout_ms;
} RPCDispatchEntry;

/*
 * Core server struct binding transport, registry, interceptors,
 * thread pool, and dispatch table.
 */
typedef struct {
    RPCTransport       transport;
    ServiceRegistry    registry;
    InterceptorChain   interceptor_chain;
    RPCDispatchEntry   dispatch_table[RPC_SERVER_MAX_SERVICES * 32];
    int32_t            dispatch_count;

    RPCWorkQueue       work_queue;
    RPCWorker          workers[RPC_SERVER_MAX_WORKERS];
    int32_t            worker_count;

    bool               running;
    bool               shutting_down;
    int32_t            listen_port;
    char               listen_addr[256];
    RPCTransportType   transport_type;

    /* Monitoring (Little's Law validation) */
    int64_t            total_requests;
    int64_t            total_completed;
    int64_t            total_errors;
    int64_t            start_time_ms;
} RPCServer;

/* --- Lifecycle ---------------------------------------------------- */

void rpc_server_init(RPCServer *srv, const char *addr, int32_t port);
void rpc_server_shutdown(RPCServer *srv);
void rpc_server_free(RPCServer *srv);

/* --- Service Registration ---------------------------------------- */

int  rpc_server_register_service(RPCServer *srv, const char *service_name,
                                  const char *method_name,
                                  RPCHandlerFn handler, int32_t timeout_ms);

int  rpc_server_unregister_service(RPCServer *srv, const char *service_name,
                                    const char *method_name);

/* --- Dispatch ----------------------------------------------------- */

int  rpc_server_dispatch(RPCServer *srv, const RPCWorkItem *item);

/*
 * Look up dispatch entry by method hash (FNV-1a).
 * Returns index or -1 if not found.
 */
int  rpc_server_lookup_method(RPCServer *srv, const char *method_name);

/* --- Thread Pool -------------------------------------------------- */

/*
 * Start N worker threads. On platforms without pthreads, N is
 * limited to 1 (single-threaded mode in the same process).
 *
 * Amdahl's Law (L4): If the serial fraction S = 0.1 (10% of work is
 * serial/sequential), maximum speedup with N workers is:
 *   speedup(N) = 1 / (0.1 + 0.9/N)
 *   speedup(4)  = 1 / (0.1 + 0.225) = 1 / 0.325 = 3.08x
 *   speedup(16) = 1 / (0.1 + 0.056) = 1 / 0.156 = 6.4x
 */
int  rpc_server_start_workers(RPCServer *srv, int32_t num_workers);
void rpc_server_stop_workers(RPCServer *srv);

/* --- Event Loop --------------------------------------------------- */

/*
 * Main server loop:
 *   1. Accept connections (non-blocking)
 *   2. Receive messages from active connections
 *   3. Decode, dispatch via interceptor chain, invoke handler
 *   4. Encode and send response
 *   5. Handle timeouts and keepalive
 *
 * Returns on shutdown signal.
 */
int  rpc_server_run(RPCServer *srv);

/*
 * Single iteration of the event loop (useful for embedded/test).
 * Returns number of requests processed.
 */
int  rpc_server_poll(RPCServer *srv);

/* --- Work Queue Operations ---------------------------------------- */

void rpc_work_queue_init(RPCWorkQueue *q);
bool rpc_work_queue_push(RPCWorkQueue *q, const RPCWorkItem *item);
bool rpc_work_queue_pop(RPCWorkQueue *q, RPCWorkItem *item);
void rpc_work_queue_close(RPCWorkQueue *q);

/* --- Worker Loop -------------------------------------------------- */

/*
 * Worker thread main function. Pulls items from work queue,
 * dispatches them, measures latency.
 * Returns number of tasks processed.
 */
int64_t rpc_server_worker_loop(RPCServer *srv, int32_t worker_id);

/* --- Statistics --------------------------------------------------- */

/*
 * Monitor server health:
 * - Throughput (req/s)
 * - Average latency
 * - Queue depth
 * - Little's Law validation: L = lambda * W
 */
void rpc_server_stats(const RPCServer *srv,
                       double *throughput_rps,
                       double *avg_latency_ms,
                       int32_t *queue_depth,
                       double *little_l_prediction);

/* --- Graceful Shutdown -------------------------------------------- */

/*
 * Graceful shutdown sequence (L6: Canonical Problem):
 * 1. Stop accepting new connections
 * 2. Drain work queue (complete in-flight requests)
 * 3. Close active connections
 * 4. Stop worker threads
 * 5. Free resources
 */
int  rpc_server_graceful_shutdown(RPCServer *srv, int32_t timeout_ms);

#endif /* RPC_SERVER_H */
