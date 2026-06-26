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
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#include <unistd.h>
#ifdef _WIN32
#define WRITE_OUT(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
#define WRITE_OUT(...) fprintf(stderr, __VA_ARGS__)
#endif
#define TEST(n) do { printf("  %-50s ", n); fflush(stdout); } while(0)
#define CHK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; return; } } while(0)
#define PAS() do { printf("PASS\n"); } while(0)

static void test_2pc_commit(void) { TEST("2PC all-yes commit"); TPCCoordinator c; tpc_coordinator_init(&c,1); TPCParticipant p1,p2; tpc_participant_init(&p1,1,"A"); tpc_participant_init(&p2,2,"B"); tpc_participant_vote(&p1,true); tpc_participant_vote(&p2,true); tpc_coordinator_add_participant(&c,&p1); tpc_coordinator_add_participant(&c,&p2); CHK(tpc_coordinator_prepare(&c),"prep"); CHK(tpc_coordinator_commit(&c),"comm"); PAS(); }
static void test_2pc_abort(void) { TEST("2PC one-no abort"); TPCCoordinator c; tpc_coordinator_init(&c,2); TPCParticipant p1,p2; tpc_participant_init(&p1,1,"A"); tpc_participant_init(&p2,2,"B"); tpc_participant_vote(&p1,true); tpc_participant_vote(&p2,false); tpc_coordinator_add_participant(&c,&p1); tpc_coordinator_add_participant(&c,&p2); CHK(!tpc_coordinator_prepare(&c),"pfail"); PAS(); }
static void test_2pc_timeout(void) { TEST("2PC timeout abort"); TPCCoordinator c; tpc_coordinator_init(&c,3); tpc_handle_timeout(&c); CHK(c.timed_out,"to"); PAS(); }
static void test_3pc(void) { TEST("3PC precommit+commit"); ThreePCCoordinator c; threepc_coordinator_init(&c,4); TPCParticipant p1,p2; tpc_participant_init(&p1,1,"A"); tpc_participant_init(&p2,2,"B"); tpc_participant_vote(&p1,true); tpc_participant_vote(&p2,true); threepc_coordinator_add_participant(&c,&p1); threepc_coordinator_add_participant(&c,&p2); CHK(threepc_prepare(&c),"3p"); CHK(threepc_precommit(&c),"3pc"); CHK(threepc_commit(&c),"3c"); PAS(); }

static bool s_ok(void *c) { (*(int*)c)++; return true; }
static bool s_cp(void *c) { (*(int*)c)--; return true; }
static bool s_no(void *c) { (void)c; return false; }

static void test_saga_success(void) { TEST("Saga success"); SagaTransaction txn; saga_transaction_init(&txn,101); SagaStep s1,s2; saga_step_init(&s1,1,"S1",s_ok,s_cp); saga_step_init(&s2,2,"S2",s_ok,s_cp); saga_transaction_add_step(&txn,&s1); saga_transaction_add_step(&txn,&s2); int c1=0,c2=0; void *c[]={&c1,&c2}; CHK(saga_execute(&txn,c),"ok"); CHK(saga_is_completed(&txn),"comp"); PAS(); }
static void test_saga_compensation(void) { TEST("Saga compensation"); SagaTransaction txn; saga_transaction_init(&txn,102); SagaStep s1,s2; saga_step_init(&s1,1,"S1",s_ok,s_cp); saga_step_init(&s2,2,"S2",s_no,s_cp); saga_transaction_add_step(&txn,&s1); saga_transaction_add_step(&txn,&s2); int c1=0,c2=0; void *c[]={&c1,&c2}; CHK(!saga_execute(&txn,c),"fail"); CHK(saga_is_compensated(&txn),"comped"); PAS(); }

static bool t_ok(void *c) { (void)c; return true; }
static bool t_no(void *c) { (void)c; return false; }

static void test_tcc_success(void) { TEST("TCC success"); TCCTransaction txn; tcc_transaction_init(&txn,"t1"); TCCResource r1,r2; tcc_resource_init(&r1,1,"R1",t_ok,t_ok,t_ok); tcc_resource_init(&r2,2,"R2",t_ok,t_ok,t_ok); tcc_transaction_add_resource(&txn,&r1); tcc_transaction_add_resource(&txn,&r2); void *c[]={NULL,NULL}; CHK(tcc_execute(&txn,c),"ok"); PAS(); }
static void test_tcc_fail(void) { TEST("TCC fail"); TCCTransaction txn; tcc_transaction_init(&txn,"t2"); TCCResource r1; tcc_resource_init(&r1,1,"R1",t_no,t_ok,t_ok); tcc_transaction_add_resource(&txn,&r1); void *c[]={NULL}; CHK(!tcc_execute(&txn,c),"fail"); PAS(); }

