/* occ.c -- Optimistic Concurrency Control (Kung & Robinson 1981)
 *
 * OCC assumes conflicts are rare and delays validation until commit time.
 * Transaction phases:
 *   1. READ:  perform all reads, buffer writes locally
 *   2. VALIDATE: check for conflicts with concurrent transactions
 *   3. WRITE:  make buffered writes visible
 *
 * Theorem (Serializability of OCC - Kung & Robinson 1981):
 *   OCC guarantees serializability if the validation phase checks:
 *   - Backward validation: for all committed txns that overlapped with this
 *     txn's read phase, this txn's read set does not intersect their write set.
 *   - Forward validation: for all active txns, this txn's write set does not
 *     intersect their read set.
 *
 * Theorem (Timestamp Ordering - Bernstein & Goodman 1981):
 *   If transactions are assigned unique timestamps and all conflicting
 *   operations execute in timestamp order, the schedule is serializable.
 *
 * Complexity:
 *   - Read: O(1) hash lookup
 *   - Backward validate: O(|read_set| * |committed_txns|)
 *   - Forward validate: O(|write_set| * |active_txns|)
 *   - Commit: O(|write_set|)
 *
 * References:
 *   - Kung, H.T., Robinson, J.T. (1981) "On Optimistic Methods for
 *     Concurrency Control", ACM TODS
 *   - Bernstein, P.A., Goodman, N. (1981) "Concurrency Control in DDBMS"
 *   - Tu, S. et al. (2013) "Speedy Transactions in Multicore In-Memory DBs"
 */

#include "occ.h"
#include <stdio.h>
#include <string.h>

const char *occ_phase_str(OCCTxnPhase p) {
    switch (p) {
    case OCC_TXN_READ_PHASE:     return "READ";
    case OCC_TXN_VALIDATE_PHASE: return "VALIDATE";
    case OCC_TXN_WRITE_PHASE:    return "WRITE";
    case OCC_TXN_COMMITTED:      return "COMMITTED";
    case OCC_TXN_ABORTED:        return "ABORTED";
    default:                     return "UNKNOWN";
    }
}

const char *occ_conflict_str(OCCConflictType c) {
    switch (c) {
    case OCC_CONFLICT_NONE:     return "NONE";
    case OCC_CONFLICT_BACKWARD: return "BACKWARD";
    case OCC_CONFLICT_FORWARD:  return "FORWARD";
    default:                    return "UNKNOWN";
    }
}

void occ_manager_init(OCCManager *mgr) {
    memset(mgr->records, 0, sizeof(mgr->records));
    mgr->record_count = 0;
    mgr->global_version = 1;
    memset(mgr->active_txns, 0, sizeof(mgr->active_txns));
    mgr->active_count = 0;
    mgr->low_watermark = 1;
    mgr->high_watermark = 1;
}

void occ_record_init(OCCRecord *rec, const char *key, const char *value,
                      int64_t version) {
    strncpy(rec->key, key, OCC_KEY_LEN - 1);
    rec->key[OCC_KEY_LEN - 1] = '\0';
    strncpy(rec->value, value ? value : "", OCC_VAL_LEN - 1);
    rec->value[OCC_VAL_LEN - 1] = '\0';
    rec->version = version;
    rec->last_committed_ts = version;
}

void occ_txn_init(OCCTransaction *txn, int64_t txn_id) {
    txn->txn_id = txn_id;
    txn->phase = OCC_TXN_READ_PHASE;
    txn->start_ts = 0;
    txn->commit_ts = 0;
    txn->read_count = 0;
    txn->write_count = 0;
    txn->abort_count = 0;
    memset(txn->read_set, 0, sizeof(txn->read_set));
    memset(txn->write_set, 0, sizeof(txn->write_set));
}

static int32_t occ_find_record(OCCManager *mgr, const char *key) {
    for (int32_t i = 0; i < mgr->record_count; i++) {
        if (strcmp(mgr->records[i].key, key) == 0) return i;
    }
    return -1;
}

static int32_t occ_ensure_record(OCCManager *mgr, const char *key,
                                  const char *init_val) {
    int32_t idx = occ_find_record(mgr, key);
    if (idx >= 0) return idx;
    if (mgr->record_count >= OCC_MAX_KEYS) return -1;
    idx = mgr->record_count;
    occ_record_init(&mgr->records[idx], key, init_val, 0);
    mgr->record_count++;
    return idx;
}

bool occ_txn_begin(OCCManager *mgr, OCCTransaction *txn, int64_t txn_id) {
    if (mgr->active_count >= OCC_MAX_TXN) return false;
    occ_txn_init(txn, txn_id);
    txn->start_ts = mgr->global_version++;
    mgr->active_txns[mgr->active_count] = *txn;
    mgr->active_count++;
    return true;
}

