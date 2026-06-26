#ifndef TWO_PC_H
#define TWO_PC_H

#include <stdbool.h>
#include <stdint.h>

#define TPC_MAX_PARTICIPANTS 8
#define TPC_TIMEOUT_MS 5000
#define TPC_RETRY_COUNT 3

typedef enum {
    TPC_INIT,
    TPC_READY,
    TPC_COMMITTED,
    TPC_ABORTED
} TPCState;

typedef enum {
    TPC_VOTE_YES,
    TPC_VOTE_NO,
    TPC_VOTE_PENDING
} TPCVote;

typedef enum {
    TPC_PHASE_PREPARE,
    TPC_PHASE_COMMIT,
    TPC_PHASE_ABORT
} TPCPhase;

typedef enum {
    THREEPC_PHASE_PREPARE,
    THREEPC_PHASE_PRECOMMIT,
    THREEPC_PHASE_COMMIT,
    THREEPC_PHASE_ABORT
} ThreePCPhase;

typedef struct {
    int32_t id;
    TPCState state;
    TPCVote vote;
    bool prepared;
    char name[32];
} TPCParticipant;

typedef struct {
    int32_t txn_id;
    TPCParticipant participants[TPC_MAX_PARTICIPANTS];
    int32_t participant_count;
    TPCPhase phase;
    bool timed_out;
} TPCCoordinator;

typedef struct {
    int32_t txn_id;
    TPCParticipant participants[TPC_MAX_PARTICIPANTS];
    int32_t participant_count;
    ThreePCPhase phase;
    bool timed_out;
} ThreePCCoordinator;

void tpc_participant_init(TPCParticipant *p, int32_t id, const char *name);
void tpc_coordinator_init(TPCCoordinator *c, int32_t txn_id);
void tpc_coordinator_add_participant(TPCCoordinator *c, TPCParticipant *p);
bool tpc_coordinator_prepare(TPCCoordinator *c);
TPCVote tpc_participant_vote(TPCParticipant *p, bool can_commit);
bool tpc_coordinator_commit(TPCCoordinator *c);
void tpc_coordinator_abort(TPCCoordinator *c);
bool tpc_handle_timeout(TPCCoordinator *c);
const char *tpc_state_str(TPCState s);
const char *tpc_phase_str(TPCPhase p);
void tpc_print_state(TPCCoordinator *c);

void threepc_coordinator_init(ThreePCCoordinator *c, int32_t txn_id);
void threepc_coordinator_add_participant(ThreePCCoordinator *c, TPCParticipant *p);
bool threepc_prepare(ThreePCCoordinator *c);
bool threepc_precommit(ThreePCCoordinator *c);
bool threepc_commit(ThreePCCoordinator *c);
void threepc_abort(ThreePCCoordinator *c);
const char *threepc_phase_str(ThreePCPhase p);
void threepc_print_state(ThreePCCoordinator *c);

#endif
