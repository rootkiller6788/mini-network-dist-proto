#include "two_pc.h"

#include <stdio.h>
#include <string.h>

const char *tpc_state_str(TPCState s) {
    switch (s) {
    case TPC_INIT:      return "INIT";
    case TPC_READY:     return "READY";
    case TPC_COMMITTED: return "COMMITTED";
    case TPC_ABORTED:   return "ABORTED";
    default:            return "UNKNOWN";
    }
}

const char *tpc_phase_str(TPCPhase p) {
    switch (p) {
    case TPC_PHASE_PREPARE: return "PREPARE";
    case TPC_PHASE_COMMIT:  return "COMMIT";
    case TPC_PHASE_ABORT:   return "ABORT";
    default:                return "UNKNOWN";
    }
}

const char *threepc_phase_str(ThreePCPhase p) {
    switch (p) {
    case THREEPC_PHASE_PREPARE:   return "PREPARE";
    case THREEPC_PHASE_PRECOMMIT: return "PRECOMMIT";
    case THREEPC_PHASE_COMMIT:    return "COMMIT";
    case THREEPC_PHASE_ABORT:     return "ABORT";
    default:                      return "UNKNOWN";
    }
}

void tpc_participant_init(TPCParticipant *p, int32_t id, const char *name) {
    p->id = id;
    p->state = TPC_INIT;
    p->vote = TPC_VOTE_PENDING;
    p->prepared = false;
    strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = '\0';
}

void tpc_coordinator_init(TPCCoordinator *c, int32_t txn_id) {
    c->txn_id = txn_id;
    c->participant_count = 0;
    c->phase = TPC_PHASE_PREPARE;
    c->timed_out = false;
    memset(c->participants, 0, sizeof(c->participants));
}

void tpc_coordinator_add_participant(TPCCoordinator *c, TPCParticipant *p) {
    if (c->participant_count >= TPC_MAX_PARTICIPANTS) return;
    c->participants[c->participant_count] = *p;
    c->participant_count++;
}

bool tpc_coordinator_prepare(TPCCoordinator *c) {
    if (c->timed_out) {
        c->phase = TPC_PHASE_ABORT;
        return false;
    }
    c->phase = TPC_PHASE_PREPARE;
    int32_t yes_count = 0;
    int32_t no_count = 0;
    for (int32_t i = 0; i < c->participant_count; i++) {
        TPCParticipant *p = &c->participants[i];
        if (p->vote == TPC_VOTE_YES) {
            p->state = TPC_READY;
            p->prepared = true;
            yes_count++;
        } else if (p->vote == TPC_VOTE_NO) {
            p->state = TPC_ABORTED;
            no_count++;
        } else {
            p->state = TPC_READY;
            p->prepared = true;
            p->vote = TPC_VOTE_YES;
            yes_count++;
        }
    }
    if (no_count > 0) {
        c->phase = TPC_PHASE_ABORT;
        return false;
    }
    return yes_count == c->participant_count;
}

TPCVote tpc_participant_vote(TPCParticipant *p, bool can_commit) {
    if (can_commit) {
        p->vote = TPC_VOTE_YES;
        p->state = TPC_READY;
        return TPC_VOTE_YES;
    }
    p->vote = TPC_VOTE_NO;
    p->state = TPC_ABORTED;
    return TPC_VOTE_NO;
}

bool tpc_coordinator_commit(TPCCoordinator *c) {
    if (c->phase != TPC_PHASE_PREPARE) return false;
    if (c->timed_out) {
        tpc_coordinator_abort(c);
        return false;
    }
    int32_t all_yes = 0;
    for (int32_t i = 0; i < c->participant_count; i++) {
        if (c->participants[i].vote == TPC_VOTE_YES) all_yes++;
    }
    if (all_yes != c->participant_count || c->participant_count == 0) {
        tpc_coordinator_abort(c);
        return false;
    }
    c->phase = TPC_PHASE_COMMIT;
    for (int32_t i = 0; i < c->participant_count; i++) {
        c->participants[i].state = TPC_COMMITTED;
    }
    return true;
}

