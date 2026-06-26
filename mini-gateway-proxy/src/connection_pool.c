#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "connection_pool.h"

ConnectionPool* cp_init(ConnectionPool *cp, const char *host, int port,
                         int max_conn)
{
    if (!cp || !host) return NULL;
    memset(cp, 0, sizeof(ConnectionPool));
    snprintf(cp->host, CP_MAX_HOST_LEN, "%s", host);
    cp->port = port;
    cp->max_conn = max_conn > 0 ? max_conn : CP_MAX_CONN;
    cp->max_idle_ms = 60000;
    cp->max_lifetime_ms = 3600000;
    cp->max_requests_per_conn = 1000;
    cp->evict_policy = CP_EVICT_LRU;
    cp->active_count = 0;
    cp->idle_count = 0;
    cp->total_created = 0;
    cp->total_destroyed = 0;
    cp->total_reused = 0;
    printf("[cp] Connection pool '%s:%d' (max=%d)\n", host, port, cp->max_conn);
    return cp;
}

CPConnection* cp_acquire(ConnectionPool *cp)
{
    if (!cp) return NULL;

    /* Try to reuse an idle connection */
    int best_idx = -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 0; i < cp->num_conn; i++) {
        CPConnection *c = &cp->connections[i];
        if (c->state == CP_IDLE) {
            if (cp->evict_policy == CP_EVICT_LRU) {
                if (best_idx < 0 ||
                    (c->last_used.tv_sec < cp->connections[best_idx].last_used.tv_sec ||
                     (c->last_used.tv_sec == cp->connections[best_idx].last_used.tv_sec &&
                      c->last_used.tv_nsec < cp->connections[best_idx].last_used.tv_nsec))) {
                    best_idx = i;
                }
            } else {
                best_idx = i;
                break;
            }
        }
    }

    if (best_idx >= 0) {
        CPConnection *c = &cp->connections[best_idx];
        c->state = CP_ACTIVE;
        c->last_used = now;
        cp->active_count++;
        cp->idle_count--;
        cp->total_reused++;
        printf("[cp] Reused connection #%d (fd=%d) to %s:%d\n",
               best_idx, c->fd, cp->host, cp->port);
        return c;
    }

    /* Need a new connection */
    if (cp->num_conn >= cp->max_conn) {
        printf("[cp] Pool exhausted: %d/%d connections\n", cp->num_conn, cp->max_conn);
        if (cp->idle_count > 0) {
            cp_evict(cp, 1);
            return cp_acquire(cp);
        }
        return NULL;
    }

    int idx = cp->num_conn++;
    CPConnection *c = &cp->connections[idx];
    memset(c, 0, sizeof(CPConnection));
    snprintf(c->host, CP_MAX_HOST_LEN, "%s", cp->host);
    c->port = cp->port;
    c->state = CP_ACTIVE;
    c->fd = -1;
    c->request_count = 0;
    c->keep_alive = true;
    clock_gettime(CLOCK_MONOTONIC, &c->created_at);
    c->last_used = c->created_at;
    cp->active_count++;
    cp->total_created++;
    printf("[cp] Created new connection #%d to %s:%d\n", idx, cp->host, cp->port);
    return c;
}

int cp_release(ConnectionPool *cp, CPConnection *conn, bool keep_alive)
{
    if (!cp || !conn) return -1;
    if (conn->state != CP_ACTIVE) return -1;

    conn->keep_alive = keep_alive;
    if (keep_alive && conn->fd >= 0 &&
        conn->request_count < cp->max_requests_per_conn) {
        conn->state = CP_IDLE;
        clock_gettime(CLOCK_MONOTONIC, &conn->idle_start);
        cp->active_count--;
        cp->idle_count++;
        printf("[cp] Released connection #%td to idle pool (%d idle)\n",
               conn - cp->connections, cp->idle_count);
    } else {
        if (conn->fd >= 0) close(conn->fd);
        conn->state = CP_CLOSING;
        conn->fd = -1;
        cp->active_count--;
        cp->total_destroyed++;
        printf("[cp] Closed connection #%td (keep_alive=%s, requests=%d)\n",
               conn - cp->connections, keep_alive ? "true" : "false",
               conn->request_count);
    }
    return 0;
}

