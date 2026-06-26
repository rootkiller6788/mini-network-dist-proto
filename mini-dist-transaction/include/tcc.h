#ifndef TCC_H
#define TCC_H

#include <stdbool.h>
#include <stdint.h>

#define TCC_MAX_RESOURCES 4

typedef enum {
    TCC_PHASE_TRY,
    TCC_PHASE_CONFIRM,
    TCC_PHASE_CANCEL
} TCCPhase;

typedef enum {
    TCC_RES_IDLE,
    TCC_RES_RESERVED,
    TCC_RES_CONFIRMED,
    TCC_RES_CANCELLED,
    TCC_RES_FAILED
} TCCResState;

typedef enum {
    TCC_TXN_CREATED,
    TCC_TXN_TRYING,
    TCC_TXN_CONFIRMING,
    TCC_TXN_CANCELLING,
    TCC_TXN_CONFIRMED,
    TCC_TXN_CANCELLED
} TCCTxnStatus;

typedef bool (*TCCTryFn)(void *ctx);
typedef bool (*TCCConfirmFn)(void *ctx);
typedef bool (*TCCCancelFn)(void *ctx);

typedef struct {
    int32_t id;
    TCCTryFn try_fn;
    TCCConfirmFn confirm_fn;
    TCCCancelFn cancel_fn;
    TCCResState state;
    char name[32];
} TCCResource;

typedef struct {
    TCCResource resources[TCC_MAX_RESOURCES];
    int32_t resource_count;
    TCCPhase phase;
    TCCTxnStatus status;
    int32_t tried_count;
    char txn_name[64];
} TCCTransaction;

void tcc_resource_init(TCCResource *r, int32_t id, const char *name,
                       TCCTryFn try_fn, TCCConfirmFn confirm_fn, TCCCancelFn cancel_fn);
void tcc_transaction_init(TCCTransaction *t, const char *name);
void tcc_transaction_add_resource(TCCTransaction *t, TCCResource *r);
bool tcc_try_all(TCCTransaction *t, void **contexts);
bool tcc_confirm_all(TCCTransaction *t, void **contexts);
bool tcc_cancel_all(TCCTransaction *t, void **contexts);
bool tcc_execute(TCCTransaction *t, void **contexts);
const char *tcc_res_state_str(TCCResState s);
const char *tcc_txn_status_str(TCCTxnStatus s);
const char *tcc_phase_str(TCCPhase p);
void tcc_print_state(TCCTransaction *t);

#endif
