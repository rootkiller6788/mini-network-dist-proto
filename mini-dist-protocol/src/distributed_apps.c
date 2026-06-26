#include "distributed_apps.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Two-Phase Commit (2PC)
 *
 * Implements the classic 2PC protocol for distributed transactions.
 * Phase 1 (Prepare): Coordinator asks all participants to vote.
 * Phase 2 (Commit/Abort): Based on votes, coordinator decides.
 *
 * References:
 *   Gray (1978) "Notes on Database Operating Systems"
 *   Mohan, Lindsay, Obermarck (1986) "Transaction Management
 *     in the R* Distributed Database Management System"
 * ================================================================ */

const char *dtx_state_name(DTXState state) {
    switch (state) {
        case DTX_INIT:      return "INIT";
        case DTX_PREPARING: return "PREPARING";
        case DTX_PREPARED:  return "PREPARED";
        case DTX_COMMITTING:return "COMMITTING";
        case DTX_ABORTING:  return "ABORTING";
        case DTX_COMMITTED: return "COMMITTED";
        case DTX_ABORTED:   return "ABORTED";
        default:            return "UNKNOWN";
    }
}

void dtx_init(DTXCoordinator *dtx, int n, int txn_id) {
    dtx->participant_count  = n;
    dtx->transaction_id     = txn_id;
    dtx->coordinator_state  = DTX_INIT;
    for (int i = 0; i < n; i++) {
        dtx->participants[i].participant_id = i;
        dtx->participants[i].state          = DTX_INIT;
        dtx->participants[i].vote_yes       = false;
        dtx->participants[i].data_value     = 0;
        dtx->participants[i].prepared_value = 0;
    }
}

bool dtx_set_value(DTXCoordinator *dtx, int pid, int value) {
    if (pid < 0 || pid >= dtx->participant_count) return false;
    if (dtx->coordinator_state != DTX_INIT) return false;
    dtx->participants[pid].data_value = value;
    return true;
}

int dtx_get_value(const DTXCoordinator *dtx, int pid) {
    if (pid < 0 || pid >= dtx->participant_count) return -1;
    return dtx->participants[pid].data_value;
}

bool dtx_prepare(DTXCoordinator *dtx) {
    if (dtx->coordinator_state != DTX_INIT) return false;
    dtx->coordinator_state = DTX_PREPARING;

    int yes_votes = 0;
    for (int i = 0; i < dtx->participant_count; i++) {
        dtx->participants[i].state = DTX_PREPARING;
        /* Each participant votes YES if it can complete the write.
         * In this model, a participant votes NO if its value is 0
         * (simulating a constraint violation). */
        if (dtx->participants[i].data_value != 0) {
            dtx->participants[i].vote_yes       = true;
            dtx->participants[i].prepared_value = dtx->participants[i].data_value;
            dtx->participants[i].state          = DTX_PREPARED;
            yes_votes++;
        } else {
            dtx->participants[i].vote_yes = false;
        }
    }

    /* Unanimous consent required for commit in standard 2PC */
    if (yes_votes == dtx->participant_count) {
        dtx->coordinator_state = DTX_PREPARED;
        return true;
    }
    return false;
}

bool dtx_commit(DTXCoordinator *dtx) {
    if (dtx->coordinator_state != DTX_PREPARED) return false;

    dtx->coordinator_state = DTX_COMMITTING;
    for (int i = 0; i < dtx->participant_count; i++) {
        if (dtx->participants[i].vote_yes) {
            dtx->participants[i].state      = DTX_COMMITTED;
            dtx->participants[i].data_value = dtx->participants[i].prepared_value;
        } else {
            dtx->participants[i].state = DTX_ABORTED;
        }
    }
    dtx->coordinator_state = DTX_COMMITTED;
    return true;
}

void dtx_abort(DTXCoordinator *dtx) {
    dtx->coordinator_state = DTX_ABORTING;
    for (int i = 0; i < dtx->participant_count; i++) {
        dtx->participants[i].state = DTX_ABORTED;
        /* Rollback: restore pre-transaction state (value=0 in this model) */
        dtx->participants[i].data_value     = 0;
        dtx->participants[i].prepared_value = 0;
    }
    dtx->coordinator_state = DTX_ABORTED;
}


