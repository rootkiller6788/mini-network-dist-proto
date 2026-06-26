#include "gossip.h"
#include <stdio.h>

int main(void)
{
    GossipNetwork gn;
    gossip_init_network(&gn, 8, GOSSIP_PUSH_PULL, 3);
    printf("=== Gossip Protocol Demo ===\n");
    printf("8 nodes, Push-Pull mode, fanout=3\n");

    gossip_update_state(&gn.nodes[0], 1, 100);
    printf("Node 0 updated with key=1, value=100. Spreading...\n");

    int rounds = gossip_rounds_to_converge(&gn, 15);
    printf("Rounds to converge: %d\n", rounds);
    printf("Infection rate: %.1f%%\n", gossip_infection_rate(&gn) * 100.0);

    gossip_print_network(&gn);
    return 0;
}
