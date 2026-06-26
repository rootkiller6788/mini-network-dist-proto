#include "xa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *xa_ret_code_str(int32_t code) {
    switch (code) {
    case XA_OK: return "XA_OK";
    case XA_RBROLLBACK: return "XA_RBROLLBACK";
    case XA_RBCOMMFAIL: return "XA_RBCOMMFAIL";
    case XA_RBDEADLOCK: /* XA_RDONLY shares value 3 */ return "XA_RBDEADLOCK";
    case XA_RBINTEGRITY: return "XA_RBINTEGRITY";
    case XA_RBOTHER: return "XA_RBOTHER";
    case XA_RBPROTO: return "XA_RBPROTO";
    case XA_RBTIMEOUT: return "XA_RBTIMEOUT";
    case XA_HEURHAZ: return "XA_HEURHAZ";
    case XA_HEURCOM: return "XA_HEURCOM";
    case XA_HEURRB: return "XA_HEURRB";
    case XA_HEURMIX: return "XA_HEURMIX";
    case XA_RETRY: return "XA_RETRY";
    case XAER_RMERR: return "XAER_RMERR";
    case XAER_NOTA: return "XAER_NOTA";
    case XAER_INVAL: return "XAER_INVAL";
    case XAER_PROTO: return "XAER_PROTO";
    case XAER_RMFAIL: return "XAER_RMFAIL";
    case XAER_DUPID: return "XAER_DUPID";
    case XAER_OUTSIDE: return "XAER_OUTSIDE";
    default: return "XA_UNKNOWN";
    }
}

const char *xa_state_str(XATxnState s) {
    switch (s) {
    case XA_TXN_ACTIVE: return "ACTIVE";
    case XA_TXN_IDLE: return "IDLE";
    case XA_TXN_PREPARED: return "PREPARED";
    case XA_TXN_COMMITTED: return "COMMITTED";
    case XA_TXN_ROLLED_BACK: return "ROLLED_BACK";
    case XA_TXN_HEURISTIC: return "HEURISTIC";
    default: return "UNKNOWN";
    }
}

void xid_init(XID *xid, int32_t format_id, const char *gtrid, const char *bqual) {
    memset(xid, 0, sizeof(*xid));
    xid->format_id = format_id;
    if (gtrid) { xid->gtrid_len = (int32_t)strlen(gtrid); memcpy(xid->data, gtrid, xid->gtrid_len); }
    if (bqual) { xid->bqual_len = (int32_t)strlen(bqual); memcpy(xid->data + xid->gtrid_len, bqual, xid->bqual_len); }
}

void xa_rm_init(XAResourceManager *rm, const char *name, void *ctx,
                 XARmOpenFn open_fn, XARmCloseFn close_fn,
                 XAStartFn start_fn, XAEndFn end_fn,
                 XAPrepareFn prepare_fn, XACommitFn commit_fn,
                 XARollbackFn rollback_fn) {
    strncpy(rm->name, name, sizeof(rm->name) - 1);
    rm->name[sizeof(rm->name) - 1] = 0;
    rm->rm_ctx = ctx;
    rm->xa_open = open_fn; rm->xa_close = close_fn;
    rm->xa_start = start_fn; rm->xa_end = end_fn;
    rm->xa_prepare = prepare_fn; rm->xa_commit = commit_fn; rm->xa_rollback = rollback_fn;
}

void xa_transaction_init(XATransaction *txn, const char *name) {
    memset(txn->rms, 0, sizeof(txn->rms));
    txn->rm_count = 0; txn->state = XA_TXN_IDLE;
    memset(&txn->current_xid, 0, sizeof(txn->current_xid));
    strncpy(txn->txn_name, name ? name : "unnamed", sizeof(txn->txn_name) - 1);
    txn->txn_name[sizeof(txn->txn_name) - 1] = 0;
}

bool xa_enlist_rm(XATransaction *txn, XAResourceManager *rm) {
    if (txn->rm_count >= XA_MAX_RMS) return false;
    txn->rms[txn->rm_count++] = *rm;
    return true;
}

