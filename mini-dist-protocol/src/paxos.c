#include "paxos.h"
#include <stdio.h>
#include <string.h>

void paxos_init_cluster(PaxosCluster *cluster, int n) {
    cluster->node_count = n;
    cluster->quorum     = (n / 2) + 1;
    for (int i = 0; i < n; i++) {
        cluster->proposers[i].proposer_id  = i;
        cluster->proposers[i].proposal_num = 0;
        cluster->proposers[i].value        = 0;

        cluster->acceptors[i].acceptor_id    = i;
        cluster->acceptors[i].promised_num   = 0;
        cluster->acceptors[i].accepted_num   = 0;
        cluster->acceptors[i].accepted_value = 0;
        cluster->acceptors[i].has_accepted   = false;

        cluster->learners[i].learner_id    = i;
        cluster->learners[i].learned_value = 0;
        cluster->learners[i].learned_round = 0;
        cluster->learners[i].has_learned   = false;
    }
}

void paxos_reset_proposer(PaxosProposer *proposer, int id) {
    proposer->proposer_id  = id;
    proposer->proposal_num = 0;
    proposer->value        = 0;
}

bool paxos_prepare(PaxosProposer *proposer, PaxosAcceptor *acceptors, int n,
                   int *promise_count, uint64_t *highest_accepted_num,
                   int *highest_accepted_value) {
    *promise_count        = 0;
    *highest_accepted_num = 0;
    *highest_accepted_value = 0;

    for (int i = 0; i < n; i++) {
        uint64_t prev_num = 0;
        int      prev_val = 0;
        bool     has_prev = false;

        if (paxos_promise(&acceptors[i], proposer->proposal_num,
                          &prev_num, &prev_val, &has_prev)) {
            (*promise_count)++;
            if (has_prev && prev_num > *highest_accepted_num) {
                *highest_accepted_num   = prev_num;
                *highest_accepted_value = prev_val;
            }
        }
    }

    if (*highest_accepted_num > 0) {
        proposer->value = *highest_accepted_value;
    }

    return *promise_count >= (n / 2 + 1);
}

bool paxos_promise(PaxosAcceptor *acceptor, uint64_t proposal_num,
                   uint64_t *prev_accepted_num, int *prev_accepted_value,
                   bool *has_prev) {
    if (proposal_num <= acceptor->promised_num) {
        return false;
    }

    *prev_accepted_num   = acceptor->accepted_num;
    *prev_accepted_value = acceptor->accepted_value;
    *has_prev            = acceptor->has_accepted;

    acceptor->promised_num = proposal_num;
    return true;
}

bool paxos_accept(PaxosProposer *proposer, PaxosAcceptor *acceptors, int n,
                  int value, int *accept_count) {
    *accept_count = 0;
    for (int i = 0; i < n; i++) {
        if (paxos_accepted(&acceptors[i], proposer->proposal_num, value)) {
            (*accept_count)++;
        }
    }
    return *accept_count >= (n / 2 + 1);
}

bool paxos_accepted(PaxosAcceptor *acceptor, uint64_t proposal_num, int value) {
    if (proposal_num < acceptor->promised_num) {
        return false;
    }

    acceptor->accepted_num   = proposal_num;
    acceptor->accepted_value = value;
    acceptor->has_accepted   = true;
    return true;
}

void paxos_learn(const PaxosCluster *cluster, int value) {
    (void)cluster;
    (void)value;
}

bool paxos_run_instance(PaxosCluster *cluster, int proposer_id, int value) {
    PaxosProposer *prop = &cluster->proposers[proposer_id];

    prop->proposal_num = (uint64_t)((proposer_id + 1) * 1000 +
                          (prop->proposal_num % 1000) + 1);
    prop->value = value;

    int      promise_count = 0;
    uint64_t highest_num   = 0;
    int      highest_val   = 0;

    bool prepared = paxos_prepare(prop, cluster->acceptors,
                                   cluster->node_count,
                                   &promise_count, &highest_num,
                                   &highest_val);

    if (!prepared) {
        return false;
    }

    int accept_count = 0;
    int final_value  = (highest_num > 0) ? highest_val : value;

    bool accepted = paxos_accept(prop, cluster->acceptors,
                                  cluster->node_count,
                                  final_value, &accept_count);

    if (accepted) {
        for (int i = 0; i < cluster->node_count; i++) {
            cluster->learners[i].learned_value = final_value;
            cluster->learners[i].learned_round = prop->proposal_num;
            cluster->learners[i].has_learned   = true;
        }
        return true;
    }

    return false;
}

static int multi_paxos_log[PAXOS_MAX_LOG_ENTRIES];
static int multi_paxos_log_count = 0;
static int multi_paxos_leader = -1;

void multi_paxos_become_leader(PaxosCluster *cluster, int leader_id) {
    multi_paxos_leader = leader_id;
    int success = 0;
    for (int i = 0; i < 3; i++) {
        cluster->proposers[leader_id].proposal_num += 1000;
        int pc = 0;
        uint64_t hn = 0;
        int hv = 0;
        if (paxos_prepare(&cluster->proposers[leader_id],
                          cluster->acceptors, cluster->node_count,
                          &pc, &hn, &hv)) {
            success = 1;
            break;
        }
    }
    if (!success) {
        multi_paxos_leader = -1;
    }
}

bool multi_paxos_replicate(PaxosCluster *cluster, int leader_id, int value) {
    if (leader_id != multi_paxos_leader) return false;
    if (multi_paxos_log_count >= PAXOS_MAX_LOG_ENTRIES) return false;

    multi_paxos_log[multi_paxos_log_count] = value;
    multi_paxos_log_count++;

    int accept_count = 0;
    paxos_accept(&cluster->proposers[leader_id],
                 cluster->acceptors, cluster->node_count,
                 value, &accept_count);

    if (accept_count >= cluster->quorum) {
        for (int i = 0; i < cluster->node_count; i++) {
            cluster->learners[i].learned_value = value;
            cluster->learners[i].learned_round =
                cluster->proposers[leader_id].proposal_num;
            cluster->learners[i].has_learned = true;
        }
        return true;
    }
    return false;
}

void multi_paxos_print_state(const PaxosCluster *cluster) {
    printf("=== Multi-Paxos Cluster (%d nodes, quorum=%d) ===\n",
           cluster->node_count, cluster->quorum);
    printf("  Leader: %s\n",
           multi_paxos_leader >= 0 ? "elected" : "none");
    for (int i = 0; i < cluster->node_count; i++) {
        printf("  Acceptor %d: promised=%llu accepted=%llu value=%d\n",
               cluster->acceptors[i].acceptor_id,
               (unsigned long long)cluster->acceptors[i].promised_num,
               (unsigned long long)cluster->acceptors[i].accepted_num,
               cluster->acceptors[i].accepted_value);
    }
    printf("  Log entries: %d\n", multi_paxos_log_count);
}

const char *paxos_phase_name(int phase) {
    switch (phase) {
        case 1: return "Prepare (Phase 1a)";
        case 2: return "Promise (Phase 1b)";
        case 3: return "Accept (Phase 2a)";
        case 4: return "Accepted (Phase 2b)";
        default: return "Unknown";
    }
}
