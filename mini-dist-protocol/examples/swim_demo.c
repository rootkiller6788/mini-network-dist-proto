#include "swim.h"
#include <stdio.h>

#define MEMBERS 5

int main(void) {
    SWIMCluster cluster;
    swim_init(&cluster, MEMBERS);

    printf("=== SWIM Membership Protocol Demo (%d members) ===\n\n", MEMBERS);

    printf("Phase 1: Initial Cluster\n");
    swim_print_cluster(&cluster);

    printf("\nPhase 2: Protocol Rounds (Ping + Indirect Ping)\n");
    printf("  Each round, one member pings a random peer.\n");
    printf("  ~20%% of pings fail, triggering indirect ping.\n\n");

    for (int round = 0; round < 10; round++) {
        swim_tick(&cluster, SWIM_PROTOCOL_PERIOD_MS);
        printf("Round %d (T=%llu ms): alive=%d suspected=%d dead=%d\n",
               round + 1,
               (unsigned long long)cluster.protocol_time_ms,
               swim_alive_count(&cluster),
               swim_suspected_count(&cluster),
               swim_dead_count(&cluster));
    }

    printf("\nPhase 3: Membership Change - Node Join\n");
    swim_join(&cluster, 20, 0x0A000014, 0);
    printf("  Node 20 joined via contact node 0\n");
    swim_print_cluster(&cluster);

    printf("\nPhase 4: Failure Detection\n");
    printf("  Simulating node 2 failure...\n");
    swim_suspect(&cluster, 2);
    printf("  Node 2 marked SUSPECTED\n");

    /* Let suspect timeout expire */
    for (int round = 0; round < 5; round++) {
        swim_tick(&cluster, SWIM_PROTOCOL_PERIOD_MS);
    }
    printf("  After suspect timeout, node 2 transitions to DEAD\n");
    swim_print_cluster(&cluster);

    printf("\nPhase 5: Member Leave\n");
    swim_leave(&cluster, 20);
    printf("  Node 20 voluntarily leaves the cluster\n");
    swim_print_cluster(&cluster);

    printf("\nPhase 6: Message Type Summary\n");
    for (int t = 0; t <= SWIM_MSG_LEAVE; t++)
        printf("  %s\n", swim_msg_name((SWIMMessageType)t));

    printf("\n=== SWIM Demo Complete ===\n");
    return 0;
}