/* ================================================================
 * Distributed Key-Value Store with Version Vectors
 *
 * Implements causal consistency using version vectors.
 * Each replica maintains a vector clock tracking writes from
 * each replica. On read, if multiple versions exist, the client
 * resolves conflicts.
 *
 * References:
 *   Dynamo (DeCandia et al., SOSP 2007)
 *   Bayou (Terry et al., SOSP 1995)
 *   Version Vectors (Parker et al., 1983)
 * ================================================================ */

void dkv_init(DKVStore *store, int n) {
    store->replica_count = n;
    store->quorum_size   = n / 2 + 1;
    for (int i = 0; i < n; i++) {
        store->replicas[i].replica_id  = i;
        store->replicas[i].entry_count = 0;
        for (int j = 0; j < DKV_MAX_REPLICAS; j++) {
            store->replicas[i].version_vector[j] = 0;
        }
    }
}

static DKVEntry *dkv_find_entry(DKVReplica *rep, int key) {
    for (int i = 0; i < rep->entry_count; i++) {
        if (rep->entries[i].key == key) return &rep->entries[i];
    }
    return NULL;
}

bool dkv_put(DKVStore *store, int replica_id, int key, int value) {
    if (replica_id < 0 || replica_id >= store->replica_count) return false;

    DKVReplica *rep = &store->replicas[replica_id];
    DKVEntry *entry = dkv_find_entry(rep, key);

    if (entry) {
        entry->value = value;
    } else {
        if (rep->entry_count >= DKV_MAX_KEYS) return false;
        entry = &rep->entries[rep->entry_count++];
        entry->key   = key;
        entry->value = value;
        for (int j = 0; j < DKV_MAX_REPLICAS; j++)
            entry->version.version_vector[j] = 0;
    }

    /* Update local version vector: increment our own counter */
    rep->version_vector[replica_id]++;
    /* Copy the updated version to the entry */
    for (int j = 0; j < store->replica_count; j++)
        entry->version.version_vector[j] = rep->version_vector[j];

    return true;
}

bool dkv_get(const DKVStore *store, int replica_id, int key,
             int *value, DKVVersion *version) {
    if (replica_id < 0 || replica_id >= store->replica_count) return false;

    const DKVReplica *rep = &store->replicas[replica_id];
    for (int i = 0; i < rep->entry_count; i++) {
        if (rep->entries[i].key == key) {
            *value   = rep->entries[i].value;
            *version = rep->entries[i].version;
            return true;
        }
    }
    return false;
}

bool dkv_replicate(DKVStore *store, int from_replica, int to_replica,
                   int key) {
    if (from_replica < 0 || from_replica >= store->replica_count) return false;
    if (to_replica   < 0 || to_replica   >= store->replica_count) return false;
    if (from_replica == to_replica) return true;

    DKVReplica *src = &store->replicas[from_replica];
    DKVReplica *dst = &store->replicas[to_replica];
    DKVEntry *src_entry = dkv_find_entry(src, key);
    if (!src_entry) return false;

    DKVEntry *dst_entry = dkv_find_entry(dst, key);
    if (!dst_entry) {
        if (dst->entry_count >= DKV_MAX_KEYS) return false;
        dst_entry = &dst->entries[dst->entry_count++];
        dst_entry->key = key;
    }

    /* Apply only if source has a newer version (causal ordering) */
    if (!dkv_causal_past(&dst_entry->version, &src_entry->version,
                         store->replica_count)) {
        /* src version is concurrent or newer: merge and apply */
        dst_entry->value = src_entry->value;
        dkv_merge_versions(&dst_entry->version, &src_entry->version,
                           store->replica_count);
        dkv_merge_versions((DKVVersion *)&dst->version_vector,
                           &src_entry->version,
                           store->replica_count);
    }
    return true;
}

