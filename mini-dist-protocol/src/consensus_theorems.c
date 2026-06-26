#include "consensus_theorems.h"
#include <string.h>
#include <stdio.h>

static int flp_quorum(int n) {
    return n / 2 + 1;
}

void flp_init(FLPSystem *sys, int n) {
    sys->process_count = n;
    sys->message_count = 0;
    sys->total_steps   = 0;
    sys->all_crashed   = false;
    for (int i = 0; i < n; i++) {
        sys->processes[i].process_id    = i;
        sys->processes[i].initial_value = i % 2;
        sys->processes[i].current_value = i % 2;
        sys->processes[i].decision      = FLP_UNDECIDED;
        sys->processes[i].crashed       = false;
        sys->processes[i].round         = 0;
        sys->processes[i].messages_sent = 0;
    }
}

static int flp_add_message(FLPSystem *sys, int from, int to,
                            int value, int round) {
    if (sys->message_count >= FLP_MAX_MSG_BUFFER) return -1;
    int idx = sys->message_count++;
    sys->messages[idx].from      = from;
    sys->messages[idx].to        = to;
    sys->messages[idx].value     = value;
    sys->messages[idx].round     = round;
    sys->messages[idx].delivered = false;
    return idx;
}

bool flp_deliver_message(FLPSystem *sys, int msg_index) {
    if (msg_index < 0 || msg_index >= sys->message_count) return false;
    if (sys->messages[msg_index].delivered) return true;
    FLPMessage *msg = &sys->messages[msg_index];
    FLPProcess *rcv = &sys->processes[msg->to];
    if (rcv->crashed) return false;
    msg->delivered = true;
    rcv->current_value = msg->value;
    rcv->round++;
    int value_0 = 0, value_1 = 0, active = 0;
    for (int i = 0; i < sys->process_count; i++) {
        if (sys->processes[i].crashed) continue;
        active++;
        if (sys->processes[i].current_value == 0) value_0++;
        else value_1++;
    }
    int q = flp_quorum(active);
    if (value_0 >= q) {
        for (int i = 0; i < sys->process_count; i++) {
            if (!sys->processes[i].crashed)
                sys->processes[i].decision = FLP_DECIDED_0;
        }
    } else if (value_1 >= q) {
        for (int i = 0; i < sys->process_count; i++) {
            if (!sys->processes[i].crashed)
                sys->processes[i].decision = FLP_DECIDED_1;
        }
    }
    return true;
}

void flp_crash_process(FLPSystem *sys, int pid) {
    if (pid < 0 || pid >= sys->process_count) return;
    sys->processes[pid].crashed = true;
    int alive = 0;
    for (int i = 0; i < sys->process_count; i++) {
        if (!sys->processes[i].crashed) alive++;
    }
    if (alive == 0) sys->all_crashed = true;
}

bool flp_is_bivalent(const FLPSystem *sys) {
    int undelivered[FLP_MAX_MSG_BUFFER];
    int ucount = 0;
    for (int i = 0; i < sys->message_count; i++) {
        if (!sys->messages[i].delivered) undelivered[ucount++] = i;
    }
    if (ucount == 0) {
        bool saw_0 = false, saw_1 = false;
        for (int i = 0; i < sys->process_count; i++) {
            if (sys->processes[i].crashed) continue;
            if (sys->processes[i].decision == FLP_DECIDED_0) saw_0 = true;
            if (sys->processes[i].decision == FLP_DECIDED_1) saw_1 = true;
        }
        return !(saw_0 && !saw_1) && !(!saw_0 && saw_1);
    }
    int reachable_0 = 0, reachable_1 = 0;
    FLPSystem copy;
    memcpy(&copy, sys, sizeof(FLPSystem));
    for (int i = 0; i < ucount; i++)
        flp_deliver_message(&copy, undelivered[i]);
    for (int i = 0; i < copy.process_count; i++) {
        if (!copy.processes[i].crashed) {
            if (copy.processes[i].decision == FLP_DECIDED_0) reachable_0 = 1;
            if (copy.processes[i].decision == FLP_DECIDED_1) reachable_1 = 1;
        }
    }
    memcpy(&copy, sys, sizeof(FLPSystem));
    for (int i = ucount - 1; i >= 0; i--)
        flp_deliver_message(&copy, undelivered[i]);
    for (int i = 0; i < copy.process_count; i++) {
        if (!copy.processes[i].crashed) {
            if (copy.processes[i].decision == FLP_DECIDED_0) reachable_0 = 1;
            if (copy.processes[i].decision == FLP_DECIDED_1) reachable_1 = 1;
        }
    }
    return reachable_0 && reachable_1;
}