bool occ_txn_read(OCCManager *mgr, OCCTransaction *txn,
                   const char *key, char *value_out, int64_t *version_out) {
    if (!mgr || !txn || !key || !value_out) return false;
    if (txn->phase != OCC_TXN_READ_PHASE) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        if (strcmp(txn->write_set[i].key, key) == 0) {
            strncpy(value_out, txn->write_set[i].value, OCC_VAL_LEN - 1);
            value_out[OCC_VAL_LEN - 1] = '\0';
            if (version_out) *version_out = -1;
            return true;
        }
    }
    int32_t idx = occ_ensure_record(mgr, key, "");
    if (idx < 0) return false;
    OCCRecord *rec = &mgr->records[idx];
    strncpy(value_out, rec->value, OCC_VAL_LEN - 1);
    value_out[OCC_VAL_LEN - 1] = '\0';
    if (version_out) *version_out = rec->version;
    if (txn->read_count < OCC_MAX_KEYS) {
        OCCReadEntry *re = &txn->read_set[txn->read_count];
        strncpy(re->key, key, OCC_KEY_LEN - 1);
        re->key[OCC_KEY_LEN - 1] = '\0';
        re->version_at_read = rec->version;
        txn->read_count++;
    }
    return true;
}

bool occ_txn_write(OCCTransaction *txn, const char *key, const char *value) {
    if (!txn || !key || !value) return false;
    if (txn->phase != OCC_TXN_READ_PHASE) return false;
    if (txn->write_count >= OCC_MAX_KEYS) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        if (strcmp(txn->write_set[i].key, key) == 0) {
            strncpy(txn->write_set[i].value, value, OCC_VAL_LEN - 1);
            txn->write_set[i].value[OCC_VAL_LEN - 1] = '\0';
            return true;
        }
    }
    OCCWriteEntry *we = &txn->write_set[txn->write_count];
    strncpy(we->key, key, OCC_KEY_LEN - 1);
    we->key[OCC_KEY_LEN - 1] = '\0';
    strncpy(we->value, value, OCC_VAL_LEN - 1);
    we->value[OCC_VAL_LEN - 1] = '\0';
    txn->write_count++;
    return true;
}

/* Backward validation: check that no committed transaction wrote to
 * any key this transaction read. Algorithm from Kung & Robinson (1981).
 *
 * Theorem: If backward validation passes, the transaction's reads are
 * consistent with all committed writes. */
bool occ_backward_validation(OCCManager *mgr, OCCTransaction *txn) {
    for (int32_t i = 0; i < txn->read_count; i++) {
        OCCReadEntry *re = &txn->read_set[i];
        int32_t idx = occ_find_record(mgr, re->key);
        if (idx < 0) continue;
        OCCRecord *rec = &mgr->records[idx];
        if (rec->version > re->version_at_read) return false;
    }
    return true;
}

/* Forward validation: check that no active transaction has read any
 * key this transaction intends to write.
 *
 * Per Silo (Tu et al. 2013), forward validation is the preferred approach
 * in modern in-memory databases as it avoids scanning committed history. */
bool occ_forward_validation(OCCManager *mgr, OCCTransaction *txn) {
    for (int32_t i = 0; i < mgr->active_count; i++) {
        OCCTransaction *other = &mgr->active_txns[i];
        if (other->txn_id == txn->txn_id) continue;
        if (other->phase == OCC_TXN_ABORTED) continue;
        for (int32_t w = 0; w < txn->write_count; w++) {
            for (int32_t r = 0; r < other->read_count; r++) {
                if (strcmp(txn->write_set[w].key, other->read_set[r].key) == 0)
                    return false;
            }
        }
    }
    return true;
}

bool occ_txn_validate(OCCManager *mgr, OCCTransaction *txn) {
    if (!mgr || !txn) return false;
    txn->phase = OCC_TXN_VALIDATE_PHASE;
    if (!occ_backward_validation(mgr, txn)) {
        txn->phase = OCC_TXN_ABORTED;
        return false;
    }
    if (!occ_forward_validation(mgr, txn)) {
        txn->phase = OCC_TXN_ABORTED;
        return false;
    }
    txn->phase = OCC_TXN_WRITE_PHASE;
    return true;
}

OCCConflictType occ_detect_conflict(OCCManager *mgr, OCCTransaction *txn,
                                      OCCTransaction *other) {
    if (!mgr || !txn || !other) return OCC_CONFLICT_NONE;
    for (int32_t r = 0; r < txn->read_count; r++) {
        for (int32_t w = 0; w < other->write_count; w++) {
            if (strcmp(txn->read_set[r].key, other->write_set[w].key) == 0)
                return OCC_CONFLICT_BACKWARD;
        }
    }
    for (int32_t w = 0; w < txn->write_count; w++) {
        for (int32_t r = 0; r < other->read_count; r++) {
            if (strcmp(txn->write_set[w].key, other->read_set[r].key) == 0)
                return OCC_CONFLICT_FORWARD;
        }
    }
    return OCC_CONFLICT_NONE;
}

