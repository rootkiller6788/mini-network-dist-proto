# Knowledge Graph -- mini-dist-transaction

## L1: Core Definitions

| Entity | File | Description |
|--------|------|-------------|
| TPCCoordinator, TPCParticipant | include/two_pc.h | 2PC coordinator/participant with vote state machine |
| ThreePCCoordinator | include/two_pc.h | 3PC coordinator with PREPARE/PRECOMMIT/COMMIT phases |
| SagaStep, SagaTransaction | include/saga.h | Saga step with action+compensate callbacks |
| TCCResource, TCCTransaction | include/tcc.h | TCC resource with try/confirm/cancel operations |
| DistLock, LockManager | include/dist_lock.h | Lease-based distributed lock with wait queue |
| IdempotentRequest, IdempotentStore | include/idempotency.h | Idempotency cache with LRU eviction |
| RetryPolicy | include/idempotency.h | Exponential backoff with jitter |
| MVCCStore, MVCCTransaction, MVCCVersion | include/mvcc.h | MVCC store with timestamp oracle and version chains |
| WALManager, WALRecord | include/wal.h | WAL with LSN-based logging and ARIES recovery |
| OCCManager, OCCTransaction | include/occ.h | OCC with read/validate/write phases |
| XAResourceManager, XATransaction, XID | include/xa.h | X/Open XA interface with heuristic handling |
| FencingToken, FencingTokenStore, FencingGuard | include/fencing.h | Fencing tokens for split-brain prevention |

## L2: Core Concepts

| Concept | Implementation | Reference |
|---------|---------------|-----------|
| Atomic Commitment | src/two_pc.c | Gray 1981, Skeen 1981 |
| Non-blocking Commit | src/two_pc.c (3PC) | Skeen 1981 |
| Compensating Transactions | src/saga.c | Garcia-Molina 1987 |
| Resource Reservation | src/tcc.c | Seata TCC pattern |
| Lease-based Locking | src/dist_lock.c | Chubby (Burrows 2006) |
| Majority Quorum Locking | src/dist_lock.c (Redlock) | Redis Redlock |
| Idempotent Operations | src/idempotency.c | HTTP idempotency semantics |
| Snapshot Isolation | src/mvcc.c | Berenson et al. 1995 |
| Write-Ahead Logging | src/wal.c | Mohan et al. 1992 (ARIES) |
| Optimistic Concurrency Control | src/occ.c | Kung & Robinson 1981 |
| X/Open XA Standard | src/xa.c | X/Open CAE 1991 |
| Fencing Tokens | src/fencing.c | Kleppmann 2016 |

## L3: Engineering Structures

| Structure | Component | File |
|-----------|-----------|------|
| Version Chain | Doubly-linked list of MVCCVersion | src/mvcc.c |
| WAL Ring Buffer | Circular WALRecord array with LSN | src/wal.c |
| Lock Manager | Lock array + Wait queue (FIFO) | src/dist_lock.c |
| XA State Machine | ACTIVE->IDLE->PREPARED->COMMITTED | src/xa.c |
| OCC Validation | Backward + Forward validation phases | src/occ.c |
| Idempotency Cache | LRU array with counter-based eviction | src/idempotency.c |

## L4: Standards & Theorems

| Theorem/Standard | Statement | Validation |
|-----------------|-----------|------------|
| Snapshot Isolation (Berenson 95) | Each txn reads from a consistent snapshot | mvcc_snapshot_read |
| Serializability via OCC (Kung 81) | OCC guarantees serializability with validation | occ_txn_validate |
| WAL Correctness (ARIES 92) | REDO all + UNDO losers = consistent recovery | wal_recover |
| CAP Theorem (Brewer 2000) | Network partition forces C/A choice | docs/ |
| XA Specification (1991) | 2PC interface for heterogeneous RMs | src/xa.c |
| Fencing Safety (Kleppmann 16) | Monotonic token prevents stale writes | fencing_check_and_advance |
| Deadlock Detection (Holt 72) | Wait-for graph cycle = deadlock | lock_deadlock_detect |

## L5: Algorithms & Methods

| Algorithm | Complexity | File |
|-----------|-----------|------|
| 2PC Prepare/Commit | O(N) messages, 2 rounds | src/two_pc.c |
| 3PC Precommit | O(N) messages, 3 rounds | src/two_pc.c |
| Saga Compensation | O(N) forward, O(K) reverse | src/saga.c |
| TCC Try-Confirm-Cancel | O(N) per phase | src/tcc.c |
| Redlock Quorum | O(N) acquires, majority check | src/dist_lock.c |
| Deadlock Detection (Floyd-Warshall) | O(N^3) transitive closure | src/dist_lock.c |
| Exponential Backoff | O(log(max_delay/base)) | src/idempotency.c |
| MVCC Snapshot Read | O(chain_length) | src/mvcc.c |
| MVCC Write-Set Validation | O(|WS| * chain_length) | src/mvcc.c |
| WAL Analyze Pass | O(log_size) single scan | src/wal.c |
| WAL REDO/UNDO | O(log_size) | src/wal.c |
| OCC Backward Validation | O(|RS| * committed_txns) | src/occ.c |
| OCC Forward Validation | O(|WS| * active_txns) | src/occ.c |

## L6: Canonical Problems

| Problem | Solution | Demo |
|---------|----------|------|
| Distributed Atomic Commit | 2PC/3PC Coordinator | examples/two_pc_demo.c |
| Long-running Business Transactions | Saga with compensation | examples/saga_demo.c |
| Distributed Mutual Exclusion | Redlock across N nodes | examples/dist_lock_demo.c |
| Crash Recovery | ARIES: Analyze -> REDO -> UNDO | src/wal.c |
| Snapshot Read Consistency | MVCC version chain walk | src/mvcc.c |
| Split-Brain Prevention | Fencing token validation | src/fencing.c |

## L7: Applications

| Application | Implementation | Reference |
|-------------|---------------|-----------|
| Travel Booking Saga | Hotel + Flight + Payment with compensation | examples/saga_demo.c |
| Order Processing TCC | Reserve -> Confirm/Cancel | examples/two_pc_demo.c |
| Critical Section Locking | Redlock across 5 Redis nodes | examples/dist_lock_demo.c |
| Cross-DB XA Transaction | MySQL + PostgreSQL via XA | src/xa.c |

## L8: Advanced Topics

| Topic | Implementation | Status |
|-------|---------------|--------|
| Serializable Snapshot Isolation (SSI) | mvcc_serializable_si_validation (Cahill 2008) | Implemented |
| Fencing Tokens | Monotonic token with guard validation | Implemented |
| Distributed Deadlock Detection | Floyd-Warshall on wait-for graph | Implemented |
| XA Heuristic Outcomes | Handle HEURHAZ/HEURCOM/HEURRB/HEURMIX | Implemented |
| Lock Wait Queue Processing | FIFO fairness with lease management | Implemented |

## L9: Industry Frontiers

| Topic | Coverage | Status |
|-------|----------|--------|
| Google Spanner TrueTime | Documented in docs/course-alignment.md | Doc only |
| Google Percolator | Snapshot isolation + 2PC pattern | Doc only |
| Hybrid Logical Clocks (HLC) | Alternative to TrueTime | Doc only |
| Calvin Deterministic DB | Lock-based deterministic ordering | Not covered |
| Seata AT Mode | Automatic undo_log compensation | Not covered |