FLPDecision flp_run_adversary(FLPSystem *sys, int max_steps,
                               FLPAdversaryAction *actions,
                               int action_count) {
    int action_idx = 0;
    for (int step = 0; step < max_steps; step++) {
        if (sys->all_crashed) return FLP_UNDECIDED;
        for (int i = 0; i < sys->process_count; i++) {
            if (!sys->processes[i].crashed) {
                if (sys->processes[i].decision == FLP_DECIDED_0) return FLP_DECIDED_0;
                if (sys->processes[i].decision == FLP_DECIDED_1) return FLP_DECIDED_1;
            }
        }
        if (action_idx >= action_count) break;
        FLPAdversaryAction act = actions[action_idx++];
        sys->total_steps++;
        switch (act) {
        case FLP_CRASH_AFTER_SEND:
            for (int i = 0; i < sys->process_count; i++)
                if (i != 0 && !sys->processes[i].crashed)
                    flp_add_message(sys, 0, i, sys->processes[0].current_value, step);
            flp_crash_process(sys, 0);
            break;
        case FLP_DELAY_MESSAGE:
            for (int i = 0; i < sys->process_count; i++)
                if (!sys->processes[i].crashed)
                    for (int j = 0; j < sys->process_count; j++)
                        if (i != j && !sys->processes[j].crashed)
                            flp_add_message(sys, i, j, sys->processes[i].current_value, step);
            break;
        case FLP_DELIVER_MESSAGE:
            for (int i = 0; i < sys->message_count; i++)
                if (!sys->messages[i].delivered) {
                    flp_deliver_message(sys, i);
                    break;
                }
            break;
        case FLP_FLIP_COIN:
            if (step % 3 == 0)
                flp_crash_process(sys, step % sys->process_count);
            else
                for (int i = 0; i < sys->message_count; i++)
                    if (!sys->messages[i].delivered) {
                        flp_deliver_message(sys, i);
                        break;
                    }
            break;
        }
    }
    return FLP_UNDECIDED;
}

CAPClassification cap_classify(const CAPSystem *sys) {
    if (!sys->partition_active)
        return sys->consistency_enforced ? CAP_CP : CAP_AP;
    if (sys->consistency_enforced && sys->availability_maintained)
        return CAP_CA_IMPOSSIBLE;
    if (sys->consistency_enforced) return CAP_CP;
    return CAP_AP;
}

int cap_partition_experiment(int node_count, int partition_size,
                              bool prioritize_consistency,
                              int *total_operations,
                              int *failed_operations) {
    if (partition_size <= 0 || partition_size >= node_count) {
        *total_operations = 0;
        *failed_operations = 0;
        return 0;
    }
    int majority   = node_count - partition_size;
    int consistent = 0, total = 0, failed = 0;
    for (int op = 0; op < 20; op++) {
        total++;
        int target_node = op % node_count;
        if (target_node >= majority) {
            if (prioritize_consistency) failed++;
            else consistent++;
        } else {
            consistent++;
        }
    }
    *total_operations  = total;
    *failed_operations = failed;
    return consistent;
}

int two_generals_simulate(int max_rounds, double message_loss_prob) {
    if (max_rounds <= 0) max_rounds = 1;
    if (message_loss_prob < 0.0) message_loss_prob = 0.0;
    if (message_loss_prob > 1.0) message_loss_prob = 1.0;
    int messages_sent = 0, messages_lost = 0;
    bool a_committed = false, b_committed = false;
    messages_sent++;
    if ((double)(messages_sent * 17 % 100) / 100.0 < message_loss_prob)
        return 0;
    b_committed = true;
    for (int r = 1; r < max_rounds; r++) {
        messages_sent++;
        if ((double)((messages_sent * 31 + r * 7) % 100) / 100.0 < message_loss_prob) {
            messages_lost++;
            if (r % 2 == 1) a_committed = false;
            else b_committed = false;
            continue;
        }
        if (r % 2 == 1) a_committed = true;
        else b_committed = true;
    }
    if (!a_committed || !b_committed) return 0;
    int confidence = 100 - (int)(50.0 / (double)max_rounds);
    if (confidence < 10) confidence = 10;
    if (messages_lost > 0) confidence -= messages_lost * 10;
    if (confidence < 0) confidence = 0;
    return confidence;
}

int quorum_intersection_min(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int q = n / 2 + 1;
    return 2 * q - n;
}

int quorum_pair_intersection(uint64_t quorum_a, uint64_t quorum_b, int n) {
    uint64_t intersection = quorum_a & quorum_b;
    if (intersection == 0) return -1;
    for (int i = 0; i < n && i < 64; i++)
        if (intersection & ((uint64_t)1 << i)) return i;
    return -1;
}

int quorum_enumerate_all(int n, uint64_t *quorum_masks, int max_quorums) {
    if (n <= 0 || n > 64 || !quorum_masks) return 0;
    int q = n / 2 + 1, count = 0;
    uint64_t max_mask = ((uint64_t)1 << n) - 1;
    uint64_t subset = ((uint64_t)1 << q) - 1;
    while (subset <= max_mask && count < max_quorums) {
        quorum_masks[count++] = subset;
        uint64_t c = subset & (~subset + 1);
        uint64_t r = subset + c;
        uint64_t diff = (r ^ subset) / (c * 4);
        subset = r | diff;
    }
    return count;
}

int byzantine_quorum_threshold(int f) {
    if (f < 0) return 0;
    return 3 * f + 1;
}

int crash_fault_quorum_threshold(int f) {
    if (f < 0) return 0;
    return 2 * f + 1;
}
