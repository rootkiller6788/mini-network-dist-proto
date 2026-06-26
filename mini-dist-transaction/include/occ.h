#ifndef OCC_H
#define OCC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define OCC_MAX_KEYS 64
#define OCC_MAX_TXN 32
#define OCC_KEY_LEN 32
#define OCC_VAL_LEN 128
#define OCC_WATERMARK_THRESHOLD 1024

/* L1: Core enums and structs for Optimistic Concurrency Control */

typedef enum {
    OCC_TXN_READ_PHASE,
    OCC_TXN_VALIDATE_PHASE,
    OCC_TXN_WRITE_PHASE,
    OCC_TXN_COMMITTED,
    OCC_TXN_ABORTED
} OCCTxnPhase;

typedef enum {
    OCC_CONFLICT_NONE,
    OCC_CONFLICT_BACKWARD,
    OCC_CONFLICT_FORWARD
} OCCConflictType;

typedef struct occ_read_entry {
    char key[OCC_KEY_LEN];
    int64_t version_at_read;
} OCCReadEntry;

typedef struct occ_write_entry {
    char key[OCC_KEY_LEN];
    char value[OCC_VAL_LEN];
} OCCWriteEntry;

typedef struct {
    int64_t txn_id;
    OCCTxnPhase phase;
    int64_t start_ts;
    int64_t commit_ts;
    OCCReadEntry read_set[OCC_MAX_KEYS];
    int32_t read_count;
    OCCWriteEntry write_set[OCC_MAX_KEYS];
    int32_t write_count;
    int32_t abort_count;
} OCCTransaction;

typedef struct {
    char key[OCC_KEY_LEN];
    char value[OCC_VAL_LEN];
    int64_t version;
    int64_t last_committed_ts;
} OCCRecord;

typedef struct {
    OCCRecord records[OCC_MAX_KEYS];
    int32_t record_count;
    int64_t global_version;
    OCCTransaction active_txns[OCC_MAX_TXN];
    int32_t active_count;
    int64_t low_watermark;
    int64_t high_watermark;
} OCCManager;

void occ_manager_init(OCCManager *mgr);
void occ_record_init(OCCRecord *rec, const char *key, const char *value,
                      int64_t version);
void occ_txn_init(OCCTransaction *txn, int64_t txn_id);

bool occ_txn_begin(OCCManager *mgr, OCCTransaction *txn, int64_t txn_id);
bool occ_txn_read(OCCManager *mgr, OCCTransaction *txn,
                   const char *key, char *value_out, int64_t *version_out);
bool occ_txn_write(OCCTransaction *txn, const char *key, const char *value);

bool occ_txn_validate(OCCManager *mgr, OCCTransaction *txn);
OCCConflictType occ_detect_conflict(OCCManager *mgr, OCCTransaction *txn,
                                      OCCTransaction *other);
bool occ_txn_commit(OCCManager *mgr, OCCTransaction *txn);
void occ_txn_abort(OCCManager *mgr, OCCTransaction *txn);

bool occ_backward_validation(OCCManager *mgr, OCCTransaction *txn);
bool occ_forward_validation(OCCManager *mgr, OCCTransaction *txn);
int64_t occ_get_version(OCCManager *mgr, const char *key);
bool occ_timestamp_ordering_check(OCCManager *mgr, OCCTransaction *txn);

int32_t occ_count_conflicts(OCCManager *mgr);
double occ_abort_probability(OCCManager *mgr);
void occ_advance_watermarks(OCCManager *mgr);

void occ_manager_print(OCCManager *mgr);
void occ_txn_print(OCCTransaction *txn);
const char *occ_phase_str(OCCTxnPhase p);
const char *occ_conflict_str(OCCConflictType c);

#endif
