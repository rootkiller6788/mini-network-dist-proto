#ifndef CIRCUIT_BREAKER_H
#define CIRCUIT_BREAKER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    CB_CLOSED,
    CB_OPEN,
    CB_HALF_OPEN
} CBCircuitState;

typedef struct {
    CBCircuitState state;
    int            failure_count;
    int            failure_threshold;
    int            success_count_in_half_open;
    int            success_threshold;
    int            half_open_max_requests;
    int            half_open_current_requests;
    uint64_t       timeout_ms;
    struct timespec last_failure_time;
    struct timespec opened_at;
    int            total_successes;
    int            total_failures;
    int            total_rejected;
    char           name[128];
} CBCircuit;

typedef int (*CBCallFunc)(void *arg);

CBCircuit*   cb_init(const char *name, int failure_threshold,
                     int success_threshold, uint64_t timeout_ms);
int          cb_call(CBCircuit *cb, CBCallFunc func, void *arg);
void         cb_on_success(CBCircuit *cb);
void         cb_on_failure(CBCircuit *cb);
bool         cb_is_open(const CBCircuit *cb);
void         cb_reset(CBCircuit *cb);
void         cb_print_state(const CBCircuit *cb);
const char*  cb_state_name(CBCircuitState state);
void         cb_set_half_open_max(CBCircuit *cb, int max_requests);

#endif
