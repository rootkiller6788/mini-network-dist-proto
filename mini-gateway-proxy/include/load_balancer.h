#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define LB_MAX_SERVERS       32
#define LB_VIRTUAL_NODES     150
#define LB_RING_SIZE         (LB_MAX_SERVERS * LB_VIRTUAL_NODES)
#define LB_ADDR_SIZE         256

typedef enum {
    LB_ROUND_ROBIN,
    LB_WEIGHTED_ROUND_ROBIN,
    LB_LEAST_CONNECTIONS,
    LB_CONSISTENT_HASH,
    LB_RANDOM
} LBAlgorithm;

typedef struct {
    char   address[LB_ADDR_SIZE];
    int    port;
    int    weight;
    int    effective_weight;
    int    current_weight;
    int    active_connections;
    bool   healthy;
} LBServer;

typedef struct {
    uint32_t hash;
    int      server_index;
} LBVirtualNode;

typedef struct {
    LBServer      servers[LB_MAX_SERVERS];
    int           num_servers;
    LBAlgorithm   algorithm;
    int           rr_index;
    LBVirtualNode ring[LB_RING_SIZE];
    int           ring_size;
} LoadBalancer;

LoadBalancer* lb_init(LBAlgorithm algo);
int           lb_add_server(LoadBalancer *lb, const char *addr, int port, int weight);
int           lb_remove_server(LoadBalancer *lb, const char *addr, int port);
int           lb_select_server(LoadBalancer *lb, const char *key);
void          lb_health_check(LoadBalancer *lb);
void          lb_health_ping(LoadBalancer *lb, int index, bool alive);
uint32_t      lb_hash_key(const char *key);
void          lb_build_ring(LoadBalancer *lb);
void          lb_print_state(const LoadBalancer *lb);
int           lb_select_round_robin(LoadBalancer *lb);
int           lb_select_weighted_rr(LoadBalancer *lb);
int           lb_select_least_conn(LoadBalancer *lb);
int           lb_select_consistent_hash(LoadBalancer *lb, const char *key);
int           lb_select_random(LoadBalancer *lb);

#endif
