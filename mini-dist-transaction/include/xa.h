#ifndef XA_H
#define XA_H

#include <stdbool.h>
#include <stdint.h>

#define XA_MAX_RMS 8
#define XA_MAX_XID_LEN 64
#define XA_MAX_TXN 16

/* L1: XA Return Codes per X/Open CAE Specification (1991) */
typedef enum {
    XA_OK          =  0,
    XA_RBROLLBACK  =  1,
    XA_RBCOMMFAIL  =  2,
    XA_RBDEADLOCK  =  3,
    XA_RBINTEGRITY =  4,
    XA_RBOTHER     =  5,
    XA_RBPROTO     =  6,
    XA_RBTIMEOUT   =  7,
    XA_RDONLY      =  3,  /* XA spec: same value as RBDEADLOCK, indicates read-only RM */
    XA_HEURHAZ     =  8,
    XA_HEURCOM     =  9,
    XA_HEURRB      = 10,
    XA_HEURMIX     = 11,
    XA_RETRY       = 12,
    XAER_RMERR     = -1,
    XAER_NOTA      = -2,
    XAER_INVAL     = -3,
    XAER_PROTO     = -4,
    XAER_RMFAIL    = -5,
    XAER_DUPID     = -6,
    XAER_OUTSIDE   = -7
} XARetCode;

typedef enum {
    XA_TXN_ACTIVE,
    XA_TXN_IDLE,
    XA_TXN_PREPARED,
    XA_TXN_COMMITTED,
    XA_TXN_ROLLED_BACK,
    XA_TXN_HEURISTIC
} XATxnState;

/* L1: XA Transaction ID (XID) format per X/Open spec */
typedef struct {
    int32_t format_id;
    int32_t gtrid_len;
    int32_t bqual_len;
    char data[XA_MAX_XID_LEN];
} XID;

/* Resource Manager interface */
typedef bool (*XARmOpenFn)(void *rm_ctx, const char *rm_name);
typedef bool (*XARmCloseFn)(void *rm_ctx);
typedef int32_t (*XAStartFn)(void *rm_ctx, XID *xid, int32_t flags);
typedef int32_t (*XAEndFn)(void *rm_ctx, XID *xid, int32_t flags);
typedef int32_t (*XAPrepareFn)(void *rm_ctx, XID *xid);
typedef int32_t (*XACommitFn)(void *rm_ctx, XID *xid, bool one_phase);
typedef int32_t (*XARollbackFn)(void *rm_ctx, XID *xid);

typedef struct {
    char name[48];
    void *rm_ctx;
    XARmOpenFn xa_open;
    XARmCloseFn xa_close;
    XAStartFn xa_start;
    XAEndFn xa_end;
    XAPrepareFn xa_prepare;
    XACommitFn xa_commit;
    XARollbackFn xa_rollback;
} XAResourceManager;

/* L2: XA Transaction Coordinator */
typedef struct {
    XAResourceManager rms[XA_MAX_RMS];
    int32_t rm_count;
    XATxnState state;
    XID current_xid;
    char txn_name[64];
} XATransaction;

const char *xa_ret_code_str(int32_t code);
void xid_init(XID *xid, int32_t format_id, const char *gtrid, const char *bqual);
void xa_rm_init(XAResourceManager *rm, const char *name, void *ctx,
                 XARmOpenFn open_fn, XARmCloseFn close_fn,
                 XAStartFn start_fn, XAEndFn end_fn,
                 XAPrepareFn prepare_fn, XACommitFn commit_fn,
                 XARollbackFn rollback_fn);
void xa_transaction_init(XATransaction *txn, const char *name);

bool xa_enlist_rm(XATransaction *txn, XAResourceManager *rm);
bool xa_start(XATransaction *txn, XID *xid);
bool xa_end(XATransaction *txn, XID *xid);
int32_t xa_prepare_all(XATransaction *txn, XID *xid);
int32_t xa_commit_all(XATransaction *txn, XID *xid);
int32_t xa_rollback_all(XATransaction *txn, XID *xid);
bool xa_two_phase_execute(XATransaction *txn, XID *xid);

int32_t xa_count_prepared(XATransaction *txn, XID *xid);
int32_t xa_handle_heuristic(XATransaction *txn, XID *xid);

const char *xa_state_str(XATxnState s);
void xa_print_state(XATransaction *txn);

#endif