int dkv_quorum_read(DKVStore *store, int key, int *value) {
    /* Read from all replicas and return the value with the most
     * recent version (highest sum of version vector components).
     * This implements a simple quorum read for eventual consistency. */
    int best_replica = -1;
    int best_sum     = -1;
    int best_val     = 0;

    for (int i = 0; i < store->replica_count; i++) {
        int val;
        DKVVersion ver;
        if (dkv_get(store, i, key, &val, &ver)) {
            int sum = 0;
            for (int j = 0; j < store->replica_count; j++)
                sum += ver.version_vector[j];
            if (sum > best_sum) {
                best_sum     = sum;
                best_val     = val;
                best_replica = i;
            }
        }
    }
    if (best_replica >= 0 && value) *value = best_val;
    return best_replica;
}

bool dkv_causal_past(const DKVVersion *a, const DKVVersion *b, int n) {
    /* Check if version a causally precedes version b.
     * a -> b iff for all i: a[i] <= b[i] AND exists j: a[j] < b[j] */
    bool strictly_less = false;
    for (int i = 0; i < n; i++) {
        if (a->version_vector[i] > b->version_vector[i]) return false;
        if (a->version_vector[i] < b->version_vector[i]) strictly_less = true;
    }
    return strictly_less;
}

void dkv_merge_versions(DKVVersion *dst, const DKVVersion *src, int n) {
    /* Element-wise maximum for version vector merge (lattice join).
     * After merge, dst dominates both original versions. */
    for (int i = 0; i < n; i++) {
        if (src->version_vector[i] > dst->version_vector[i])
            dst->version_vector[i] = src->version_vector[i];
    }
}

void dkv_print_replica(const DKVReplica *rep) {
    printf("  Replica %d [", rep->replica_id);
    for (int j = 0; j < DKV_MAX_REPLICAS; j++) {
        if (rep->version_vector[j] > 0)
            printf("%s%d:%d", j > 0 ? "," : "", j, rep->version_vector[j]);
    }
    printf("] entries=%d\n", rep->entry_count);
    for (int i = 0; i < rep->entry_count; i++) {
        printf("    key=%d val=%d ver=[", rep->entries[i].key,
               rep->entries[i].value);
        for (int j = 0; j < DKV_MAX_REPLICAS; j++) {
            if (rep->entries[i].version.version_vector[j] > 0)
                printf("%s%d:%d", j > 0 ? "," : "", j,
                       rep->entries[i].version.version_vector[j]);
        }
        printf("]\n");
    }
}


/* ================================================================
 * Distributed Lock Manager (Lease-based)
 *
 * Lock manager using Lamport logical clocks for ordering and
 * time-based leases for deadlock prevention. Clients must
 * renew leases before expiry.
 *
 * References:
 *   Lamport (1978) "Time, Clocks, and the Ordering of Events"
 *   Chubby (Burrows, OSDI 2006)
 *   ZooKeeper (Hunt et al., USENIX ATC 2010)
 * ================================================================ */

void dlm_init(DLMManager *dlm, int num_clients) {
    dlm->client_count  = num_clients;
    dlm->lock_count    = 0;
    dlm->global_clock  = 0;
    dlm->current_time_ms = 0;

    for (int i = 0; i < num_clients; i++) {
        dlm->clients[i].client_id     = i;
        dlm->clients[i].logical_clock = 0;
        dlm->clients[i].held_count    = 0;
        for (int j = 0; j < DLM_MAX_LOCKS; j++)
            dlm->clients[i].held_locks[j] = -1;
    }

    for (int i = 0; i < DLM_MAX_LOCKS; i++) {
        dlm->locks[i].lock_id        = i;
        dlm->locks[i].owner_id       = -1;
        dlm->locks[i].lease_expiry_ms = 0;
        dlm->locks[i].logical_clock  = 0;
        dlm->locks[i].granted        = false;
    }
    dlm->lock_count = (num_clients > 0) ? num_clients : DLM_MAX_LOCKS;
}

static void dlm_update_clock(DLMManager *dlm, uint64_t received_clock) {
    if (received_clock > dlm->global_clock)
        dlm->global_clock = received_clock;
    dlm->global_clock++;
}

