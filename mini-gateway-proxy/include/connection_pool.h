#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define CP_MAX_POOLS      16
#define CP_MAX_CONN       64
#define CP_MAX_HOST_LEN   256

typedef enum {
    CP_IDLE,
    CP_ACTIVE,
    CP_CLOSING,
    CP_ERROR
} CPConnState;

typedef struct {
    int          fd;
    char         host[CP_MAX_HOST_LEN];
    int          port;
    CPConnState  state;
    struct timespec created_at;
    struct timespec last_used;
    struct timespec idle_start;
    int          request_count;
    bool         keep_alive;
} CPConnection;

typedef enum {
    CP_EVICT_FIFO,
    CP_EVICT_LRU,
    CP_EVICT_LIFO
} CPEvictionPolicy;

typedef struct {
    CPConnection    connections[CP_MAX_CONN];
    int             num_conn;
    int             max_conn;
    int             max_idle_ms;
    int             max_lifetime_ms;
    int             max_requests_per_conn;
    CPEvictionPolicy evict_policy;
    char            host[CP_MAX_HOST_LEN];
    int             port;
    int             active_count;
    int             idle_count;
    uint64_t        total_created;
    uint64_t        total_destroyed;
    uint64_t        total_reused;
} ConnectionPool;

typedef struct {
    ConnectionPool pools[CP_MAX_POOLS];
    int            num_pools;
    int            default_max_conn;
    int            default_max_idle_ms;
    int            default_max_lifetime_ms;
    int            default_max_requests;
} PoolManager;

ConnectionPool*  cp_init(ConnectionPool *cp, const char *host, int port,
                          int max_conn);
CPConnection*    cp_acquire(ConnectionPool *cp);
int              cp_release(ConnectionPool *cp, CPConnection *conn,
                            bool keep_alive);
int              cp_evict(ConnectionPool *cp, int count);
int              cp_close_idle(ConnectionPool *cp);
int              cp_close_expired(ConnectionPool *cp);
void             cp_health_check(ConnectionPool *cp);
double           cp_utilization(const ConnectionPool *cp);
int              cp_pool_size(const ConnectionPool *cp);

PoolManager*     pm_init(void);
ConnectionPool*  pm_get_or_create_pool(PoolManager *pm, const char *host,
                                        int port);
CPConnection*    pm_borrow(PoolManager *pm, const char *host, int port);
int              pm_return(PoolManager *pm, CPConnection *conn);
void             pm_reap_idle(PoolManager *pm);
void             pm_print_stats(const PoolManager *pm);

#endif
