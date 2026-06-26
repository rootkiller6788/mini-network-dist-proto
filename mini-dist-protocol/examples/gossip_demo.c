#include "gossip.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define NODE_COUNT 5
#define MAX_ROUNDS 20

int main(void) {
    GossipNode nodes[NODE_COUNT];

    gossip_init_full(nodes, NODE_COUNT);

    printf("=== Gossip Protocol Demo: 5-Node Full Mesh ===\n\n");

    printf("Phase 1: Initialize Partial Data\n");
    printf("  Each node starts with only 2 unique keys.\n\n");

    gossip_set_data(&nodes[0], 1, 100);
    gossip_set_data(&nodes[0], 2, 200);
    gossip_set_data(&nodes[1], 3, 300);
    gossip_set_data(&nodes[1], 4, 400);
    gossip_set_data(&nodes[2], 5, 500);
    gossip_set_data(&nodes[2], 6, 600);
    gossip_set_data(&nodes[3], 7, 700);
    gossip_set_data(&nodes[3], 8, 800);
    gossip_set_data(&nodes[4], 9, 900);
    gossip_set_data(&nodes[4], 10, 1000);

    printf("Initial state:\n");
    gossip_print_all(nodes, NODE_COUNT);
    printf("\n");

    printf("Phase 2: Gossip Spread (Push-Pull)\n");
    printf("  Each round, nodes exchange data with random neighbors.\n");
    printf("  Version vectors ensure latest-write-wins.\n\n");

    int rounds_needed = 0;
    bool converged = false;

    for (int r = 0; r < MAX_ROUNDS; r++) {
        gossip_spread(nodes, NODE_COUNT, GOSSIP_PUSH_PULL);

        printf("Round %d:\n", r + 1);
        gossip_print_all(nodes, NODE_COUNT);

        if (gossip_all_synced(nodes, NODE_COUNT)) {
            rounds_needed = r + 1;
            converged = true;
            printf("  >> FULL CONVERGENCE achieved!\n\n");
            break;
        }
        printf("\n");
    }

    if (!converged) {
        printf("  >> Did not fully converge within %d rounds.\n\n", MAX_ROUNDS);
    } else {
        printf("  Convergence in %d rounds.\n", rounds_needed);
    }

    printf("Phase 3: Version Spread Analysis\n");
    printf("  Demonstrating eventual consistency through anti-entropy.\n\n");

    gossip_set_data(&nodes[0], 1, 111);
    gossip_set_data(&nodes[2], 1, 222);

    printf("  Node 0 set key=1 value=111 (version v=%llu)\n",
           (unsigned long long)nodes[0].version_clock);
    printf("  Node 2 set key=1 value=222 (version v=%llu)\n",
           (unsigned long long)nodes[2].version_clock);
    printf("  After gossip, max version wins.\n\n");

    for (int r = 0; r < 5; r++) {
        gossip_spread(nodes, NODE_COUNT, GOSSIP_PUSH_PULL);
        printf("Round %d:\n", r + 1);
        for (int i = 0; i < NODE_COUNT; i++) {
            int val;
            uint64_t ver;
            if (gossip_get_data(&nodes[i], 1, &val, &ver)) {
                printf("  Node %d: key=1 value=%d version=%llu\n",
                       i, val, (unsigned long long)ver);
            } else {
                printf("  Node %d: key=1 MISSING\n", i);
            }
        }
        printf("\n");
    }

    printf("=== Gossip Demo Complete ===\n");
    return 0;
}
