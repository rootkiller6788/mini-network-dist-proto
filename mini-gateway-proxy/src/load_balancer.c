#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "load_balancer.h"

LoadBalancer* lb_init(LBAlgorithm algo)
{
    LoadBalancer *lb = calloc(1, sizeof(LoadBalancer));
    if (!lb) return NULL;
    lb->algorithm = algo;
    lb->rr_index = 0;
    lb->num_servers = 0;
    lb->ring_size = 0;
    return lb;
}

int lb_add_server(LoadBalancer *lb, const char *addr, int port, int weight)
{
    if (!lb || !addr || lb->num_servers >= LB_MAX_SERVERS) return -1;
    if (weight <= 0) weight = 1;

    LBServer *s = &lb->servers[lb->num_servers];
    snprintf(s->address, LB_ADDR_SIZE, "%s:%d", addr, port);
    s->port = port;
    s->weight = weight;
    s->effective_weight = weight;
    s->current_weight = 0;
    s->active_connections = 0;
    s->healthy = true;
    lb->num_servers++;

    if (lb->algorithm == LB_CONSISTENT_HASH) {
        lb_build_ring(lb);
    }

    printf("[lb] Added server %s weight=%d (total=%d)\n",
           s->address, weight, lb->num_servers);
    return lb->num_servers - 1;
}

int lb_remove_server(LoadBalancer *lb, const char *addr, int port)
{
    if (!lb || !addr) return -1;
    char full_addr[LB_ADDR_SIZE];
    snprintf(full_addr, LB_ADDR_SIZE, "%s:%d", addr, port);

    for (int i = 0; i < lb->num_servers; i++) {
        if (strcmp(lb->servers[i].address, full_addr) == 0) {
            memmove(&lb->servers[i], &lb->servers[i + 1],
                    (size_t)(lb->num_servers - i - 1) * sizeof(LBServer));
            lb->num_servers--;
            if (lb->algorithm == LB_CONSISTENT_HASH) {
                lb_build_ring(lb);
            }
            printf("[lb] Removed server %s (total=%d)\n", full_addr, lb->num_servers);
            return 0;
        }
    }
    return -1;
}

uint32_t lb_hash_key(const char *key)
{
    /* FNV-1a hash */
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint32_t)(unsigned char)*key++;
        hash *= 16777619u;
    }
    return hash;
}

void lb_build_ring(LoadBalancer *lb)
{
    if (!lb) return;
    lb->ring_size = 0;

    for (int i = 0; i < lb->num_servers; i++) {
        for (int v = 0; v < LB_VIRTUAL_NODES; v++) {
            char vnode_key[320];
            snprintf(vnode_key, sizeof(vnode_key), "%s#%d",
                     lb->servers[i].address, v);
            uint32_t h = lb_hash_key(vnode_key);

            /* sorted insert into ring */
            int pos = 0;
            while (pos < lb->ring_size && lb->ring[pos].hash < h) pos++;
            memmove(&lb->ring[pos + 1], &lb->ring[pos],
                    (size_t)(lb->ring_size - pos) * sizeof(LBVirtualNode));
            lb->ring[pos].hash = h;
            lb->ring[pos].server_index = i;
            lb->ring_size++;
        }
    }

    printf("[lb] Built consistent hash ring with %d virtual nodes\n",
           lb->ring_size);
}

int lb_select_server(LoadBalancer *lb, const char *key)
{
    if (!lb || lb->num_servers == 0) return -1;

    switch (lb->algorithm) {
    case LB_ROUND_ROBIN:
        return lb_select_round_robin(lb);
    case LB_WEIGHTED_ROUND_ROBIN:
        return lb_select_weighted_rr(lb);
    case LB_LEAST_CONNECTIONS:
        return lb_select_least_conn(lb);
    case LB_CONSISTENT_HASH:
        return lb_select_consistent_hash(lb, key ? key : "default");
    case LB_RANDOM:
        return lb_select_random(lb);
    default:
        return -1;
    }
}

int lb_select_round_robin(LoadBalancer *lb)
{
    for (int tries = 0; tries < lb->num_servers; tries++) {
        int idx = lb->rr_index % lb->num_servers;
        lb->rr_index++;
        if (lb->servers[idx].healthy) return idx;
    }
    return -1;
}

