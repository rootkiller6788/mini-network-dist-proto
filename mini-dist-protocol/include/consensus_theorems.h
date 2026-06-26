#ifndef CONSENSUS_THEOREMS_H
#define CONSENSUS_THEOREMS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ================================================================
 * L4: Standards/Theorems - Distributed Consensus Theory
 *
 * FLP Impossibility (Fisher-Lynch-Patterson 1985),
 * CAP Theorem (Brewer 2000, Gilbert-Lynch 2002),
 * Two-Generals Paradox (Gray 1978),
 * Quorum Intersection (Lamport 1998)
 * ================================================================ */

#define FLP_MAX_PROCESSES    8
#define FLP_MAX_STEPS       64
#define FLP_MAX_MSG_BUFFER  64

typedef enum {
    FLP_DECIDED_0,
    FLP_DECIDED_1,
    FLP_UNDECIDED
} FLPDecision;

typedef enum {
    FLP_FLIP_COIN = 0,
    FLP_CRASH_AFTER_SEND,
    FLP_DELAY_MESSAGE,
    FLP_DELIVER_MESSAGE
} FLPAdversaryAction;

typedef struct {
    int           process_id;
    int           initial_value;
    int           current_value;
    FLPDecision   decision;
    bool          crashed;
    int           round;
    int           messages_sent;
} FLPProcess;

typedef struct {
    int           from;
    int           to;
    int           value;
    int           round;
    bool          delivered;
} FLPMessage;

typedef struct {
    FLPProcess    processes[FLP_MAX_PROCESSES];
    FLPMessage    messages[FLP_MAX_MSG_BUFFER];
    int           process_count;
    int           message_count;
    int           total_steps;
    bool          all_crashed;
} FLPSystem;

bool flp_is_bivalent(const FLPSystem *sys);
FLPDecision flp_run_adversary(FLPSystem *sys, int max_steps,
                               FLPAdversaryAction *actions,
                               int action_count);
void flp_init(FLPSystem *sys, int n);
bool flp_deliver_message(FLPSystem *sys, int msg_index);
void flp_crash_process(FLPSystem *sys, int pid);

typedef enum {
    CAP_CP,
    CAP_AP,
    CAP_CA_IMPOSSIBLE
} CAPClassification;

typedef struct {
    int           node_count;
    int           partition_boundary;
    bool          partition_active;
    bool          consistency_enforced;
    bool          availability_maintained;
} CAPSystem;

CAPClassification cap_classify(const CAPSystem *sys);
int cap_partition_experiment(int node_count, int partition_size,
                              bool prioritize_consistency,
                              int *total_operations,
                              int *failed_operations);

int two_generals_simulate(int max_rounds, double message_loss_prob);

int quorum_intersection_min(int n);
int quorum_pair_intersection(uint64_t quorum_a, uint64_t quorum_b, int n);
int quorum_enumerate_all(int n, uint64_t *quorum_masks, int max_quorums);
int byzantine_quorum_threshold(int f);
int crash_fault_quorum_threshold(int f);

#endif
