#include "two_pc.h"
#include "saga.h"
#include "tcc.h"
#include "dist_lock.h"
#include "idempotency.h"
#include "mvcc.h"
#include "wal.h"
#include "occ.h"
#include "xa.h"
#include "fencing.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("=== Full test ===\n"); fflush(stdout);

    printf("2PC... "); fflush(stdout);
    TPCCoordinator c; tpc_coordinator_init(&c,1);
    TPCParticipant p1; tpc_participant_init(&p1,1,"A"); tpc_participant_vote(&p1,true);
    tpc_coordinator_add_participant(&c,&p1);
    assert(tpc_coordinator_prepare(&c));
    assert(tpc_coordinator_commit(&c));
    printf("OK\n"); fflush(stdout);

    printf("Saga... "); fflush(stdout);
    SagaTransaction txn; saga_transaction_init(&txn,101);
    SagaStep s1; saga_step_init(&s1,1,"S1",NULL,NULL);
    saga_transaction_add_step(&txn,&s1);
    printf("OK\n"); fflush(stdout);

    printf("Lock... "); fflush(stdout);
    LockManager lm; lock_manager_init(&lm);
    assert(lock_acquire(&lm,"r:1","A",30000,1000));
    printf("OK\n"); fflush(stdout);

    printf("Idempotent... "); fflush(stdout);
    IdempotentStore is; idempotent_store_init(&is);
    printf("OK\n"); fflush(stdout);

    printf("MVCC... "); fflush(stdout);
    MVCCStore ms; mvcc_store_init(&ms);
    MVCCTransaction mt; mvcc_txn_begin(&ms,&mt,1);
    mvcc_prepare_write(&mt,"k","v");
    mvcc_txn_commit(&ms,&mt);
    printf("OK\n"); fflush(stdout);

    printf("WAL... "); fflush(stdout);
    WALManager wm; wal_manager_init(&wm);
    WALRecord r; wal_record_init(&r,WAL_REC_INSERT,1,"k","","v");
    wal_append(&wm,&r);
    printf("OK\n"); fflush(stdout);

    printf("OCC... "); fflush(stdout);
    OCCManager om; occ_manager_init(&om);
    OCCTransaction ot; occ_txn_begin(&om,&ot,1);
    occ_txn_write(&ot,"x","y");
    occ_txn_commit(&om,&ot);
    printf("OK\n"); fflush(stdout);

    printf("XA... "); fflush(stdout);
    XATransaction xt; xa_transaction_init(&xt,"test");
    printf("OK\n"); fflush(stdout);

    printf("Fencing... "); fflush(stdout);
    FencingTokenStore fs; fencing_store_init(&fs,"v1");
    fencing_issue_token(&fs,"r","o");
    printf("OK\n"); fflush(stdout);

    printf("ALL OK\n");
    return 0;
}