int lb_select_weighted_rr(LoadBalancer *lb)
{
    /* NGINX smooth weighted round-robin (SWRR) */
    int total_weight = 0;
    int best_idx = -1;
    int best_weight = -1;

    for (int i = 0; i < lb->num_servers; i++) {
        if (!lb->servers[i].healthy) continue;
        lb->servers[i].current_weight += lb->servers[i].effective_weight;
        total_weight += lb->servers[i].effective_weight;

        if (best_idx < 0 ||
            lb->servers[i].current_weight > best_weight) {
            best_idx = i;
            best_weight = lb->servers[i].current_weight;
        }
    }

    if (best_idx >= 0) {
        lb->servers[best_idx].current_weight -= total_weight;
    }
    return best_idx;
}

int lb_select_least_conn(LoadBalancer *lb)
{
    int best_idx = -1;
    int min_conn = -1;

    for (int i = 0; i < lb->num_servers; i++) {
        if (!lb->servers[i].healthy) continue;
        if (best_idx < 0 || lb->servers[i].active_connections < min_conn) {
            best_idx = i;
            min_conn = lb->servers[i].active_connections;
        }
    }
    return best_idx;
}

int lb_select_consistent_hash(LoadBalancer *lb, const char *key)
{
    if (lb->ring_size == 0 || !key) return lb_select_round_robin(lb);

    uint32_t h = lb_hash_key(key);

    /* binary search on ring */
    int lo = 0, hi = lb->ring_size - 1;
    int idx = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (lb->ring[mid].hash >= h) {
            idx = mid;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    if (lo >= lb->ring_size) idx = 0;

    return lb->ring[idx].server_index;
}

int lb_select_random(LoadBalancer *lb)
{
    int healthy_count = 0;
    int healthy_indices[LB_MAX_SERVERS];

    for (int i = 0; i < lb->num_servers; i++) {
        if (lb->servers[i].healthy) {
            healthy_indices[healthy_count++] = i;
        }
    }
    if (healthy_count == 0) return -1;

    return healthy_indices[rand() % healthy_count];
}

void lb_health_ping(LoadBalancer *lb, int index, bool alive)
{
    if (!lb || index < 0 || index >= lb->num_servers) return;

    bool prev = lb->servers[index].healthy;
    lb->servers[index].healthy = alive;

    if (prev != alive) {
        printf("[lb] Server %s is now %s\n",
               lb->servers[index].address,
               alive ? "HEALTHY" : "UNHEALTHY");
        if (lb->algorithm == LB_CONSISTENT_HASH) {
            lb_build_ring(lb);
        }
    }
}

void lb_health_check(LoadBalancer *lb)
{
    if (!lb) return;
    printf("[lb] Running health check on %d servers\n", lb->num_servers);

    for (int i = 0; i < lb->num_servers; i++) {
        bool was_healthy = lb->servers[i].healthy;
        /* simulate a health check: penalize servers with >10 active connections */
        lb->servers[i].healthy = (lb->servers[i].active_connections < 100);

        if (was_healthy && !lb->servers[i].healthy) {
            lb->servers[i].current_weight = 0;
            if (lb->algorithm == LB_CONSISTENT_HASH) {
                lb_build_ring(lb);
            }
        }
        if (!was_healthy && lb->servers[i].healthy) {
            lb->servers[i].effective_weight = lb->servers[i].weight;
        }

        printf("  %s: %s (conn=%d, weight=%d/%d)\n",
               lb->servers[i].address,
               lb->servers[i].healthy ? "UP" : "DOWN",
               lb->servers[i].active_connections,
               lb->servers[i].effective_weight,
               lb->servers[i].weight);
    }
}

void lb_print_state(const LoadBalancer *lb)
{
    if (!lb) return;

    const char *algo_names[] = {
        "ROUND_ROBIN", "WEIGHTED_ROUND_ROBIN",
        "LEAST_CONNECTIONS", "CONSISTENT_HASH", "RANDOM"
    };

    printf("=== Load Balancer State ===\n");
    printf("Algorithm: %s\n",
           lb->algorithm < 5 ? algo_names[lb->algorithm] : "UNKNOWN");
    printf("Servers: %d\n", lb->num_servers);

    for (int i = 0; i < lb->num_servers; i++) {
        printf("  [%d] %-24s weight=%-3d conn=%-4d current_w=%-3d %s\n",
               i, lb->servers[i].address,
               lb->servers[i].weight,
               lb->servers[i].active_connections,
               lb->servers[i].current_weight,
               lb->servers[i].healthy ? "UP" : "DOWN");
    }

    if (lb->algorithm == LB_CONSISTENT_HASH) {
        printf("Ring nodes: %d\n", lb->ring_size);
    }
    printf("===========================\n");
}
