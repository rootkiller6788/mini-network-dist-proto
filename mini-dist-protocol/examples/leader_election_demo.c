#include "leader_election.h"
#include <stdio.h>

static void compare_complexity(int n) {
    printf("  Bully:    O(n^2) = %d messages\n", bully_message_complexity(n));
    printf("  Ring:     O(2n) = %d messages\n", ring_message_complexity(n));
    printf("  ZK-style: O(2n) = %d messages\n", zk_message_complexity(n));
}

int main(void) {
    const int N = 5;

    printf("=== Leader Election Algorithms Comparison ===\n");
    printf("  Node count: %d\n\n", N);

    printf("Phase 1: Bully Algorithm (highest ID wins)\n");
    printf("------------------------------------------------\n");

    BullyCluster bully;
    bully_init(&bully, N);
    printf("  Initial state:\n");
    bully_print(&bully);

    printf("\n  Node 2 starts election...\n");
    int winner = bully_election(&bully, 2);
    printf("  Winner: Node %d\n\n", winner);
    bully_print(&bully);

    printf("\n  Leader (node %d) crashes...\n", winner);
    bully_node_crash(&bully, winner);
    printf("  New election triggered by highest remaining node.\n");
    bully_print(&bully);

    printf("\n  Crashed node recovers...\n");
    bully_node_recover(&bully, winner);
    bully_print(&bully);

    printf("\nPhase 2: Ring Algorithm (token circulation)\n");
    printf("------------------------------------------------\n");

    RingCluster ring;
    ring_init(&ring, N);
    printf("  Initial state (ring topology):\n");
    ring_print(&ring);

    printf("\n  Node 0 starts election token...\n");
    winner = ring_election(&ring, 0);
    printf("  Winner: Node %d (after token completes ring)\n\n", winner);
    ring_print(&ring);

    printf("\n  Node %d crashes, ring reforms...\n", winner);
    ring_node_crash(&ring, winner);
    ring_print(&ring);

    printf("\nPhase 3: ZooKeeper-style (ephemeral sequential)\n");
    printf("------------------------------------------------\n");

    ZKCluster zk;
    zk_init(&zk, N);
    printf("  Initial state:\n");
    zk_print(&zk);

    printf("\n  Nodes create ephemeral sequential znodes...\n");
    winner = zk_leader_election(&zk);
    printf("  Winner: Node %d (lowest sequence number)\n\n", winner);
    zk_print(&zk);

    printf("\n  Leader crashes, re-election triggered...\n");
    zk_node_crash(&zk, winner);
    winner = zk_leader_election(&zk);
    printf("  New winner: Node %d\n\n", winner);
    zk_print(&zk);

    printf("\nPhase 4: Message Complexity Comparison\n");
    printf("------------------------------------------------\n");
    printf("  For N=%d nodes:\n", N);
    compare_complexity(N);

    printf("\n  For N=10 nodes:\n");
    compare_complexity(10);

    printf("\n=== Leader Election Demo Complete ===\n");
    return 0;
}