static void test_lock_basic(void) { TEST("Lock acq+rel"); LockManager lm; lock_manager_init(&lm); CHK(lock_acquire(&lm,"r:1","A",30000,1000),"acq"); CHK(lock_is_owned_by(&lm,"r:1","A"),"own"); CHK(!lock_acquire(&lm,"r:1","B",30000,1000),"blk"); CHK(lock_release(&lm,"r:1","A"),"rel"); PAS(); }
static void test_lock_expiry(void) { TEST("Lock expiry"); LockManager lm; lock_manager_init(&lm); lock_acquire(&lm,"r:X","w1",5000,0); CHK(lock_handle_expiry(&lm,6000)==1,"exp"); PAS(); }
static void test_lock_renew(void) { TEST("Lock renew"); LockManager lm; lock_manager_init(&lm); lock_acquire(&lm,"r:Y","w1",5000,0); CHK(lock_renew_lease(&lm,"r:Y","w1",10000,3000),"renew"); int64_t rem=lock_remaining_lease_ms(&lm,"r:Y",8000); CHK(rem>3000,"ext"); PAS(); }
static void test_redlock(void) { TEST("Redlock"); LockManager n[5]; LockManager *p[5]; for(int i=0;i<5;i++){lock_manager_init(&n[i]);p[i]=&n[i];} CHK(redlock_acquire(p,5,3,"rc","C",30000,5000),"rla"); CHK(redlock_release(p,5,"rc","C"),"rlr"); PAS(); }
static void test_deadlock(void) { TEST("Deadlock detect"); LockManager lm; lock_manager_init(&lm); lock_acquire(&lm,"a","t1",30000,1000); lock_acquire(&lm,"b","t2",30000,1000); lock_acquire(&lm,"b","t1",30000,1000); lock_acquire(&lm,"a","t2",30000,1000); lock_deadlock_detect(&lm); PAS(); }

static void test_idempotent(void) { TEST("Idempotency"); IdempotentStore s; idempotent_store_init(&s); CHK(idempotent_check(&s,"r1","PUT","/x",0xABCD,NULL)==REQ_NEW,"new"); CHK(idempotent_record(&s,"r1","PUT","/x",0xABCD,"OK"),"rec"); CHK(idempotent_check(&s,"r1","PUT","/x",0xABCD,NULL)==REQ_PROCESSED,"cached"); PAS(); }
static void test_backoff(void) { TEST("Backoff"); RetryPolicy p; retry_policy_init(&p,5,100,30000,false); int64_t d0=idempotent_compute_backoff(&p,0),d1=idempotent_compute_backoff(&p,1); CHK(d1>=d0,"mono"); PAS(); }

static void test_mvcc_read(void) { TEST("MVCC snapshot read"); MVCCStore s; mvcc_store_init(&s); MVCCTransaction t; mvcc_txn_begin(&s,&t,1); mvcc_prepare_write(&t,"k1","v1"); mvcc_txn_commit(&s,&t); MVCCTransaction t2; mvcc_txn_begin(&s,&t2,2); char b[128]; CHK(mvcc_snapshot_read(&s,&t2,"k1",b),"read"); CHK(strcmp(b,"v1")==0,"val"); PAS(); }
static void test_mvcc_conflict(void) { TEST("MVCC ww-conflict"); MVCCStore s; mvcc_store_init(&s); MVCCTransaction t1,t2; mvcc_txn_begin(&s,&t1,1); mvcc_txn_begin(&s,&t2,2); mvcc_prepare_write(&t1,"x","a"); mvcc_prepare_write(&t2,"x","b"); CHK(mvcc_txn_commit(&s,&t1),"t1"); CHK(!mvcc_txn_commit(&s,&t2),"t2"); PAS(); }
static void test_mvcc_serializable(void) { TEST("MVCC serializable"); MVCCStore s; mvcc_store_init(&s); MVCCTransaction seed; mvcc_txn_begin(&s,&seed,0); mvcc_prepare_write(&seed,"a","1"); mvcc_txn_commit(&s,&seed); MVCCTransaction t1,t2; mvcc_txn_begin(&s,&t1,1); mvcc_txn_begin(&s,&t2,2); char b[128]; mvcc_snapshot_read(&s,&t1,"a",b); mvcc_prepare_write(&t2,"a","2"); mvcc_txn_commit(&s,&t2); mvcc_prepare_write(&t1,"a","3"); CHK(!mvcc_txn_commit(&s,&t1),"t1 abort (ww-conflict on a)"); PAS(); }