bool dlm_acquire(DLMManager *dlm, int client_id, int lock_id,
                 uint64_t ttl_ms) {
    if (client_id < 0 || client_id >= dlm->client_count) return false;
    if (lock_id   < 0 || lock_id   >= dlm->lock_count)   return false;
    if (ttl_ms == 0) ttl_ms = DLM_DEFAULT_TTL_MS;

    DLMLock *lock  = &dlm->locks[lock_id];
    DLMClient *cli = &dlm->clients[client_id];

    dlm_update_clock(dlm, cli->logical_clock);

    /* Check if lock is available or held by us */
    if (lock->granted && lock->owner_id != client_id) {
        /* Check if previous lease has expired */
        if (dlm->current_time_ms < lock->lease_expiry_ms) {
            return false;  /* Lock still valid for another client */
        }
        /* Lease expired: reclaim lock */
        DLMClient *old_owner = &dlm->clients[lock->owner_id];
        for (int i = 0; i < old_owner->held_count; i++) {
            if (old_owner->held_locks[i] == lock_id) {
                old_owner->held_locks[i] =
                    old_owner->held_locks[--old_owner->held_count];
                break;
            }
        }
    }

    /* Grant the lock */
    lock->granted         = true;
    lock->owner_id        = client_id;
    lock->lease_expiry_ms = dlm->current_time_ms + ttl_ms;
    lock->logical_clock   = dlm->global_clock;

    /* Track in client */
    if (cli->held_count < DLM_MAX_LOCKS) {
        cli->held_locks[cli->held_count++] = lock_id;
    }
    cli->logical_clock = dlm->global_clock;

    return true;
}

bool dlm_release(DLMManager *dlm, int client_id, int lock_id) {
    if (client_id < 0 || client_id >= dlm->client_count) return false;
    if (lock_id   < 0 || lock_id   >= dlm->lock_count)   return false;

    DLMLock *lock = &dlm->locks[lock_id];
    if (!lock->granted || lock->owner_id != client_id) return false;

    lock->granted  = false;
    lock->owner_id = -1;

    DLMClient *cli = &dlm->clients[client_id];
    for (int i = 0; i < cli->held_count; i++) {
        if (cli->held_locks[i] == lock_id) {
            cli->held_locks[i] = cli->held_locks[--cli->held_count];
            break;
        }
    }
    return true;
}

bool dlm_renew(DLMManager *dlm, int client_id, int lock_id,
               uint64_t ttl_ms) {
    if (ttl_ms == 0) ttl_ms = DLM_DEFAULT_TTL_MS;

    DLMLock *lock = &dlm->locks[lock_id];
    if (!lock->granted || lock->owner_id != client_id) return false;

    lock->lease_expiry_ms = dlm->current_time_ms + ttl_ms;
    return true;
}

void dlm_tick(DLMManager *dlm, uint64_t delta_ms) {
    dlm->current_time_ms += delta_ms;

    /* Expire stale leases */
    for (int i = 0; i < dlm->lock_count; i++) {
        if (dlm->locks[i].granted &&
            dlm->current_time_ms >= dlm->locks[i].lease_expiry_ms) {
            dlm_release(dlm, dlm->locks[i].owner_id, i);
        }
    }
}

bool dlm_is_locked(const DLMManager *dlm, int lock_id) {
    if (lock_id < 0 || lock_id >= dlm->lock_count) return false;
    if (!dlm->locks[lock_id].granted) return false;
    return dlm->current_time_ms < dlm->locks[lock_id].lease_expiry_ms;
}

int dlm_lock_owner(const DLMManager *dlm, int lock_id) {
    if (lock_id < 0 || lock_id >= dlm->lock_count) return -1;
    if (!dlm->locks[lock_id].granted) return -1;
    if (dlm->current_time_ms >= dlm->locks[lock_id].lease_expiry_ms)
        return -1;
    return dlm->locks[lock_id].owner_id;
}

void dlm_print_state(const DLMManager *dlm) {
    printf("=== DLM (clock=%llu, time=%llu ms) ===\n",
           (unsigned long long)dlm->global_clock,
           (unsigned long long)dlm->current_time_ms);
    for (int i = 0; i < dlm->lock_count; i++) {
        const DLMLock *l = &dlm->locks[i];
        printf("  Lock %d: %s owner=%d expiry=%llu lc=%llu\n",
               l->lock_id,
               dlm_is_locked(dlm, i) ? "HELD" : "FREE",
               l->owner_id,
               (unsigned long long)l->lease_expiry_ms,
               (unsigned long long)l->logical_clock);
    }
}
