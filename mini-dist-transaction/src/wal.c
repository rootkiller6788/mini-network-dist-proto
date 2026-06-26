/* wal.c -- Write-Ahead Logging (ARIES-inspired)
 *
 * Implements the fundamental WAL protocol: log records MUST be written
 * to stable storage BEFORE the corresponding data pages are modified.
 *
 * Theorem (WAL - Mohan et al. 1992):
 *   The WAL protocol guarantees that after a crash, the database can be
 *   recovered to a consistent state by:
 *   1. ANALYZE: scan log to identify dirty pages and loser transactions
 *   2. REDO: repeat all committed changes from the checkpoint
 *   3. UNDO: reverse all uncommitted changes in reverse chronological order
 *
 * Complexity:
 *   - Log append: O(1) amortized
 *   - Recovery (ARIES): O(log_size) single pass
 *   - Checkpoint: O(active_txns)
 *
 * References:
 *   - Mohan, C. et al. (1992) "ARIES: A Transaction Recovery Method"
 *   - Gray, J., Reuter, A. (1993) "Transaction Processing", Ch.9
 *   - CMU 15-721 Lecture 18: Crash Recovery
 */

#include "wal.h"
#include <stdio.h>
#include <string.h>

const char *wal_record_type_str(WALRecordType t) {
    switch (t) {
    case WAL_REC_INSERT:     return "INSERT";
    case WAL_REC_UPDATE:     return "UPDATE";
    case WAL_REC_DELETE:     return "DELETE";
    case WAL_REC_BEGIN:      return "BEGIN";
    case WAL_REC_COMMIT:     return "COMMIT";
    case WAL_REC_ABORT:      return "ABORT";
    case WAL_REC_CHECKPOINT: return "CHECKPOINT";
    default:                 return "UNKNOWN";
    }
}

void wal_manager_init(WALManager *wm) {
    memset(wm->records, 0, sizeof(wm->records));
    wm->count = 0;
    wm->next_lsn = 1;
    wm->flushed_lsn = 0;
    wm->checkpoint_lsn = 0;
    wm->write_pos = 0;
    wm->flush_pos = 0;
    wm->is_circular = false;
}

void wal_record_init(WALRecord *rec, WALRecordType type, int64_t txn_id,
                      const char *key, const char *old_val, const char *new_val) {
    memset(rec, 0, sizeof(*rec));
    rec->type = type;
    rec->txn_id = txn_id;
    if (key) {
        strncpy(rec->key, key, WAL_KEY_LEN - 1);
        rec->key[WAL_KEY_LEN - 1] = '\0';
    }
    if (old_val) {
        strncpy(rec->old_value, old_val, WAL_VAL_LEN - 1);
        rec->old_value[WAL_VAL_LEN - 1] = '\0';
    }
    if (new_val) {
        strncpy(rec->new_value, new_val, WAL_VAL_LEN - 1);
        rec->new_value[WAL_VAL_LEN - 1] = '\0';
    }
    rec->timestamp = (int64_t)time(NULL);
}

int64_t wal_append(WALManager *wm, WALRecord *rec) {
    if (wm->count >= WAL_MAX_RECORDS) {
        if (!wm->is_circular) {
            wm->is_circular = true;
            wm->write_pos = 0;
        }
    }
    if (wm->count >= WAL_MAX_RECORDS && wm->is_circular) {
        rec->lsn = wm->next_lsn;
        rec->prev_lsn = wm->records[wm->write_pos].lsn;
        wm->records[wm->write_pos] = *rec;
        wm->write_pos = (wm->write_pos + 1) % WAL_MAX_RECORDS;
    } else {
        rec->lsn = wm->next_lsn;
        rec->prev_lsn = (wm->count > 0) ? wm->records[wm->count - 1].lsn : 0;
        wm->records[wm->count] = *rec;
        wm->write_pos = wm->count;
        wm->count++;
    }
    wm->next_lsn++;
    return rec->lsn;
}

