#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "load_balancer.h"

#define NUM_SERVERS   5
#define NUM_REQUESTS  100
#define REQUESTS_PER_ALGO 100

static int dist[NUM_SERVERS];

static void reset_dist(void)
{
    for (int i = 0; i < NUM_SERVERS; i++) dist[i] = 0;
}

static void print_dist(const char *label, int total)
{
    printf("\n--- %s (total=%d) ---\n", label, total);
    for (int i = 0; i < NUM_SERVERS; i++) {
        double pct = (total > 0) ? (100.0 * dist[i] / total) : 0.0;
        printf("  Server %d: %3d requests (%.1f%%)", i, dist[i], pct);
        /* simple ascii bar */
        int bar = (int)(pct / 2.0);
        printf("  ");
        for (int b = 0; b < bar; b++) printf("#");
        printf("\n");
    }
}

static void run_rr_demo(void)
{
    printf("\n===== Round-Robin Demo =====\n");
    LoadBalancer *lb = lb_init(LB_ROUND_ROBIN);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {5, 1, 1, 3, 2};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    reset_dist();
    for (int i = 0; i < REQUESTS_PER_ALGO; i++) {
        int idx = lb_select_server(lb, NULL);
        if (idx >= 0) dist[idx]++;
    }
    print_dist("Round-Robin", REQUESTS_PER_ALGO);
    lb_print_state(lb);
    free(lb);
}

static void run_wrr_demo(void)
{
    printf("\n===== Weighted Round-Robin (NGINX SWRR) Demo =====\n");
    LoadBalancer *lb = lb_init(LB_WEIGHTED_ROUND_ROBIN);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {5, 1, 1, 3, 2};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    reset_dist();
    for (int i = 0; i < REQUESTS_PER_ALGO; i++) {
        int idx = lb_select_server(lb, NULL);
        if (idx >= 0) dist[idx]++;
    }
    print_dist("Weighted Round-Robin (weights: 5,1,1,3,2)", REQUESTS_PER_ALGO);
    printf("  Expected distribution: ~42%% srv-a, ~8%% srv-b, ~8%% srv-c, "
           "~25%% srv-d, ~17%% srv-e\n");
    lb_print_state(lb);
    free(lb);
}

static void run_least_conn_demo(void)
{
    printf("\n===== Least Connections Demo =====\n");
    LoadBalancer *lb = lb_init(LB_LEAST_CONNECTIONS);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {5, 1, 1, 3, 2};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    /* simulate active connections to bias distribution */
    lb->servers[1].active_connections = 50;
    lb->servers[2].active_connections = 30;

    reset_dist();
    for (int i = 0; i < REQUESTS_PER_ALGO; i++) {
        int idx = lb_select_server(lb, NULL);
        if (idx >= 0) {
            dist[idx]++;
            lb->servers[idx].active_connections++;
        }
    }
    print_dist("Least Connections (srv-b=50, srv-c=30 preloaded)", REQUESTS_PER_ALGO);
    printf("  srv-b and srv-c should receive fewer requests due to high connection count\n");
    lb_print_state(lb);
    free(lb);
}

static void run_consistent_hash_demo(void)
{
    printf("\n===== Consistent Hash Demo =====\n");
    LoadBalancer *lb = lb_init(LB_CONSISTENT_HASH);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {5, 1, 1, 3, 2};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    /* demonstrate sticky sessions via consistent hash keys */
    const char *users[] = {
        "alice", "bob", "charlie", "david", "eve",
        "frank", "grace", "henry", "iris", "jack",
        "kate", "leo", "mia", "noah", "olivia"
    };
    int num_users = 15;

    reset_dist();
    printf("\n  User -> Server mapping (consistent hash):\n");
    for (int i = 0; i < num_users; i++) {
        int idx = lb_select_server(lb, users[i]);
        if (idx >= 0) {
            dist[idx]++;
            printf("    %-10s -> srv-%c\n", users[i], 'a' + idx);
        }
    }
    print_dist("Consistent Hash (15 unique users)", num_users);

    /* demonstrate: removing a server redistributes only its keys */
    printf("\n  --- Removing srv-c from ring ---\n");
    int before_count = dist[2];
    lb_remove_server(lb, "srv-c", 8082);

    /* recheck user mappings */
    reset_dist();
    printf("\n  User -> Server mapping (after removing srv-c):\n");
    for (int i = 0; i < num_users; i++) {
        int idx = lb_select_server(lb, users[i]);
        if (idx >= 0) {
            dist[idx]++;
            printf("    %-10s -> srv-%c", users[i], 'a' + idx);
            if (idx == 2) printf("  (stayed with srv-c? no - srv-c removed)");
            printf("\n");
        }
    }
    printf("\n  Only ~%d keys (%.0f%%) from srv-c should be redistributed\n",
           before_count, 100.0 * before_count / num_users);

    lb_print_state(lb);
    free(lb);
}

static void run_random_demo(void)
{
    printf("\n===== Random Demo =====\n");
    LoadBalancer *lb = lb_init(LB_RANDOM);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {1, 1, 1, 1, 1};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    reset_dist();
    for (int i = 0; i < REQUESTS_PER_ALGO; i++) {
        int idx = lb_select_server(lb, NULL);
        if (idx >= 0) dist[idx]++;
    }
    print_dist("Random Distribution", REQUESTS_PER_ALGO);
    printf("  With 100 requests, each server should get ~20 requests\n");
    lb_print_state(lb);
    free(lb);
}

static void run_health_check_demo(void)
{
    printf("\n===== Health Check with Failover Demo =====\n");
    LoadBalancer *lb = lb_init(LB_ROUND_ROBIN);

    const char *addrs[NUM_SERVERS] = {"srv-a", "srv-b", "srv-c", "srv-d", "srv-e"};
    int weights[NUM_SERVERS] = {5, 1, 1, 3, 2};

    for (int i = 0; i < NUM_SERVERS; i++) {
        lb_add_server(lb, addrs[i], 8080 + i, weights[i]);
    }

    printf("\n  Initial health check:\n");
    lb_health_check(lb);

    /* mark 2 servers as unhealthy */
    printf("\n  Marking srv-b and srv-d as DOWN:\n");
    lb_health_ping(lb, 1, false);
    lb_health_ping(lb, 3, false);

    reset_dist();
    for (int i = 0; i < 50; i++) {
        int idx = lb_select_server(lb, NULL);
        if (idx >= 0) dist[idx]++;
    }
    print_dist("RR Distribution (srv-b and srv-d DOWN)", 50);
    printf("  srv-b and srv-d should receive 0 requests\n");
    lb_print_state(lb);
    free(lb);
}

int main(void)
{
    srand((unsigned)time(NULL));

    printf("============================================\n");
    printf("  Load Balancer Demo - 5 Algorithm Showcase\n");
    printf("============================================\n");
    printf("Backends: 5 servers (srv-a to srv-e)\n");
    printf("Requests per algorithm: %d\n", REQUESTS_PER_ALGO);

    run_rr_demo();
    run_wrr_demo();
    run_least_conn_demo();
    run_consistent_hash_demo();
    run_random_demo();
    run_health_check_demo();

    printf("\n============================================\n");
    printf("  Demo Complete\n");
    printf("============================================\n");

    return 0;
}
