#ifndef DISTRIBUTED_APPS_H
#define DISTRIBUTED_APPS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ================================================================
 * L7: Applications - Distributed Systems in Practice
 *
 * Two-Phase Commit (2PC): Distributed Transactions
 * Distributed KV Store: Version Vectors + Causal Consistency
 * Distributed Lock Manager: Lease-based Mutual Exclusion
 * ================================================================ */

/* --- Two-Phase Commit (2PC) --- */

#define DTX_MAX_PARTICIPANTS 8

typedef enum {
    DTX_INIT,
    DTX_PREPARING,
    DTX_PREPARED,
    DTX_COMMITTING,
    DTX_ABORTING,
    DTX_COMMITTED,
    DTX_ABORTED
} DTXState;

typedef struct {
    int       participant_id;
    DTXState  state;
    bool      vote_yes;
    int       data_value;
    int       prepared_value;
} DTXParticipant;

typedef struct {
    DTXParticipant participants[DTX_MAX_PARTICIPANTS];
    int            participant_count;
    DTXState       coordinator_state;
    int            transaction_id;
} DTXCoordinator;

void dtx_init(DTXCoordinator *dtx, int n, int txn_id);
bool dtx_prepare(DTXCoordinator *dtx);
bool dtx_commit(DTXCoordinator *dtx);
void dtx_abort(DTXCoordinator *dtx);
bool dtx_set_value(DTXCoordinator *dtx, int pid, int value);
int  dtx_get_value(const DTXCoordinator *dtx, int pid);
const char *dtx_state_name(DTXState state);

/* --- Distributed KV Store --- */

#define DKV_MAX_REPLICAS    8
#define DKV_MAX_KEYS       32
#define DKV_MAX_VERSIONS    8

typedef struct {
    int replica_id;
    int version_vector[DKV_MAX_REPLICAS];
} DKVVersion;

typedef struct {
    int       key;
    int       value;
    DKVVersion version;
} DKVEntry;

typedef struct {
    DKVEntry entries[DKV_MAX_KEYS];
    int      entry_count;
    int      replica_id;
    int      version_vector[DKV_MAX_REPLICAS];
} DKVReplica;

typedef struct {
    DKVReplica replicas[DKV_MAX_REPLICAS];
    int        replica_count;
    int        quorum_size;
} DKVStore;

void dkv_init(DKVStore *store, int n);
bool dkv_put(DKVStore *store, int replica_id, int key, int value);
bool dkv_get(const DKVStore *store, int replica_id, int key,
             int *value, DKVVersion *version);
bool dkv_replicate(DKVStore *store, int from_replica, int to_replica, int key);
int  dkv_quorum_read(DKVStore *store, int key, int *value);
bool dkv_causal_past(const DKVVersion *a, const DKVVersion *b, int n);
void dkv_merge_versions(DKVVersion *dst, const DKVVersion *src, int n);
void dkv_print_replica(const DKVReplica *rep);

/* --- Distributed Lock Manager --- */

#define DLM_MAX_CLIENTS    8
#define DLM_MAX_LOCKS     16
#define DLM_DEFAULT_TTL_MS 5000

typedef struct {
    int       lock_id;
    int       owner_id;
    uint64_t  lease_expiry_ms;
    uint64_t  logical_clock;
    bool      granted;
} DLMLock;

typedef struct {
    int       client_id;
    uint64_t  logical_clock;
    int       held_locks[DLM_MAX_LOCKS];
    int       held_count;
} DLMClient;

typedef struct {
    DLMLock   locks[DLM_MAX_LOCKS];
    int       lock_count;
    DLMClient clients[DLM_MAX_CLIENTS];
    int       client_count;
    uint64_t  global_clock;
    uint64_t  current_time_ms;
} DLMManager;

void dlm_init(DLMManager *dlm, int num_clients);
bool dlm_acquire(DLMManager *dlm, int client_id, int lock_id,
                 uint64_t ttl_ms);
bool dlm_release(DLMManager *dlm, int client_id, int lock_id);
bool dlm_renew(DLMManager *dlm, int client_id, int lock_id,
               uint64_t ttl_ms);
void dlm_tick(DLMManager *dlm, uint64_t delta_ms);
bool dlm_is_locked(const DLMManager *dlm, int lock_id);
int  dlm_lock_owner(const DLMManager *dlm, int lock_id);
void dlm_print_state(const DLMManager *dlm);

#endif