bool wal_flush(WALManager *wm) {
    if (wm->write_pos == wm->flush_pos && wm->flushed_lsn >= wm->next_lsn - 1) {
        return true;
    }
    wm->flushed_lsn = wm->next_lsn - 1;
    wm->flush_pos = wm->write_pos;
    return true;
}

int32_t wal_flush_to(WALManager *wm, int64_t target_lsn) {
    int32_t flushed = 0;
    while (wm->flushed_lsn < target_lsn && wm->flushed_lsn < wm->next_lsn - 1) {
        wm->flushed_lsn++;
        flushed++;
    }
    wm->flush_pos = wm->write_pos;
    return flushed;
}

void wal_checkpoint(WALManager *wm) {
    WALRecord cp;
    wal_record_init(&cp, WAL_REC_CHECKPOINT, -1, NULL, NULL, NULL);
    cp.lsn = wm->next_lsn;
    cp.prev_lsn = wm->checkpoint_lsn;
    wm->records[wm->count] = cp;
    wm->count++;
    wm->checkpoint_lsn = cp.lsn;
    wm->next_lsn++;
}

WALRecord *wal_find_by_lsn(WALManager *wm, int64_t lsn) {
    for (int32_t i = 0; i < wm->count; i++) {
        if (wm->records[i].lsn == lsn) return &wm->records[i];
    }
    return NULL;
}

WALRecord *wal_scan_from(WALManager *wm, int64_t start_lsn, int32_t *count_out) {
    if (count_out) *count_out = 0;
    int32_t start = 0;
    for (int32_t i = 0; i < wm->count; i++) {
        if (wm->records[i].lsn >= start_lsn) { start = i; break; }
    }
    if (count_out) *count_out = wm->count - start;
    return &wm->records[start];
}

int32_t wal_analyze_pass(WALManager *wm, int64_t *active_txns, int32_t max_txn,
                          int64_t *first_undone_lsn) {
    int32_t txn_count = 0;
    bool txn_ended[256] = {false};
    int64_t txn_ids[256] = {0};
    if (first_undone_lsn) *first_undone_lsn = INT64_MAX;
    for (int32_t i = 0; i < wm->count; i++) {
        WALRecord *rec = &wm->records[i];
        int32_t t = -1;
        for (int32_t j = 0; j < txn_count; j++) {
            if (txn_ids[j] == rec->txn_id) { t = j; break; }
        }
        if (t < 0 && txn_count < max_txn && rec->txn_id > 0) {
            t = txn_count;
            txn_ids[txn_count] = rec->txn_id;
            txn_ended[txn_count] = false;
            txn_count++;
        }
        if (t >= 0) {
            if (rec->type == WAL_REC_COMMIT || rec->type == WAL_REC_ABORT)
                txn_ended[t] = true;
            if (!txn_ended[t] && rec->lsn < *first_undone_lsn)
                *first_undone_lsn = rec->lsn;
        }
    }
    int32_t out = 0;
    for (int32_t j = 0; j < txn_count && out < max_txn; j++) {
        if (!txn_ended[j]) active_txns[out++] = txn_ids[j];
    }
    return out;
}

int32_t wal_redo_pass(WALManager *wm, int64_t start_lsn,
                       bool (*apply)(void *ctx, const char *key,
                                      const char *val, int64_t ver),
                       void *ctx) {
    int32_t redone = 0;
    for (int32_t i = 0; i < wm->count; i++) {
        WALRecord *rec = &wm->records[i];
        if (rec->lsn < start_lsn) continue;
        if (rec->type == WAL_REC_INSERT || rec->type == WAL_REC_UPDATE) {
            if (apply) apply(ctx, rec->key, rec->new_value, rec->lsn);
            redone++;
        } else if (rec->type == WAL_REC_DELETE) {
            if (apply) apply(ctx, rec->key, NULL, rec->lsn);
            redone++;
        }
    }
    return redone;
}