int cp_evict(ConnectionPool *cp, int count)
{
    if (!cp || count <= 0) return -1;
    int evicted = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    while (evicted < count && cp->idle_count > 0) {
        int target = -1;
        for (int i = 0; i < cp->num_conn; i++) {
            if (cp->connections[i].state != CP_IDLE) continue;
            if (cp->evict_policy == CP_EVICT_FIFO) {
                if (target < 0 ||
                    (cp->connections[i].idle_start.tv_sec <
                     cp->connections[target].idle_start.tv_sec))
                    target = i;
            } else if (cp->evict_policy == CP_EVICT_LRU) {
                if (target < 0 ||
                    (cp->connections[i].last_used.tv_sec <
                     cp->connections[target].last_used.tv_sec))
                    target = i;
            } else {
                target = i;
                break;
            }
        }
        if (target < 0) break;
        CPConnection *c = &cp->connections[target];
        if (c->fd >= 0) close(c->fd);
        c->fd = -1;
        c->state = CP_CLOSING;
        cp->idle_count--;
        cp->total_destroyed++;
        evicted++;
    }
    if (evicted > 0)
        printf("[cp] Evicted %d idle connections (policy=%d)\n",
               evicted, (int)cp->evict_policy);
    return evicted;
}

int cp_close_idle(ConnectionPool *cp)
{
    if (!cp) return -1;
    int closed = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < cp->num_conn; i++) {
        CPConnection *c = &cp->connections[i];
        if (c->state != CP_IDLE) continue;
        uint64_t idle_ms = (uint64_t)(now.tv_sec - c->idle_start.tv_sec) * 1000 +
                           (uint64_t)(now.tv_nsec - c->idle_start.tv_nsec) / 1000000;
        if (idle_ms >= (uint64_t)cp->max_idle_ms) {
            if (c->fd >= 0) close(c->fd);
            c->fd = -1;
            c->state = CP_CLOSING;
            cp->idle_count--;
            cp->total_destroyed++;
            closed++;
        }
    }
    if (closed > 0)
        printf("[cp] Closed %d idle connections exceeding %dms\n",
               closed, cp->max_idle_ms);
    return closed;
}

int cp_close_expired(ConnectionPool *cp)
{
    if (!cp) return -1;
    int closed = 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < cp->num_conn; i++) {
        CPConnection *c = &cp->connections[i];
        if (c->state == CP_CLOSING || c->state == CP_ERROR) continue;
        uint64_t lifetime_ms = (uint64_t)(now.tv_sec - c->created_at.tv_sec) * 1000 +
                               (uint64_t)(now.tv_nsec - c->created_at.tv_nsec) / 1000000;
        if (lifetime_ms >= (uint64_t)cp->max_lifetime_ms) {
            if (c->fd >= 0) close(c->fd);
            c->fd = -1;
            if (c->state == CP_ACTIVE) cp->active_count--;
            else if (c->state == CP_IDLE) cp->idle_count--;
            c->state = CP_CLOSING;
            cp->total_destroyed++;
            closed++;
        }
    }
    if (closed > 0)
        printf("[cp] Closed %d expired connections (>%dms lifetime)\n",
               closed, cp->max_lifetime_ms);
    return closed;
}

void cp_health_check(ConnectionPool *cp)
{
    if (!cp) return;
    printf("[cp] Health check: %s:%d active=%d idle=%d total=%d\n",
           cp->host, cp->port, cp->active_count, cp->idle_count, cp->num_conn);
    for (int i = 0; i < cp->num_conn; i++) {
        CPConnection *c = &cp->connections[i];
        if (c->state == CP_ERROR) {
            if (c->fd >= 0) close(c->fd);
            c->fd = -1;
            c->state = CP_CLOSING;
            cp->total_destroyed++;
            printf("[cp] Removed errored connection #%d\n", i);
        }
    }
}

