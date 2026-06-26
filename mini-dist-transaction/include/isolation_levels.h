#ifndef ISOLATION_LEVELS_H
#define ISOLATION_LEVELS_H
#include <stdbool.h>
#include <stdint.h>

/* L4: ANSI SQL Isolation Levels (ANSI X3.135-1992; Berenson et al., 1995)
 *
 * Phenomenon definitions:
 *   Dirty Read:     T1 reads uncommitted data written by T2
 *   Non-Repeatable: T1 reads same row twice, gets different values (T2 updated)
 *   Phantom:        T1 executes same query twice, gets different row sets (T2 inserted/deleted)
 *   Lost Update:    T1's write is overwritten by T2 before T1 commits
 *
 * Isolation guarantees:
 *   READ UNCOMMITTED: No guarantees (dirty reads allowed)
 *   READ COMMITTED:   No dirty reads (most DBs default)
 *   REPEATABLE READ:  No dirty reads, no non-repeatable reads (Snapshot Isolation)
 *   SERIALIZABLE:     No anomalies — equivalent to serial execution
 *
 * Theorem (Adya et al., 2000): Snapshot Isolation does NOT guarantee
 * serializability.  Write Skew anomaly is possible under SI when
 * two transactions read overlapping data but write disjoint subsets.
 *
 * Reference: Stanford CS 245, CMU 15-445/645, MIT 6.824
 */

typedef enum {
    ISO_READ_UNCOMMITTED,
    ISO_READ_COMMITTED,
    ISO_REPEATABLE_READ,
    ISO_SERIALIZABLE
} IsolationLevel;

typedef struct {
    uint64_t txn_id;
    uint64_t start_ts;     /* start timestamp (logical clock) */
    uint64_t commit_ts;    /* commit timestamp */
    IsolationLevel level;
    bool     is_read_only;
} TxnContext;

/* Initialize transaction context */
void txn_init(TxnContext *txn, uint64_t txn_id, IsolationLevel level);

/* L4: Check if one isolation level is at least as strong as another */
bool iso_at_least(IsolationLevel actual, IsolationLevel required);

/* L4: Determine which anomalies are possible at a given level */
typedef enum { ANOMALY_DIRTY_READ=1, ANOMALY_NONREPEATABLE=2,
               ANOMALY_PHANTOM=4, ANOMALY_WRITE_SKEW=8,
               ANOMALY_LOST_UPDATE=16 } AnomalyType;
int  iso_possible_anomalies(IsolationLevel level);

/* L8: Serializability verification via conflict graph cycle detection
 * Theorem (Papadimitriou, 1979): A history H is conflict-serializable
 * iff its serialization graph SG(H) is acyclic. */
typedef struct {
    uint64_t from_txn;
    uint64_t to_txn;
    char     item[64];
    bool     is_read_write; /* true = rw, false = wr */
} ConflictEdge;

bool ser_detect_cycle(const ConflictEdge *edges, int num_edges,
                       uint64_t *txns, int num_txns);

/* L7: Write Skew detection for Snapshot Isolation
 * Two transactions T1, T2 each read x and y, then T1 writes y, T2 writes x.
 * Under SI, both commit → invariant x+y=const violated. */
bool si_detect_write_skew(const ConflictEdge *edges, int num_edges,
                           uint64_t txn_a, uint64_t txn_b);

#endif