int32_t wal_undo_pass(WALManager *wm, int64_t *loser_txns, int32_t loser_count,
                       bool (*rollback)(void *ctx, const char *key,
                                         const char *old_val, int64_t ver),
                       void *ctx) {
    int32_t undone = 0;
    for (int32_t i = wm->count - 1; i >= 0; i--) {
        WALRecord *rec = &wm->records[i];
        bool is_loser = false;
        for (int32_t j = 0; j < loser_count; j++) {
            if (rec->txn_id == loser_txns[j]) { is_loser = true; break; }
        }
        if (!is_loser) continue;
        if (rec->type == WAL_REC_INSERT) {
            if (rollback) rollback(ctx, rec->key, NULL, rec->lsn);
            undone++;
        } else if (rec->type == WAL_REC_UPDATE || rec->type == WAL_REC_DELETE) {
            if (rollback) rollback(ctx, rec->key, rec->old_value, rec->lsn);
            undone++;
        }
    }
    return undone;
}

bool wal_recover(WALManager *wm,
                  bool (*redo_fn)(void *ctx, const char *key, const char *val, int64_t ver),
                  bool (*undo_fn)(void *ctx, const char *key, const char *val, int64_t ver),
                  void *ctx) {
    int64_t start = wm->checkpoint_lsn > 0 ? wm->checkpoint_lsn : 1;
    wal_redo_pass(wm, start, redo_fn, ctx);
    int64_t losers[64];
    int64_t first_undone;
    int32_t n = wal_analyze_pass(wm, losers, 64, &first_undone);
    if (n > 0) wal_undo_pass(wm, losers, n, undo_fn, ctx);
    WALRecord abort_rec;
    for (int32_t i = 0; i < n; i++) {
        wal_record_init(&abort_rec, WAL_REC_ABORT, losers[i], NULL, NULL, NULL);
        wal_append(wm, &abort_rec);
    }
    wal_flush(wm);
    return true;
}

bool wal_crash_recovery_simulate(WALManager *wm) {
    int32_t saved_count = wm->count;
    int32_t saved_pos = wm->write_pos;
    wm->count = wm->flush_pos + 1;
    wm->write_pos = wm->flush_pos;
    int64_t losers[64];
    int64_t first_undone;
    int32_t n = wal_analyze_pass(wm, losers, 64, &first_undone);
    wm->count = saved_count;
    wm->write_pos = saved_pos;
    return n > 0;
}

int64_t wal_get_next_lsn(WALManager *wm) { return wm->next_lsn; }
int64_t wal_get_flushed_lsn(WALManager *wm) { return wm->flushed_lsn; }

void wal_print(WALManager *wm) {
    printf("=== WAL Manager ===\n");
    printf("  Records: %d  Next LSN: %lld  Flushed LSN: %lld  "
           "Checkpoint LSN: %lld\n",
           wm->count, (long long)wm->next_lsn,
           (long long)wm->flushed_lsn, (long long)wm->checkpoint_lsn);
    printf("  Circular: %s  Write pos: %d  Flush pos: %d\n",
           wm->is_circular ? "yes" : "no", wm->write_pos, wm->flush_pos);
    printf("  Log:\n");
    for (int32_t i = 0; i < wm->count; i++) {
        WALRecord *r = &wm->records[i];
        printf("    LSN=%lld type=%-10s txn=%lld key=[%s] old=[%s] new=[%s]\n",
               (long long)r->lsn, wal_record_type_str(r->type),
               (long long)r->txn_id, r->key, r->old_value, r->new_value);
    }
}

void wal_record_print(WALRecord *rec) {
    printf("WALRecord{LSN=%lld, type=%s, txn=%lld, key=[%s]}",
           (long long)rec->lsn, wal_record_type_str(rec->type),
           (long long)rec->txn_id, rec->key);
}
