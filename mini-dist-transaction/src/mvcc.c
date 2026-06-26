#include "mvcc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *mvcc_txn_status_str(MVCCTxnStatus s) {
    switch (s) {
    case MVCC_TXN_ACTIVE:    return "ACTIVE";
    case MVCC_TXN_COMMITTED: return "COMMITTED";
    case MVCC_TXN_ABORTED:   return "ABORTED";
    default:                 return "UNKNOWN";
    }
}

const char *mvcc_rec_status_str(MVCCRecStatus s) {
    switch (s) {
    case MVCC_REC_INIT:      return "INIT";
    case MVCC_REC_COMMITTED: return "COMMITTED";
    case MVCC_REC_ABORTED:   return "ABORTED";
    default:                 return "UNKNOWN";
    }
}

void mvcc_store_init(MVCCStore *store) {
    store->record_count = 0;
    store->version_count = 0;
    store->global_ts = 1;
    store->active_count = 0;
    memset(store->records, 0, sizeof(store->records));
    memset(store->versions, 0, sizeof(store->versions));
    memset(store->active_txns, 0, sizeof(store->active_txns));
}

void mvcc_version_init(MVCCVersion *v, int64_t txn_id, int64_t commit_ts,
                        const char *value, MVCCRecStatus status) {
    v->txn_id = txn_id;
    v->commit_ts = commit_ts;
    v->status = status;
    v->prev = NULL;
    v->next = NULL;
    strncpy(v->value, value ? value : "", MVCC_VAL_LEN - 1);
    v->value[MVCC_VAL_LEN - 1] = '\0';
}

void mvcc_record_init(MVCCRecord *rec, const char *key) {
    strncpy(rec->key, key, MVCC_KEY_LEN - 1);
    rec->key[MVCC_KEY_LEN - 1] = '\0';
    rec->head = NULL;
    rec->version_count = 0;
}

void mvcc_txn_init(MVCCTransaction *txn, int64_t txn_id) {
    txn->txn_id = txn_id;
    txn->read_ts = 0;
    txn->commit_ts = 0;
    txn->status = MVCC_TXN_ACTIVE;
    txn->read_count = 0;
    txn->write_count = 0;
    memset(txn->read_keys, 0, sizeof(txn->read_keys));
    memset(txn->write_keys, 0, sizeof(txn->write_keys));
    memset(txn->write_vals, 0, sizeof(txn->write_vals));
}

int64_t mvcc_allocate_ts(MVCCStore *store) {
    return store->global_ts++;
}

bool mvcc_txn_begin(MVCCStore *store, MVCCTransaction *txn, int64_t txn_id) {
    if (store->active_count >= MVCC_MAX_TXN) return false;
    mvcc_txn_init(txn, txn_id);
    txn->read_ts = mvcc_allocate_ts(store);
    store->active_txns[store->active_count] = *txn;
    store->active_count++;
    return true;
}

MVCCVersion *mvcc_version_chain_head(MVCCStore *store, const char *key) {
    for (int32_t i = 0; i < store->record_count; i++) {
        if (strcmp(store->records[i].key, key) == 0) {
            return store->records[i].head;
        }
    }
    return NULL;
}

MVCCVersion *mvcc_visible_version(MVCCStore *store, const char *key,
                                   int64_t read_ts) {
    MVCCVersion *cur = mvcc_version_chain_head(store, key);
    while (cur) {
        if (cur->status == MVCC_REC_COMMITTED && cur->commit_ts <= read_ts) {
            return cur;
        }
        cur = cur->prev;
    }
    return NULL;
}

void mvcc_link_version(MVCCVersion *head, MVCCVersion *new_version) {
    new_version->prev = head;
    new_version->next = NULL;
    if (head) head->next = new_version;
}

static int32_t mvcc_find_or_create_record(MVCCStore *store, const char *key) {
    for (int32_t i = 0; i < store->record_count; i++) {
        if (strcmp(store->records[i].key, key) == 0) return i;
    }
    if (store->record_count >= MVCC_MAX_KEYS) return -1;
    int32_t idx = store->record_count;
    mvcc_record_init(&store->records[idx], key);
    store->record_count++;
    return idx;
}

void mvcc_version_chain_append(MVCCStore *store, const char *key,
                                MVCCVersion *v) {
    int32_t idx = mvcc_find_or_create_record(store, key);
    if (idx < 0) return;
    MVCCRecord *rec = &store->records[idx];
    if (store->version_count >= MVCC_MAX_VERSIONS) return;
    /* Allocate version from store's persistent array */
    MVCCVersion *slot = &store->versions[store->version_count];
    *slot = *v;
    store->version_count++;
    mvcc_link_version(rec->head, slot);
    rec->head = slot;
    rec->version_count++;
}

