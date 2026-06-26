#include "leader_election.h"
#include <stdio.h>

int main(void)
{
    int priorities[] = {1, 5, 3, 7, 2};
    LENetwork bully_net, ring_net;

    printf("=== Leader Election Demo ===\n\n");

    printf("--- Bully Algorithm ---\n");
    le_init_network(&bully_net, 5, priorities, LE_BULLY);
    le_bully_start_election(&bully_net, 0);
    le_process_messages(&bully_net);
    le_process_messages(&bully_net);
    printf("Bully elected leader: Node %d (priority %d)\n",
           bully_net.elected_leader,
           bully_net.nodes[bully_net.elected_leader].priority);

    printf("\n--- Ring Algorithm ---\n");
    le_init_network(&ring_net, 5, priorities, LE_RING);
    le_ring_start_election(&ring_net, 0);
    int i;
    for (i = 0; i < 10; i++) {
        le_process_messages(&ring_net);
    }
    printf("Ring elected leader: Node %d (priority %d)\n",
           ring_net.elected_leader,
           ring_net.elected_leader >= 0 ? ring_net.nodes[ring_net.elected_leader].priority : -1);

    return 0;
}