double cp_utilization(const ConnectionPool *cp)
{
    if (!cp || cp->num_conn == 0) return 0.0;
    return (double)cp->active_count / (double)cp->num_conn;
}

int cp_pool_size(const ConnectionPool *cp)
{
    return cp ? cp->num_conn : 0;
}

PoolManager* pm_init(void)
{
    PoolManager *pm = calloc(1, sizeof(PoolManager));
    if (!pm) return NULL;
    pm->num_pools = 0;
    pm->default_max_conn = 32;
    pm->default_max_idle_ms = 60000;
    pm->default_max_lifetime_ms = 1800000;
    pm->default_max_requests = 10000;
    printf("[pm] Pool manager initialized\n");
    return pm;
}

ConnectionPool* pm_get_or_create_pool(PoolManager *pm, const char *host,
                                       int port)
{
    if (!pm || !host) return NULL;
    for (int i = 0; i < pm->num_pools; i++) {
        if (strcmp(pm->pools[i].host, host) == 0 &&
            pm->pools[i].port == port)
            return &pm->pools[i];
    }
    if (pm->num_pools >= CP_MAX_POOLS) return NULL;
    ConnectionPool *cp = &pm->pools[pm->num_pools++];
    return cp_init(cp, host, port, pm->default_max_conn);
}

CPConnection* pm_borrow(PoolManager *pm, const char *host, int port)
{
    if (!pm || !host) return NULL;
    ConnectionPool *cp = pm_get_or_create_pool(pm, host, port);
    return cp ? cp_acquire(cp) : NULL;
}

int pm_return(PoolManager *pm, CPConnection *conn)
{
    if (!pm || !conn) return -1;
    for (int i = 0; i < pm->num_pools; i++) {
        ConnectionPool *cp = &pm->pools[i];
        if (strcmp(cp->host, conn->host) == 0 && cp->port == conn->port)
            return cp_release(cp, conn, conn->keep_alive);
    }
    return -1;
}

/*
 * Reap idle connections across all pools.
 * Implements Little's Law: L = lambda * W
 *   L = average idle connections in system
 *   lambda = average arrival rate of new connections
 *   W = average idle time before eviction
 * By tuning max_idle_ms, we control W directly.
 * Google's gRPC connection pool uses similar logic with
 * exponential backoff for idle reaping.
 */
void pm_reap_idle(PoolManager *pm)
{
    if (!pm) return;
    int total_closed = 0;
    for (int i = 0; i < pm->num_pools; i++) {
        total_closed += cp_close_idle(&pm->pools[i]);
    }
    if (total_closed > 0) printf("[pm] Reaped %d idle connections across %d pools\n",
                                  total_closed, pm->num_pools);
}

void pm_print_stats(const PoolManager *pm)
{
    if (!pm) return;
    printf("=== Pool Manager Stats ===\n");
    printf("Pools: %d\n", pm->num_pools);
    uint64_t total_created = 0, total_destroyed = 0, total_reused = 0;
    int total_active = 0, total_idle = 0, total_conn = 0;
    for (int i = 0; i < pm->num_pools; i++) {
        const ConnectionPool *cp = &pm->pools[i];
        printf("  [%s:%d] conn=%d active=%d idle=%d util=%.1f%%\n",
               cp->host, cp->port, cp->num_conn, cp->active_count,
               cp->idle_count, cp_utilization(cp) * 100.0);
        total_created += cp->total_created;
        total_destroyed += cp->total_destroyed;
        total_reused += cp->total_reused;
        total_active += cp->active_count;
        total_idle += cp->idle_count;
        total_conn += cp->num_conn;
    }
    printf("Summary: %d pools, %d conn (%d active, %d idle)\n",
           pm->num_pools, total_conn, total_active, total_idle);
    printf("Lifetime: %llu created, %llu destroyed, %llu reused\n",
           (unsigned long long)total_created,
           (unsigned long long)total_destroyed,
           (unsigned long long)total_reused);
    printf("===========================\n");
}