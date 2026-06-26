#ifndef WAL_H
#define WAL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define WAL_MAX_RECORDS 512
#define WAL_KEY_LEN 32
#define WAL_VAL_LEN 128
#define WAL_CHECKPOINT_INTERVAL 64

typedef enum {
    WAL_REC_INSERT,
    WAL_REC_UPDATE,
    WAL_REC_DELETE,
    WAL_REC_BEGIN,
    WAL_REC_COMMIT,
    WAL_REC_ABORT,
    WAL_REC_CHECKPOINT
} WALRecordType;

typedef struct {
    int64_t lsn;
    WALRecordType type;
    int64_t txn_id;
    char key[WAL_KEY_LEN];
    char old_value[WAL_VAL_LEN];
    char new_value[WAL_VAL_LEN];
    int64_t prev_lsn;
    int64_t timestamp;
} WALRecord;

typedef struct {
    WALRecord records[WAL_MAX_RECORDS];
    int32_t count;
    int64_t next_lsn;
    int64_t flushed_lsn;
    int64_t checkpoint_lsn;
    int32_t write_pos;
    int32_t flush_pos;
    bool is_circular;
} WALManager;

void wal_manager_init(WALManager *wm);
void wal_record_init(WALRecord *rec, WALRecordType type, int64_t txn_id,
                      const char *key, const char *old_val, const char *new_val);
int64_t wal_append(WALManager *wm, WALRecord *rec);
bool wal_flush(WALManager *wm);
int32_t wal_flush_to(WALManager *wm, int64_t target_lsn);
void wal_checkpoint(WALManager *wm);

WALRecord *wal_find_by_lsn(WALManager *wm, int64_t lsn);
WALRecord *wal_scan_from(WALManager *wm, int64_t start_lsn, int32_t *count_out);

int32_t wal_analyze_pass(WALManager *wm, int64_t *active_txns, int32_t max_txn,
                          int64_t *first_undone_lsn);
int32_t wal_redo_pass(WALManager *wm, int64_t start_lsn,
                       bool (*apply)(void *ctx, const char *key, const char *val, int64_t ver),
                       void *ctx);
int32_t wal_undo_pass(WALManager *wm, int64_t *loser_txns, int32_t loser_count,
                       bool (*rollback)(void *ctx, const char *key, const char *old_val, int64_t ver),
                       void *ctx);

bool wal_recover(WALManager *wm,
                  bool (*redo_fn)(void *ctx, const char *key, const char *val, int64_t ver),
                  bool (*undo_fn)(void *ctx, const char *key, const char *val, int64_t ver),
                  void *ctx);
bool wal_crash_recovery_simulate(WALManager *wm);

const char *wal_record_type_str(WALRecordType t);
int64_t wal_get_next_lsn(WALManager *wm);
int64_t wal_get_flushed_lsn(WALManager *wm);
void wal_print(WALManager *wm);
void wal_record_print(WALRecord *rec);

#endif
