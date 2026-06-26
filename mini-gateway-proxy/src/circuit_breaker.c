#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "circuit_breaker.h"

static uint64_t timespec_to_ms(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * 1000 + (uint64_t)ts->tv_nsec / 1000000;
}

static uint64_t elapsed_ms(const struct timespec *start, const struct timespec *now)
{
    uint64_t start_ms = timespec_to_ms(start);
    uint64_t now_ms = timespec_to_ms(now);
    return (now_ms > start_ms) ? (now_ms - start_ms) : 0;
}

const char* cb_state_name(CBCircuitState state)
{
    switch (state) {
    case CB_CLOSED:    return "CLOSED";
    case CB_OPEN:      return "OPEN";
    case CB_HALF_OPEN: return "HALF_OPEN";
    default:           return "UNKNOWN";
    }
}

CBCircuit* cb_init(const char *name, int failure_threshold,
                   int success_threshold, uint64_t timeout_ms)
{
    CBCircuit *cb = calloc(1, sizeof(CBCircuit));
    if (!cb) return NULL;

    if (name) snprintf(cb->name, sizeof(cb->name), "%s", name);
    cb->state = CB_CLOSED;
    cb->failure_count = 0;
    cb->failure_threshold = failure_threshold > 0 ? failure_threshold : 5;
    cb->success_count_in_half_open = 0;
    cb->success_threshold = success_threshold > 0 ? success_threshold : 3;
    cb->half_open_max_requests = success_threshold;
    cb->half_open_current_requests = 0;
    cb->timeout_ms = timeout_ms > 0 ? timeout_ms : 30000;
    cb->total_successes = 0;
    cb->total_failures = 0;
    cb->total_rejected = 0;
    memset(&cb->last_failure_time, 0, sizeof(cb->last_failure_time));
    memset(&cb->opened_at, 0, sizeof(cb->opened_at));

    printf("[cb] Initialized circuit breaker '%s' "
           "(threshold=%d, timeout=%llums)\n",
           cb->name, cb->failure_threshold,
           (unsigned long long)cb->timeout_ms);
    return cb;
}

void cb_set_half_open_max(CBCircuit *cb, int max_requests)
{
    if (cb) cb->half_open_max_requests = max_requests;
}

bool cb_is_open(const CBCircuit *cb)
{
    return cb ? (cb->state == CB_OPEN) : false;
}

int cb_call(CBCircuit *cb, CBCallFunc func, void *arg)
{
    if (!cb) return -1;

    switch (cb->state) {
    case CB_CLOSED: {
        int ret = func(arg);
        if (ret == 0) {
            cb_on_success(cb);
        } else {
            cb_on_failure(cb);
        }
        return ret;
    }

    case CB_OPEN: {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t elapsed = elapsed_ms(&cb->opened_at, &now);

        if (elapsed >= cb->timeout_ms) {
            cb->state = CB_HALF_OPEN;
            cb->success_count_in_half_open = 0;
            cb->half_open_current_requests = 0;
            printf("[cb:%s] timeout expired (%llums), transitioning to HALF_OPEN\n",
                   cb->name, (unsigned long long)elapsed);

            cb->half_open_current_requests++;
            int ret = func(arg);
            if (ret == 0) {
                cb_on_success(cb);
            } else {
                cb_on_failure(cb);
            }
            return ret;
        } else {
            cb->total_rejected++;
            printf("[cb:%s] OPEN: fast-fail (%llums remaining)\n",
                   cb->name,
                   (unsigned long long)(cb->timeout_ms - elapsed));
            return -1; /* fast-fail */
        }
    }

    case CB_HALF_OPEN: {
        if (cb->half_open_current_requests >= cb->half_open_max_requests) {
            cb->total_rejected++;
            printf("[cb:%s] HALF_OPEN: request limit reached, fast-fail\n",
                   cb->name);
            return -1;
        }

        cb->half_open_current_requests++;
        int ret = func(arg);
        if (ret == 0) {
            cb_on_success(cb);
        } else {
            cb_on_failure(cb);
        }
        return ret;
    }
    }

    return -1;
}

void cb_on_success(CBCircuit *cb)
{
    if (!cb) return;
    cb->total_successes++;

    switch (cb->state) {
    case CB_CLOSED:
        cb->failure_count = 0;
        break;

    case CB_HALF_OPEN:
        cb->success_count_in_half_open++;
        if (cb->success_count_in_half_open >= cb->success_threshold) {
            cb->state = CB_CLOSED;
            cb->failure_count = 0;
            cb->half_open_current_requests = 0;
            printf("[cb:%s] HALF_OPEN probe succeeded %d/%d, CLOSING circuit\n",
                   cb->name, cb->success_count_in_half_open,
                   cb->success_threshold);
        }
        break;

    case CB_OPEN:
        break;
    }
}

void cb_on_failure(CBCircuit *cb)
{
    if (!cb) return;
    cb->total_failures++;
    clock_gettime(CLOCK_MONOTONIC, &cb->last_failure_time);

    switch (cb->state) {
    case CB_CLOSED:
        cb->failure_count++;
        printf("[cb:%s] failure %d/%d (CLOSED)\n",
               cb->name, cb->failure_count, cb->failure_threshold);
        if (cb->failure_count >= cb->failure_threshold) {
            cb->state = CB_OPEN;
            clock_gettime(CLOCK_MONOTONIC, &cb->opened_at);
            cb->half_open_current_requests = 0;
            printf("[cb:%s] threshold exceeded, OPENING circuit "
                   "(timeout=%llums)\n",
                   cb->name, (unsigned long long)cb->timeout_ms);
        }
        break;

    case CB_HALF_OPEN:
        cb->state = CB_OPEN;
        clock_gettime(CLOCK_MONOTONIC, &cb->opened_at);
        cb->half_open_current_requests = 0;
        printf("[cb:%s] HALF_OPEN probe failed, re-OPENING circuit\n",
               cb->name);
        break;

    case CB_OPEN:
        break;
    }
}

void cb_reset(CBCircuit *cb)
{
    if (!cb) return;
    cb->state = CB_CLOSED;
    cb->failure_count = 0;
    cb->success_count_in_half_open = 0;
    cb->half_open_current_requests = 0;
    printf("[cb:%s] reset to CLOSED\n", cb->name);
}

void cb_print_state(const CBCircuit *cb)
{
    if (!cb) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    printf("=== Circuit Breaker '%s' ===\n", cb->name);
    printf("  State:        %s\n", cb_state_name(cb->state));
    printf("  Failures:     %d/%d\n", cb->failure_count, cb->failure_threshold);
    printf("  Half-open ok: %d/%d (current: %d)\n",
           cb->success_count_in_half_open, cb->success_threshold,
           cb->half_open_current_requests);
    printf("  Timeout:      %llums\n", (unsigned long long)cb->timeout_ms);

    if (cb->state == CB_OPEN) {
        uint64_t elapsed = elapsed_ms(&cb->opened_at, &now);
        if (elapsed < cb->timeout_ms) {
            uint64_t remaining = cb->timeout_ms - elapsed;
            printf("  Remaining:    %llums\n", (unsigned long long)remaining);
        }
    }

    printf("  Total:        %d success, %d failures, %d rejected\n",
           cb->total_successes, cb->total_failures, cb->total_rejected);
    printf("=============================\n");
}
