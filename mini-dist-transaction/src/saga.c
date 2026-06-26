#include "saga.h"

#include <stdio.h>
#include <string.h>

const char *saga_step_status_str(SagaStepStatus s) {
    switch (s) {
    case SAGA_STEP_PENDING:    return "PENDING";
    case SAGA_STEP_SUCCEEDED:  return "SUCCEEDED";
    case SAGA_STEP_FAILED:     return "FAILED";
    case SAGA_STEP_RUNNING:    return "RUNNING";
    default:                   return "UNKNOWN";
    }
}

const char *saga_txn_status_str(SagaTxnStatus s) {
    switch (s) {
    case SAGA_TXN_CREATED:      return "CREATED";
    case SAGA_TXN_RUNNING:      return "RUNNING";
    case SAGA_TXN_COMPLETED:    return "COMPLETED";
    case SAGA_TXN_COMPENSATING: return "COMPENSATING";
    case SAGA_TXN_COMPENSATED:  return "COMPENSATED";
    case SAGA_TXN_FAILED:       return "FAILED";
    default:                    return "UNKNOWN";
    }
}

void saga_step_init(SagaStep *s, int32_t id, const char *name,
                    SagaActionFn action, SagaCompensateFn compensate) {
    s->id = id;
    s->action = action;
    s->compensate = compensate;
    s->status = SAGA_STEP_PENDING;
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
}

void saga_transaction_init(SagaTransaction *st, int32_t txn_id) {
    st->txn_id = txn_id;
    st->step_count = 0;
    st->current_step = 0;
    st->status = SAGA_TXN_CREATED;
    memset(st->steps, 0, sizeof(st->steps));
}

void saga_transaction_add_step(SagaTransaction *st, SagaStep *s) {
    if (st->step_count >= SAGA_MAX_STEPS) return;
    st->steps[st->step_count] = *s;
    st->step_count++;
}

bool saga_execute(SagaTransaction *st, void **contexts) {
    if (st->step_count == 0) return false;
    st->status = SAGA_TXN_RUNNING;
    st->current_step = 0;

    for (int32_t i = 0; i < st->step_count; i++) {
        SagaStep *step = &st->steps[i];
        st->current_step = i;
        step->status = SAGA_STEP_RUNNING;
        printf("  [SAGA] Executing step %d: %s\n", step->id, step->name);

        bool ok = false;
        if (step->action) {
            ok = step->action(contexts ? contexts[i] : NULL);
        }

        if (!ok) {
            step->status = SAGA_STEP_FAILED;
            printf("  [SAGA] Step %d FAILED: %s\n", step->id, step->name);

            st->status = SAGA_TXN_COMPENSATING;
            for (int32_t j = i - 1; j >= 0; j--) {
                SagaStep *cs = &st->steps[j];
                printf("  [SAGA] Compensating step %d: %s\n", cs->id, cs->name);
                if (cs->compensate) {
                    cs->compensate(contexts ? contexts[j] : NULL);
                }
                cs->status = SAGA_STEP_PENDING;
            }
            st->status = SAGA_TXN_COMPENSATED;
            return false;
        }

        step->status = SAGA_STEP_SUCCEEDED;
        printf("  [SAGA] Step %d SUCCEEDED: %s\n", step->id, step->name);
    }

    st->status = SAGA_TXN_COMPLETED;
    return true;
}

bool saga_retry(SagaTransaction *st, void **contexts) {
    if (st->status != SAGA_TXN_COMPENSATED &&
        st->status != SAGA_TXN_FAILED) return false;
    printf("  [SAGA] Retrying transaction %d...\n", st->txn_id);
    for (int32_t i = 0; i < st->step_count; i++) {
        st->steps[i].status = SAGA_STEP_PENDING;
    }
    return saga_execute(st, contexts);
}

void saga_print_log(SagaTransaction *st) {
    printf("=== Saga Transaction %d ===\n", st->txn_id);
    printf("  Status: %s\n", saga_txn_status_str(st->status));
    printf("  Current step: %d / %d\n", st->current_step + 1, st->step_count);
    printf("  Steps:\n");
    for (int32_t i = 0; i < st->step_count; i++) {
        SagaStep *s = &st->steps[i];
        printf("    [%d] %-32s  status=%-12s action=%p compensate=%p\n",
               s->id, s->name, saga_step_status_str(s->status),
               (void *)s->action, (void *)s->compensate);
    }
}

int32_t saga_count_succeeded(SagaTransaction *st) {
    int32_t count = 0;
    for (int32_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].status == SAGA_STEP_SUCCEEDED) count++;
    }
    return count;
}

int32_t saga_count_failed(SagaTransaction *st) {
    int32_t count = 0;
    for (int32_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].status == SAGA_STEP_FAILED) count++;
    }
    return count;
}

void saga_reset_steps(SagaTransaction *st) {
    for (int32_t i = 0; i < st->step_count; i++) {
        st->steps[i].status = SAGA_STEP_PENDING;
    }
    st->current_step = 0;
    st->status = SAGA_TXN_CREATED;
}

bool saga_is_compensated(SagaTransaction *st) {
    return st->status == SAGA_TXN_COMPENSATED;
}

bool saga_is_completed(SagaTransaction *st) {
    return st->status == SAGA_TXN_COMPLETED;
}

int32_t saga_get_step_index(SagaTransaction *st, int32_t step_id) {
    for (int32_t i = 0; i < st->step_count; i++) {
        if (st->steps[i].id == step_id) return i;
    }
    return -1;
}