bool mvcc_snapshot_read(MVCCStore *store, MVCCTransaction *txn,
                         const char *key, char *value_out) {
    if (!txn || !key || !value_out) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        if (strcmp(txn->write_keys[i], key) == 0) {
            strncpy(value_out, txn->write_vals[i], MVCC_VAL_LEN - 1);
            value_out[MVCC_VAL_LEN - 1] = '\0';
            return true;
        }
    }
    MVCCVersion *v = mvcc_visible_version(store, key, txn->read_ts);
    if (!v) {
        value_out[0] = '\0';
        return false;
    }
    strncpy(value_out, v->value, MVCC_VAL_LEN - 1);
    value_out[MVCC_VAL_LEN - 1] = '\0';
    if (txn->read_count < MVCC_MAX_KEYS) {
        txn->read_keys[txn->read_count] = (char *)key;
        txn->read_count++;
    }
    return true;
}

bool mvcc_read_your_writes(MVCCTransaction *txn, const char *key,
                            char *value_out) {
    if (!txn || !key || !value_out) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        if (strcmp(txn->write_keys[i], key) == 0) {
            strncpy(value_out, txn->write_vals[i], MVCC_VAL_LEN - 1);
            value_out[MVCC_VAL_LEN - 1] = '\0';
            return true;
        }
    }
    return false;
}

bool mvcc_prepare_write(MVCCTransaction *txn, const char *key,
                         const char *value) {
    if (!txn || !key || !value) return false;
    if (txn->status != MVCC_TXN_ACTIVE) return false;
    if (txn->write_count >= MVCC_MAX_KEYS) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        if (strcmp(txn->write_keys[i], key) == 0) {
            strncpy(txn->write_vals[i], value, MVCC_VAL_LEN - 1);
            txn->write_vals[i][MVCC_VAL_LEN - 1] = '\0';
            return true;
        }
    }
    txn->write_keys[txn->write_count] = (char *)key;
    strncpy(txn->write_vals[txn->write_count], value, MVCC_VAL_LEN - 1);
    txn->write_vals[txn->write_count][MVCC_VAL_LEN - 1] = '\0';
    txn->write_count++;
    return true;
}

bool mvcc_validate_write_set(MVCCStore *store, MVCCTransaction *txn) {
    for (int32_t i = 0; i < txn->write_count; i++) {
        MVCCVersion *head = mvcc_version_chain_head(store, txn->write_keys[i]);
        if (head && head->commit_ts > txn->read_ts) return false;
    }
    return true;
}

bool mvcc_check_write_conflict(MVCCStore *store, MVCCTransaction *txn,
                                MVCCTransaction *other) {
    if (txn->txn_id == other->txn_id) return false;
    if (other->status != MVCC_TXN_COMMITTED) return false;
    for (int32_t i = 0; i < txn->write_count; i++) {
        for (int32_t j = 0; j < other->write_count; j++) {
            if (strcmp(txn->write_keys[i], other->write_keys[j]) == 0)
                return true;
        }
    }
    return false;
}

bool mvcc_txn_commit(MVCCStore *store, MVCCTransaction *txn) {
    if (!store || !txn) return false;
    if (txn->status != MVCC_TXN_ACTIVE) return false;
    txn->commit_ts = mvcc_allocate_ts(store);
    if (!mvcc_validate_write_set(store, txn)) {
        mvcc_txn_abort(store, txn);
        return false;
    }
    for (int32_t i = 0; i < txn->write_count; i++) {
        MVCCVersion version;
        mvcc_version_init(&version, txn->txn_id, txn->commit_ts,
                          txn->write_vals[i], MVCC_REC_INIT);
        mvcc_version_chain_append(store, txn->write_keys[i], &version);
        MVCCVersion *head = mvcc_version_chain_head(store, txn->write_keys[i]);
        if (head) {
            head->status = MVCC_REC_COMMITTED;
            head->commit_ts = txn->commit_ts;
        }
    }
    txn->status = MVCC_TXN_COMMITTED;
    for (int32_t i = 0; i < store->active_count; i++) {
        if (store->active_txns[i].txn_id == txn->txn_id) {
            for (int32_t j = i; j < store->active_count - 1; j++)
                store->active_txns[j] = store->active_txns[j + 1];
            store->active_count--;
            break;
        }
    }
    return true;
}

void mvcc_txn_abort(MVCCStore *store, MVCCTransaction *txn) {
    if (!txn) return;
    txn->status = MVCC_TXN_ABORTED;
    if (store) {
        for (int32_t i = 0; i < store->active_count; i++) {
            if (store->active_txns[i].txn_id == txn->txn_id) {
                for (int32_t j = i; j < store->active_count - 1; j++)
                    store->active_txns[j] = store->active_txns[j + 1];
                store->active_count--;
                break;
            }
        }
    }
}

bool mvcc_check_serializable(MVCCStore *store, MVCCTransaction *txn) {
    if (!mvcc_validate_write_set(store, txn)) return false;
    for (int32_t i = 0; i < store->active_count; i++) {
        MVCCTransaction *other = &store->active_txns[i];
        if (other->txn_id == txn->txn_id) continue;
        if (other->status != MVCC_TXN_ACTIVE) continue;
        for (int32_t r = 0; r < txn->read_count; r++) {
            for (int32_t w = 0; w < other->write_count; w++) {
                if (txn->read_keys[r] && other->write_keys[w] &&
                    strcmp(txn->read_keys[r], other->write_keys[w]) == 0)
                    return false;
            }
        }
    }
    return true;
}