bool occ_txn_commit(OCCManager *mgr, OCCTransaction *txn) {
    if (!mgr || !txn) return false;
    if (!occ_txn_validate(mgr, txn)) {
        txn->phase = OCC_TXN_ABORTED;
        return false;
    }
    txn->commit_ts = mgr->global_version++;
    for (int32_t i = 0; i < txn->write_count; i++) {
        int32_t idx = occ_ensure_record(mgr, txn->write_set[i].key,
                                         txn->write_set[i].value);
        if (idx >= 0) {
            OCCRecord *rec = &mgr->records[idx];
            strncpy(rec->value, txn->write_set[i].value, OCC_VAL_LEN - 1);
            rec->value[OCC_VAL_LEN - 1] = '\0';
            rec->version = txn->commit_ts;
            rec->last_committed_ts = txn->commit_ts;
        }
    }
    txn->phase = OCC_TXN_COMMITTED;
    occ_advance_watermarks(mgr);
    return true;
}

void occ_txn_abort(OCCManager *mgr, OCCTransaction *txn) {
    if (!txn) return;
    txn->phase = OCC_TXN_ABORTED;
    txn->abort_count++;
}

/* Timestamp ordering check per Bernstein & Goodman (1981):
 * All reads must see the latest committed version before start_ts,
 * and all writes must go to versions newer than start_ts. */
bool occ_timestamp_ordering_check(OCCManager *mgr, OCCTransaction *txn) {
    for (int32_t i = 0; i < txn->read_count; i++) {
        OCCReadEntry *re = &txn->read_set[i];
        int32_t idx = occ_find_record(mgr, re->key);
        if (idx >= 0 && mgr->records[idx].version > txn->start_ts)
            return false;
    }
    return true;
}

int64_t occ_get_version(OCCManager *mgr, const char *key) {
    int32_t idx = occ_find_record(mgr, key);
    return (idx >= 0) ? mgr->records[idx].version : 0;
}

int32_t occ_count_conflicts(OCCManager *mgr) {
    int32_t conflicts = 0;
    for (int32_t i = 0; i < mgr->active_count; i++) {
        if (mgr->active_txns[i].phase == OCC_TXN_ABORTED) conflicts++;
    }
    return conflicts;
}

double occ_abort_probability(OCCManager *mgr) {
    if (mgr->active_count == 0) return 0.0;
    return (double)occ_count_conflicts(mgr) / (double)mgr->active_count;
}

void occ_advance_watermarks(OCCManager *mgr) {
    mgr->low_watermark = mgr->high_watermark;
    mgr->high_watermark = mgr->global_version;
}

void occ_manager_print(OCCManager *mgr) {
    printf("=== OCC Manager ===\n");
    printf("  Records: %d  Global version: %lld  Active TXNs: %d\n",
           mgr->record_count, (long long)mgr->global_version, mgr->active_count);
    printf("  Watermarks: low=%lld high=%lld\n",
           (long long)mgr->low_watermark, (long long)mgr->high_watermark);
    printf("  Records:\n");
    for (int32_t i = 0; i < mgr->record_count; i++) {
        OCCRecord *r = &mgr->records[i];
        printf("    [%s] ver=%lld val=[%s]\n",
               r->key, (long long)r->version, r->value);
    }
    printf("  Active Transactions:\n");
    for (int32_t i = 0; i < mgr->active_count; i++) {
        OCCTransaction *t = &mgr->active_txns[i];
        printf("    txn=%lld phase=%s start_ts=%lld reads=%d writes=%d\n",
               (long long)t->txn_id, occ_phase_str(t->phase),
               (long long)t->start_ts, t->read_count, t->write_count);
    }
}

void occ_txn_print(OCCTransaction *txn) {
    printf("=== OCC Transaction %lld ===\n", (long long)txn->txn_id);
    printf("  Phase: %s  Start-TS: %lld  Commit-TS: %lld  Aborts: %d\n",
           occ_phase_str(txn->phase), (long long)txn->start_ts,
           (long long)txn->commit_ts, txn->abort_count);
    printf("  Read set (%d): ", txn->read_count);
    for (int32_t i = 0; i < txn->read_count; i++)
        printf("[%s@v%lld] ", txn->read_set[i].key,
               (long long)txn->read_set[i].version_at_read);
    printf("\n  Write set (%d):\n", txn->write_count);
    for (int32_t i = 0; i < txn->write_count; i++)
        printf("    %s -> [%s]\n", txn->write_set[i].key, txn->write_set[i].value);
}
