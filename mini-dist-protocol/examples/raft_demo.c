#include "raft.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define CLUSTER_SIZE 3
#define SIMULATION_TIME_MS 5000
#define TICK_MS 10

static void print_cluster(RaftNode *nodes, int n) {
    static int step = 0;
    printf("\n--- Step %d (T+%dms) ---\n", step, step * TICK_MS);
    for (int i = 0; i < n; i++) {
        raft_print_state(&nodes[i]);
    }
    int leader = raft_find_leader(nodes, n);
    printf("  Leader: %s\n", leader >= 0 ? "elected" : "none");
    step++;
}

int main(void) {
    RaftNode nodes[CLUSTER_SIZE];
    raft_init_cluster(nodes, CLUSTER_SIZE);

    printf("=== Raft 3-Node Cluster Demo ===\n\n");
    printf("Phase 1: Leader Election (randomized timeout 150-300ms)\n");
    printf("----------------------------------------\n");

    for (int t = 0; t < 1500; t += TICK_MS) {
        raft_tick(nodes, CLUSTER_SIZE, TICK_MS);
        if (t % 200 == 0) print_cluster(nodes, CLUSTER_SIZE);

        int leader = raft_find_leader(nodes, CLUSTER_SIZE);
        if (leader >= 0) {
            printf("\n>> Leader elected: Node %d at T+%dms\n\n", leader, t);
            break;
        }
    }

    printf("\nPhase 2: Log Replication\n");
    printf("----------------------------------------\n");

    int leader = raft_find_leader(nodes, CLUSTER_SIZE);
    if (leader >= 0) {
        for (int cmd = 1; cmd <= 5; cmd++) {
            raft_submit_command(&nodes[leader], nodes, cmd * 100);
            for (int t = 0; t < 300; t += TICK_MS) {
                raft_tick(nodes, CLUSTER_SIZE, TICK_MS);
            }
            printf("  Command %d submitted (value=%d)\n", cmd, cmd * 100);
            print_cluster(nodes, CLUSTER_SIZE);
        }
    }

    printf("\nPhase 3: Leader Step-Down on Partition\n");
    printf("----------------------------------------\n");

    leader = raft_find_leader(nodes, CLUSTER_SIZE);
    if (leader >= 0) {
        printf("  Isolating leader node %d...\n", leader);
        raft_isolate_node(&nodes[leader]);

        for (int t = 0; t < 2000; t += TICK_MS) {
            raft_tick(nodes, CLUSTER_SIZE, TICK_MS);
            if (t % 400 == 0) print_cluster(nodes, CLUSTER_SIZE);

            int new_leader = raft_find_leader(nodes, CLUSTER_SIZE);
            if (new_leader >= 0 && new_leader != leader) {
                printf("\n>> New leader elected: Node %d at T+%dms\n",
                       new_leader, t + 1500);
                break;
            }
        }

        printf("\n  Reconnecting node %d...\n", leader);
        raft_reconnect_node(nodes, CLUSTER_SIZE, leader);

        for (int t = 0; t < 1000; t += TICK_MS) {
            raft_tick(nodes, CLUSTER_SIZE, TICK_MS);
        }
        print_cluster(nodes, CLUSTER_SIZE);
    }

    printf("\n=== Simulation Complete ===\n");
    return 0;
}
