#ifndef PAXOS_H
#define PAXOS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PAXOS_MAX_NODES      8
#define PAXOS_MAX_LOG_ENTRIES 512

typedef struct {
    int      proposer_id;
    uint64_t proposal_num;
    int      value;
} PaxosProposer;

typedef struct {
    int      acceptor_id;
    uint64_t promised_num;
    uint64_t accepted_num;
    int      accepted_value;
    bool     has_accepted;
} PaxosAcceptor;

typedef struct {
    int      learner_id;
    int      learned_value;
    uint64_t learned_round;
    bool     has_learned;
} PaxosLearner;

typedef struct {
    PaxosProposer proposers[PAXOS_MAX_NODES];
    PaxosAcceptor acceptors[PAXOS_MAX_NODES];
    PaxosLearner  learners[PAXOS_MAX_NODES];
    int           node_count;
    int           quorum;
} PaxosCluster;

bool paxos_prepare(PaxosProposer *proposer, PaxosAcceptor *acceptors, int n,
                   int *promise_count, uint64_t *highest_accepted_num,
                   int *highest_accepted_value);
bool paxos_promise(PaxosAcceptor *acceptor, uint64_t proposal_num,
                   uint64_t *prev_accepted_num, int *prev_accepted_value,
                   bool *has_prev);
bool paxos_accept(PaxosProposer *proposer, PaxosAcceptor *acceptors, int n,
                  int value, int *accept_count);
bool paxos_accepted(PaxosAcceptor *acceptor, uint64_t proposal_num, int value);
void paxos_learn(const PaxosCluster *cluster, int value);
void paxos_init_cluster(PaxosCluster *cluster, int n);
bool paxos_run_instance(PaxosCluster *cluster, int proposer_id, int value);
void paxos_reset_proposer(PaxosProposer *proposer, int id);

void multi_paxos_become_leader(PaxosCluster *cluster, int leader_id);
bool multi_paxos_replicate(PaxosCluster *cluster, int leader_id, int value);
void multi_paxos_print_state(const PaxosCluster *cluster);
const char *paxos_phase_name(int phase);

#endif
