#ifndef SAGA_H
#define SAGA_H

#include <stdbool.h>
#include <stdint.h>

#define SAGA_MAX_STEPS 16
#define SAGA_MAX_RETRIES 3

typedef enum {
    SAGA_STEP_PENDING,
    SAGA_STEP_SUCCEEDED,
    SAGA_STEP_FAILED,
    SAGA_STEP_RUNNING
} SagaStepStatus;

typedef enum {
    SAGA_TXN_CREATED,
    SAGA_TXN_RUNNING,
    SAGA_TXN_COMPLETED,
    SAGA_TXN_COMPENSATING,
    SAGA_TXN_COMPENSATED,
    SAGA_TXN_FAILED
} SagaTxnStatus;

typedef bool (*SagaActionFn)(void *ctx);
typedef bool (*SagaCompensateFn)(void *ctx);

typedef struct {
    int32_t id;
    SagaActionFn action;
    SagaCompensateFn compensate;
    SagaStepStatus status;
    char name[48];
} SagaStep;

typedef struct {
    int32_t txn_id;
    SagaStep steps[SAGA_MAX_STEPS];
    int32_t step_count;
    int32_t current_step;
    SagaTxnStatus status;
} SagaTransaction;

void saga_step_init(SagaStep *s, int32_t id, const char *name,
                    SagaActionFn action, SagaCompensateFn compensate);
void saga_transaction_init(SagaTransaction *st, int32_t txn_id);
void saga_transaction_add_step(SagaTransaction *st, SagaStep *s);
bool saga_execute(SagaTransaction *st, void **contexts);
bool saga_retry(SagaTransaction *st, void **contexts);
void saga_print_log(SagaTransaction *st);
const char *saga_step_status_str(SagaStepStatus s);
const char *saga_txn_status_str(SagaTxnStatus s);

int32_t saga_count_succeeded(SagaTransaction *st);
int32_t saga_count_failed(SagaTransaction *st);
void saga_reset_steps(SagaTransaction *st);
bool saga_is_compensated(SagaTransaction *st);
bool saga_is_completed(SagaTransaction *st);
int32_t saga_get_step_index(SagaTransaction *st, int32_t step_id);

#endif
