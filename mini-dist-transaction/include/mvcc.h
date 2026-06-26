#ifndef MVCC_H
#define MVCC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MVCC_MAX_KEYS 128
#define MVCC_MAX_VERSIONS 1024
#define MVCC_MAX_TXN 64
#define MVCC_GC_THRESHOLD 8
#define MVCC_KEY_LEN 32
#define MVCC_VAL_LEN 128

/* === L1: Core Definitions === */

typedef enum {
    MVCC_TXN_ACTIVE,
    MVCC_TXN_COMMITTED,
    MVCC_TXN_ABORTED
} MVCCTxnStatus;

typedef enum {
    MVCC_REC_INIT,
    MVCC_REC_COMMITTED,
    MVCC_REC_ABORTED
} MVCCRecStatus;

/* Single version record in a chain */
typedef struct mvcc_version {
    int64_t txn_id;
    int64_t commit_ts;
    char value[MVCC_VAL_LEN];
    MVCCRecStatus status;
    struct mvcc_version *prev;
    struct mvcc_version *next;
} MVCCVersion;

/* A key with its version chain */
typedef struct {
    char key[MVCC_KEY_LEN];
    MVCCVersion *head;
    int32_t version_count;
} MVCCRecord;

/* L2: Snapshot Isolation Transaction */
typedef struct {
    int64_t txn_id;
    int64_t read_ts;
    int64_t commit_ts;
    MVCCTxnStatus status;
    char *read_keys[MVCC_MAX_KEYS];
    int32_t read_count;
    char *write_keys[MVCC_MAX_KEYS];
    char write_vals[MVCC_MAX_KEYS][MVCC_VAL_LEN];
    int32_t write_count;
} MVCCTransaction;

/* MVCC Store with global timestamp oracle */
typedef struct {
    MVCCRecord records[MVCC_MAX_KEYS];
    int32_t record_count;
    MVCCVersion versions[MVCC_MAX_VERSIONS];
    int32_t version_count;
    int64_t global_ts;
    MVCCTransaction active_txns[MVCC_MAX_TXN];
    int32_t active_count;
} MVCCStore;

void mvcc_store_init(MVCCStore *store);
void mvcc_version_init(MVCCVersion *v, int64_t txn_id, int64_t commit_ts,
                        const char *value, MVCCRecStatus status);
void mvcc_record_init(MVCCRecord *rec, const char *key);
void mvcc_txn_init(MVCCTransaction *txn, int64_t txn_id);
bool mvcc_txn_begin(MVCCStore *store, MVCCTransaction *txn, int64_t txn_id);
int64_t mvcc_allocate_ts(MVCCStore *store);
MVCCVersion *mvcc_version_chain_head(MVCCStore *store, const char *key);
MVCCVersion *mvcc_visible_version(MVCCStore *store, const char *key, int64_t read_ts);
void mvcc_version_chain_append(MVCCStore *store, const char *key, MVCCVersion *v);
void mvcc_link_version(MVCCVersion *head, MVCCVersion *new_version);
bool mvcc_snapshot_read(MVCCStore *store, MVCCTransaction *txn,
                         const char *key, char *value_out);
bool mvcc_read_your_writes(MVCCTransaction *txn, const char *key, char *value_out);
bool mvcc_prepare_write(MVCCTransaction *txn, const char *key, const char *value);
bool mvcc_validate_write_set(MVCCStore *store, MVCCTransaction *txn);
bool mvcc_txn_commit(MVCCStore *store, MVCCTransaction *txn);
void mvcc_txn_abort(MVCCStore *store, MVCCTransaction *txn);
bool mvcc_check_serializable(MVCCStore *store, MVCCTransaction *txn);
bool mvcc_check_write_conflict(MVCCStore *store, MVCCTransaction *txn,
                                MVCCTransaction *other);
int32_t mvcc_gc_old_versions(MVCCStore *store, int64_t oldest_active_ts);
int32_t mvcc_count_active_txns(MVCCStore *store);
int64_t mvcc_oldest_snapshot_ts(MVCCStore *store);
bool mvcc_serializable_si_validation(MVCCStore *store, MVCCTransaction *txn);
int32_t mvcc_abort_rate(MVCCStore *store);
void mvcc_store_print(MVCCStore *store);
void mvcc_txn_print(MVCCTransaction *txn);
const char *mvcc_txn_status_str(MVCCTxnStatus s);
const char *mvcc_rec_status_str(MVCCRecStatus s);

#endif