void tpc_coordinator_abort(TPCCoordinator *c) {
    c->phase = TPC_PHASE_ABORT;
    for (int32_t i = 0; i < c->participant_count; i++) {
        c->participants[i].state = TPC_ABORTED;
    }
}

bool tpc_handle_timeout(TPCCoordinator *c) {
    c->timed_out = true;
    tpc_coordinator_abort(c);
    return false;
}

void tpc_print_state(TPCCoordinator *c) {
    printf("=== 2PC Transaction %d ===\n", c->txn_id);
    printf("  Phase: %s | Timeout: %s\n",
           tpc_phase_str(c->phase), c->timed_out ? "YES" : "NO");
    printf("  Participants (%d):\n", c->participant_count);
    for (int32_t i = 0; i < c->participant_count; i++) {
        TPCParticipant *p = &c->participants[i];
        printf("    [%d] %s: state=%s vote=%s prepared=%s\n",
               p->id, p->name, tpc_state_str(p->state),
               p->vote == TPC_VOTE_YES ? "YES" :
               p->vote == TPC_VOTE_NO ? "NO" : "PENDING",
               p->prepared ? "true" : "false");
    }
}

void threepc_coordinator_init(ThreePCCoordinator *c, int32_t txn_id) {
    c->txn_id = txn_id;
    c->participant_count = 0;
    c->phase = THREEPC_PHASE_PREPARE;
    c->timed_out = false;
    memset(c->participants, 0, sizeof(c->participants));
}

void threepc_coordinator_add_participant(ThreePCCoordinator *c, TPCParticipant *p) {
    if (c->participant_count >= TPC_MAX_PARTICIPANTS) return;
    c->participants[c->participant_count] = *p;
    c->participant_count++;
}

bool threepc_prepare(ThreePCCoordinator *c) {
    c->phase = THREEPC_PHASE_PREPARE;
    int32_t yes = 0;
    for (int32_t i = 0; i < c->participant_count; i++) {
        TPCParticipant *p = &c->participants[i];
        if (p->vote == TPC_VOTE_YES || p->vote == TPC_VOTE_PENDING) {
            p->vote = TPC_VOTE_YES;
            p->state = TPC_READY;
            yes++;
        } else {
            p->state = TPC_ABORTED;
            threepc_abort(c);
            return false;
        }
    }
    return yes == c->participant_count && c->participant_count > 0;
}

bool threepc_precommit(ThreePCCoordinator *c) {
    if (c->phase != THREEPC_PHASE_PREPARE) return false;
    c->phase = THREEPC_PHASE_PRECOMMIT;
    for (int32_t i = 0; i < c->participant_count; i++) {
        if (c->participants[i].vote != TPC_VOTE_YES) {
            threepc_abort(c);
            return false;
        }
        c->participants[i].prepared = true;
    }
    return true;
}

bool threepc_commit(ThreePCCoordinator *c) {
    if (c->phase != THREEPC_PHASE_PRECOMMIT) return false;
    c->phase = THREEPC_PHASE_COMMIT;
    for (int32_t i = 0; i < c->participant_count; i++) {
        c->participants[i].state = TPC_COMMITTED;
    }
    return true;
}

void threepc_abort(ThreePCCoordinator *c) {
    c->phase = THREEPC_PHASE_ABORT;
    for (int32_t i = 0; i < c->participant_count; i++) {
        c->participants[i].state = TPC_ABORTED;
    }
}

void threepc_print_state(ThreePCCoordinator *c) {
    printf("=== 3PC Transaction %d ===\n", c->txn_id);
    printf("  Phase: %s | Timeout: %s\n",
           threepc_phase_str(c->phase), c->timed_out ? "YES" : "NO");
    printf("  Participants (%d):\n", c->participant_count);
    for (int32_t i = 0; i < c->participant_count; i++) {
        TPCParticipant *p = &c->participants[i];
        printf("    [%d] %s: state=%s vote=%s\n",
               p->id, p->name, tpc_state_str(p->state),
               p->vote == TPC_VOTE_YES ? "YES" : "NO");
    }
}