bool xa_start(XATransaction *txn, XID *xid) {
    if (txn->state != XA_TXN_IDLE) return false;
    txn->state = XA_TXN_ACTIVE;
    if (xid) txn->current_xid = *xid;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        if (rm->xa_start && rm->xa_start(rm->rm_ctx, &txn->current_xid, 0) < 0) return false;
    }
    return true;
}

bool xa_end(XATransaction *txn, XID *xid) {
    if (txn->state != XA_TXN_ACTIVE) return false;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        if (rm->xa_end) rm->xa_end(rm->rm_ctx, &txn->current_xid, 0);
    }
    txn->state = XA_TXN_IDLE; (void)xid;
    return true;
}

int32_t xa_prepare_all(XATransaction *txn, XID *xid) {
    int32_t ok = 0;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        int32_t rc = XA_OK;
        if (rm->xa_prepare) rc = rm->xa_prepare(rm->rm_ctx, &txn->current_xid);
        if (rc == XA_OK || rc == XA_RDONLY) ok++;
        else return -(i + 1);
    }
    if (ok == txn->rm_count) txn->state = XA_TXN_PREPARED;
    return ok;
}

int32_t xa_commit_all(XATransaction *txn, XID *xid) {
    if (txn->state != XA_TXN_PREPARED) return XAER_PROTO;
    int32_t ok = 0; bool heuristic = false;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        int32_t rc = XA_OK;
        if (rm->xa_commit) rc = rm->xa_commit(rm->rm_ctx, &txn->current_xid, false);
        if (rc == XA_OK) ok++;
        else if (rc >= XA_HEURHAZ) heuristic = true;
    }
    txn->state = heuristic ? XA_TXN_HEURISTIC : XA_TXN_COMMITTED;
    (void)xid; return ok;
}

int32_t xa_rollback_all(XATransaction *txn, XID *xid) {
    int32_t ok = 0;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        int32_t rc = XA_OK;
        if (rm->xa_rollback) rc = rm->xa_rollback(rm->rm_ctx, &txn->current_xid);
        if (rc == XA_OK) ok++;
    }
    txn->state = XA_TXN_ROLLED_BACK; (void)xid;
    return ok;
}

bool xa_two_phase_execute(XATransaction *txn, XID *xid) {
    if (!xa_start(txn, xid)) return false;
    xa_end(txn, xid);
    if (xa_prepare_all(txn, xid) == txn->rm_count) { xa_commit_all(txn, xid); return true; }
    xa_rollback_all(txn, xid);
    return false;
}

int32_t xa_count_prepared(XATransaction *txn, XID *xid) {
    int32_t c = 0;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        if (rm->xa_prepare && rm->xa_prepare(rm->rm_ctx, &txn->current_xid) == XA_OK) c++;
    }
    (void)xid; return c;
}

int32_t xa_handle_heuristic(XATransaction *txn, XID *xid) {
    int32_t hc = 0;
    for (int32_t i = 0; i < txn->rm_count; i++) {
        XAResourceManager *rm = &txn->rms[i];
        if (rm->xa_commit) {
            int32_t rc = rm->xa_commit(rm->rm_ctx, &txn->current_xid, false);
            if (rc >= XA_HEURHAZ) {
                printf("  [XA] Heuristic on RM %s: %s\n", rm->name, xa_ret_code_str(rc));
                hc++;
            }
        }
    }
    (void)xid; return hc;
}

void xa_print_state(XATransaction *txn) {
    printf("=== XA Transaction [%s] ===\n", txn->txn_name);
    printf("  State: %s  RMs: %d\n", xa_state_str(txn->state), txn->rm_count);
    printf("  XID: fmt=%d gtrid_len=%d bqual_len=%d\n",
           txn->current_xid.format_id, txn->current_xid.gtrid_len, txn->current_xid.bqual_len);
    printf("  Enlisted Resource Managers:\n");
    for (int32_t i = 0; i < txn->rm_count; i++)
        printf("    [%d] %s ctx=%p\n", i, txn->rms[i].name, txn->rms[i].rm_ctx);
}