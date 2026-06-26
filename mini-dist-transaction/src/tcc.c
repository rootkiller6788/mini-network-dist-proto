#include "tcc.h"

#include <stdio.h>
#include <string.h>

const char *tcc_res_state_str(TCCResState s) {
    switch (s) {
    case TCC_RES_IDLE:       return "IDLE";
    case TCC_RES_RESERVED:   return "RESERVED";
    case TCC_RES_CONFIRMED:  return "CONFIRMED";
    case TCC_RES_CANCELLED:  return "CANCELLED";
    case TCC_RES_FAILED:     return "FAILED";
    default:                 return "UNKNOWN";
    }
}

const char *tcc_txn_status_str(TCCTxnStatus s) {
    switch (s) {
    case TCC_TXN_CREATED:    return "CREATED";
    case TCC_TXN_TRYING:     return "TRYING";
    case TCC_TXN_CONFIRMING: return "CONFIRMING";
    case TCC_TXN_CANCELLING: return "CANCELLING";
    case TCC_TXN_CONFIRMED:  return "CONFIRMED";
    case TCC_TXN_CANCELLED:  return "CANCELLED";
    default:                 return "UNKNOWN";
    }
}

const char *tcc_phase_str(TCCPhase p) {
    switch (p) {
    case TCC_PHASE_TRY:     return "TRY";
    case TCC_PHASE_CONFIRM: return "CONFIRM";
    case TCC_PHASE_CANCEL:  return "CANCEL";
    default:                return "UNKNOWN";
    }
}

void tcc_resource_init(TCCResource *r, int32_t id, const char *name,
                       TCCTryFn try_fn, TCCConfirmFn confirm_fn, TCCCancelFn cancel_fn) {
    r->id = id;
    r->try_fn = try_fn;
    r->confirm_fn = confirm_fn;
    r->cancel_fn = cancel_fn;
    r->state = TCC_RES_IDLE;
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
}

void tcc_transaction_init(TCCTransaction *t, const char *name) {
    t->resource_count = 0;
    t->phase = TCC_PHASE_TRY;
    t->status = TCC_TXN_CREATED;
    t->tried_count = 0;
    strncpy(t->txn_name, name, sizeof(t->txn_name) - 1);
    t->txn_name[sizeof(t->txn_name) - 1] = '\0';
    memset(t->resources, 0, sizeof(t->resources));
}

void tcc_transaction_add_resource(TCCTransaction *t, TCCResource *r) {
    if (t->resource_count >= TCC_MAX_RESOURCES) return;
    t->resources[t->resource_count] = *r;
    t->resource_count++;
}

bool tcc_try_all(TCCTransaction *t, void **contexts) {
    t->phase = TCC_PHASE_TRY;
    t->status = TCC_TXN_TRYING;
    t->tried_count = 0;

    for (int32_t i = 0; i < t->resource_count; i++) {
        TCCResource *r = &t->resources[i];
        bool ok = false;
        if (r->try_fn) {
            ok = r->try_fn(contexts ? contexts[i] : NULL);
        }
        if (!ok) {
            r->state = TCC_RES_FAILED;
            printf("  [TCC] Try failed on resource %d: %s\n", r->id, r->name);
            return false;
        }
        r->state = TCC_RES_RESERVED;
        t->tried_count++;
        printf("  [TCC] Try succeeded on resource %d: %s\n", r->id, r->name);
    }
    return t->tried_count == t->resource_count && t->resource_count > 0;
}

bool tcc_confirm_all(TCCTransaction *t, void **contexts) {
    if (t->phase != TCC_PHASE_TRY) return false;
    if (t->tried_count != t->resource_count) return false;

    t->phase = TCC_PHASE_CONFIRM;
    t->status = TCC_TXN_CONFIRMING;

    for (int32_t i = 0; i < t->resource_count; i++) {
        TCCResource *r = &t->resources[i];
        if (r->state != TCC_RES_RESERVED) {
            printf("  [TCC] Resource %d not in RESERVED state, aborting confirm\n", r->id);
            tcc_cancel_all(t, contexts);
            return false;
        }
        if (r->confirm_fn) {
            r->confirm_fn(contexts ? contexts[i] : NULL);
        }
        r->state = TCC_RES_CONFIRMED;
    }
    t->status = TCC_TXN_CONFIRMED;
    return true;
}

bool tcc_cancel_all(TCCTransaction *t, void **contexts) {
    t->phase = TCC_PHASE_CANCEL;
    t->status = TCC_TXN_CANCELLING;

    for (int32_t i = 0; i < t->tried_count; i++) {
        TCCResource *r = &t->resources[i];
        if (r->state == TCC_RES_CONFIRMED || r->state == TCC_RES_IDLE) continue;
        if (r->cancel_fn) {
            r->cancel_fn(contexts ? contexts[i] : NULL);
        }
        r->state = TCC_RES_CANCELLED;
    }
    t->status = TCC_TXN_CANCELLED;
    return true;
}

bool tcc_execute(TCCTransaction *t, void **contexts) {
    if (!tcc_try_all(t, contexts)) {
        tcc_cancel_all(t, contexts);
        return false;
    }
    return tcc_confirm_all(t, contexts);
}

void tcc_print_state(TCCTransaction *t) {
    printf("=== TCC Transaction '%s' ===\n", t->txn_name);
    printf("  Status: %s | Phase: %s\n",
           tcc_txn_status_str(t->status), tcc_phase_str(t->phase));
    printf("  Tried: %d / %d\n", t->tried_count, t->resource_count);
    printf("  Resources:\n");
    for (int32_t i = 0; i < t->resource_count; i++) {
        TCCResource *r = &t->resources[i];
        printf("    [%d] %-24s  state=%-12s  try=%p confirm=%p cancel=%p\n",
               r->id, r->name, tcc_res_state_str(r->state),
               (void *)r->try_fn, (void *)r->confirm_fn, (void *)r->cancel_fn);
    }
}