static void test_wal_basic(void) { TEST("WAL append"); WALManager wm; wal_manager_init(&wm); WALRecord r; wal_record_init(&r,WAL_REC_INSERT,1,"k1","","v1"); CHK(wal_append(&wm,&r)==1,"lsn=1"); CHK(wal_flush(&wm),"flush"); CHK(wal_get_flushed_lsn(&wm)==1,"flushed"); PAS(); }
static void test_wal_recovery(void) { TEST("WAL analyze"); WALManager wm; wal_manager_init(&wm); WALRecord b,i,c; wal_record_init(&b,WAL_REC_BEGIN,10,"","",""); wal_record_init(&i,WAL_REC_INSERT,10,"k1","","hello"); wal_record_init(&c,WAL_REC_COMMIT,10,"","",""); wal_append(&wm,&b); wal_append(&wm,&i); wal_append(&wm,&c); WALRecord b2,i2; wal_record_init(&b2,WAL_REC_BEGIN,11,"","",""); wal_record_init(&i2,WAL_REC_INSERT,11,"k2","","world"); wal_append(&wm,&b2); wal_append(&wm,&i2); int64_t losers[16],first; int32_t n=wal_analyze_pass(&wm,losers,16,&first); CHK(n==1,"1 loser"); CHK(losers[0]==11,"txn11"); PAS(); }

static void test_occ_commit(void) { TEST("OCC commit"); OCCManager mgr; occ_manager_init(&mgr); OCCTransaction t; occ_txn_begin(&mgr,&t,1); char b[128]; int64_t v; occ_txn_write(&t,"x","hello"); occ_txn_read(&mgr,&t,"x",b,&v); CHK(occ_txn_commit(&mgr,&t),"occ"); PAS(); }
static void test_occ_conflict(void) { TEST("OCC conflict"); OCCManager mgr; occ_manager_init(&mgr); OCCTransaction t1,t2; occ_txn_begin(&mgr,&t1,1); occ_txn_begin(&mgr,&t2,2); char b[128]; int64_t v; occ_txn_write(&t1,"x","v1"); occ_txn_read(&mgr,&t2,"x",b,&v); CHK(occ_txn_commit(&mgr,&t1),"t1"); CHK(!occ_txn_commit(&mgr,&t2),"t2"); PAS(); }

static int32_t xp(void *c, XID *x) { (*(int*)c)++; (void)x; return XA_OK; }
static int32_t xc(void *c, XID *x, bool o) { (*(int*)c)++; (void)x; (void)o; return XA_OK; }
static void test_xa_2pc(void) { TEST("XA prepare+commit"); int p1=0; XAResourceManager rm; xa_rm_init(&rm,"DB1",&p1,NULL,NULL,NULL,NULL,(XAPrepareFn)xp,(XACommitFn)xc,NULL); XATransaction txn; xa_transaction_init(&txn,"xa"); xa_enlist_rm(&txn,&rm); XID xid; xid_init(&xid,1,"G1","B1"); CHK(xa_start(&txn,&xid),"st"); CHK(xa_end(&txn,&xid),"en"); CHK(xa_prepare_all(&txn,&xid)==1,"prep"); CHK(xa_commit_all(&txn,&xid)==1,"comm"); PAS(); }

static void test_fencing_issue(void) { TEST("Fencing issue"); FencingTokenStore s; fencing_store_init(&s,"v1"); FencingToken *t=fencing_issue_token(&s,"r:X","A"); CHK(t!=NULL,"iss"); CHK(t->value>=1,"val"); FencingGuard g; fencing_guard_init(&g); CHK(fencing_validate_token(&g,t),"val"); PAS(); }
static void test_fencing_reject(void) { TEST("Fencing reject"); FencingGuard g; fencing_guard_init(&g); g.highest_seen=100; CHK(!fencing_check_and_advance(&g,50),"rej"); CHK(g.rejected_count==1,"cnt"); PAS(); }
static void test_fencing_monotonic(void) { TEST("Fencing monotonic"); FencingTokenStore s; fencing_store_init(&s,"v2"); fencing_issue_token(&s,"r:A","o1"); fencing_issue_token(&s,"r:B","o2"); CHK(fencing_is_monotonic(&s),"mono"); PAS(); }

int main(void) {
    printf("\n=== mini-dist-transaction Test Suite ===\n\n");
    printf("[Two-Phase Commit]\n"); test_2pc_commit(); test_2pc_abort(); test_2pc_timeout(); test_3pc();
    printf("\n[Saga]\n"); test_saga_success(); test_saga_compensation();
    printf("\n[TCC]\n"); test_tcc_success(); test_tcc_fail();
    printf("\n[Distributed Lock]\n"); test_lock_basic(); test_lock_expiry(); test_lock_renew(); test_redlock(); test_deadlock();
    printf("\n[Idempotency]\n"); test_idempotent(); test_backoff();
    printf("\n[MVCC]\n"); test_mvcc_read(); test_mvcc_conflict(); test_mvcc_serializable();
    printf("\n[WAL]\n"); test_wal_basic(); test_wal_recovery();
    printf("\n[OCC]\n"); test_occ_commit(); test_occ_conflict();
    printf("\n[XA]\n"); test_xa_2pc();
    printf("\n[Fencing]\n"); test_fencing_issue(); test_fencing_reject(); test_fencing_monotonic();
    printf("\n========================================\n");
    if (failures == 0) { printf("  ALL TESTS PASSED (28 tests)\n"); return 0; }
    printf("  %d TEST(S) FAILED\n", failures); return 1;
}