int32_t mvcc_count_active_txns(MVCCStore *store) {
    int32_t count = 0;
    for (int32_t i = 0; i < store->active_count; i++) {
        if (store->active_txns[i].status == MVCC_TXN_ACTIVE) count++;
    }
    return count;
}

int64_t mvcc_oldest_snapshot_ts(MVCCStore *store) {
    int64_t oldest = INT64_MAX;
    if (store->active_count == 0) return store->global_ts;
    for (int32_t i = 0; i < store->active_count; i++) {
        if (store->active_txns[i].status == MVCC_TXN_ACTIVE &&
            store->active_txns[i].read_ts < oldest)
            oldest = store->active_txns[i].read_ts;
    }
    return (oldest == INT64_MAX) ? store->global_ts : oldest;
}

int32_t mvcc_gc_old_versions(MVCCStore *store, int64_t oldest_active_ts) {
    int32_t removed = 0;
    for (int32_t i = 0; i < store->record_count; i++) {
        MVCCRecord *rec = &store->records[i];
        MVCCVersion *cur = rec->head;
        int32_t chain_idx = 0;
        while (cur) {
            if (chain_idx > 0 && cur->commit_ts < oldest_active_ts) {
                removed++;
                rec->version_count--;
            }
            cur = cur->prev;
            chain_idx++;
        }
    }
    return removed;
}

bool mvcc_serializable_si_validation(MVCCStore *store, MVCCTransaction *txn) {
    if (!mvcc_validate_write_set(store, txn)) return false;
    for (int32_t i = 0; i < store->active_count; i++) {
        MVCCTransaction *other = &store->active_txns[i];
        if (other->txn_id == txn->txn_id) continue;
        if (other->status == MVCC_TXN_ABORTED) continue;
        bool other_reads_mine = false, i_read_others = false;
        for (int32_t r = 0; r < other->read_count && !other_reads_mine; r++) {
            for (int32_t w = 0; w < txn->write_count; w++) {
                if (txn->write_keys[w] && other->read_keys[r] &&
                    strcmp(other->read_keys[r], txn->write_keys[w]) == 0) {
                    other_reads_mine = true;
                    break;
                }
            }
        }
        for (int32_t r = 0; r < txn->read_count && !i_read_others; r++) {
            for (int32_t w = 0; w < other->write_count; w++) {
                if (other->write_keys[w] && txn->read_keys[r] &&
                    strcmp(txn->read_keys[r], other->write_keys[w]) == 0) {
                    i_read_others = true;
                    break;
                }
            }
        }
        if (other_reads_mine && i_read_others) return false;
    }
    return true;
}

int32_t mvcc_abort_rate(MVCCStore *store) {
    int32_t committed = 0, aborted = 0;
    for (int32_t i = 0; i < store->active_count; i++) {
        if (store->active_txns[i].status == MVCC_TXN_COMMITTED) committed++;
        if (store->active_txns[i].status == MVCC_TXN_ABORTED) aborted++;
    }
    if (committed + aborted == 0) return 0;
    return (aborted * 100) / (committed + aborted);
}

void mvcc_store_print(MVCCStore *store) {
    printf("=== MVCC Store ===\n");
    printf("  Global TS: %lld  Records: %d  Versions: %d  Active TXNs: %d\n",
           (long long)store->global_ts, store->record_count,
           store->version_count, store->active_count);
    printf("  Records:\n");
    for (int32_t i = 0; i < store->record_count; i++) {
        MVCCRecord *rec = &store->records[i];
        printf("    [%s] versions=%d\n", rec->key, rec->version_count);
        MVCCVersion *v = rec->head;
        int32_t level = 0;
        while (v) {
            printf("      v%d: txn=%lld ts=%lld status=%s val=[%s]\n",
                   level, (long long)v->txn_id, (long long)v->commit_ts,
                   mvcc_rec_status_str(v->status), v->value);
            v = v->prev;
            level++;
        }
    }
}

void mvcc_txn_print(MVCCTransaction *txn) {
    printf("=== MVCC Transaction %lld ===\n", (long long)txn->txn_id);
    printf("  Status: %s  Read-TS: %lld  Commit-TS: %lld\n",
           mvcc_txn_status_str(txn->status),
           (long long)txn->read_ts, (long long)txn->commit_ts);
    printf("  Read set (%d): ", txn->read_count);
    for (int32_t i = 0; i < txn->read_count; i++)
        printf("[%s] ", txn->read_keys[i] ? txn->read_keys[i] : "-");
    printf("\n  Write set (%d):\n", txn->write_count);
    for (int32_t i = 0; i < txn->write_count; i++)
        printf("    %s -> [%s]\n",
               txn->write_keys[i] ? txn->write_keys[i] : "-",
               txn->write_vals[i]);
}
