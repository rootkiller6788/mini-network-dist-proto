#include "paxos.h"
#include <stdio.h>

#define NODES 5

int main(void) {
    PaxosCluster cluster;
    paxos_init_cluster(&cluster, NODES);

    printf("=== Multi-Paxos Demo (%d nodes, quorum=%d) ===\n\n",
           NODES, cluster.quorum);

    printf("Phase 1: Basic Paxos Instance\n");
    printf("  Proposer 0 runs a single-decree Paxos for value=42\n");

    bool ok = paxos_run_instance(&cluster, 0, 42);
    printf("  Result: %s\n", ok ? "CONSENSUS REACHED" : "FAILED");

    if (ok) {
        for (int i = 0; i < NODES; i++) {
            printf("  Learner %d learned: value=%d round=%llu\n",
                   i,
                   cluster.learners[i].learned_value,
                   (unsigned long long)cluster.learners[i].learned_round);
        }
    }

    printf("\nPhase 2: Multi-Paxos Leader Election\n");
    printf("  Node 0 attempts to become leader (Phase 1 bypass)\n");

    multi_paxos_become_leader(&cluster, 0);
    multi_paxos_print_state(&cluster);

    printf("\nPhase 3: Log Replication via Multi-Paxos\n");
    printf("  Leader replicates 5 commands via Phase 2 only\n");

    for (int cmd = 1; cmd <= 5; cmd++) {
        ok = multi_paxos_replicate(&cluster, 0, cmd * 100);
        printf("  Command %d (value=%d): %s\n",
               cmd, cmd * 100, ok ? "COMMITTED" : "FAILED");
    }

    multi_paxos_print_state(&cluster);

    printf("\nPhase 4: Paxos Phase Names\n");
    for (int p = 1; p <= 4; p++)
        printf("  Phase %d: %s\n", p, paxos_phase_name(p));

    printf("\n=== Multi-Paxos Demo Complete ===\n");
    return 0;
}
