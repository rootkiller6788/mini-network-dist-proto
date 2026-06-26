#include "idempotency.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#else
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#endif

const char *idempotent_status_str(IdempotentStatus s) {
    switch (s) {
    case REQ_NEW:         return "NEW";
    case REQ_PROCESSING:  return "PROCESSING";
    case REQ_PROCESSED:   return "PROCESSED";
    default:              return "UNKNOWN";
    }
}

void idempotent_store_init(IdempotentStore *store) {
    store->count = 0;
    store->lru_counter = 0;
    memset(store->processed_ids, 0, sizeof(store->processed_ids));
}

int32_t idempotent_store_find(IdempotentStore *store, const char *request_id) {
    for (int32_t i = 0; i < store->count; i++) {
        if (strcmp(store->processed_ids[i].request_id, request_id) == 0) {
            store->processed_ids[i].lru_counter = ++store->lru_counter;
            return i;
        }
    }
    return -1;
}

IdempotentStatus idempotent_check(IdempotentStore *store, const char *request_id,
                                   const char *method, const char *resource,
                                   uint64_t body_hash, char *cached_result_out) {
    int32_t idx = idempotent_store_find(store, request_id);
    if (idx >= 0) {
        IdempotentRequest *req = &store->processed_ids[idx];
        if (req->status == REQ_PROCESSED && cached_result_out) {
            strncpy(cached_result_out, req->cached_result, 255);
            cached_result_out[255] = '\0';
        }
        return req->status;
    }
    return REQ_NEW;
}

bool idempotent_record(IdempotentStore *store, const char *request_id,
                       const char *method, const char *resource,
                       uint64_t body_hash, const char *result) {
    int32_t idx = idempotent_store_find(store, request_id);

    if (idx < 0) {
        if (store->count >= IDEMPOTENT_CACHE_SIZE) {
            idempotent_store_evict_lru(store);
        }
        idx = store->count;
        store->count++;

        strncpy(store->processed_ids[idx].request_id, request_id, 63);
        store->processed_ids[idx].request_id[63] = '\0';
        strncpy(store->processed_ids[idx].method, method, 15);
        store->processed_ids[idx].method[15] = '\0';
        strncpy(store->processed_ids[idx].resource, resource, 127);
        store->processed_ids[idx].resource[127] = '\0';
        store->processed_ids[idx].body_hash = body_hash;
        store->processed_ids[idx].timestamp = (int64_t)time(NULL);
    }

    store->processed_ids[idx].status = REQ_PROCESSED;
    store->processed_ids[idx].lru_counter = ++store->lru_counter;

    if (result) {
        strncpy(store->processed_ids[idx].cached_result, result, 255);
        store->processed_ids[idx].cached_result[255] = '\0';
    }
    return true;
}

void idempotent_store_evict_lru(IdempotentStore *store) {
    int32_t min_idx = 0;
    int32_t min_lru = store->processed_ids[0].lru_counter;

    for (int32_t i = 1; i < store->count; i++) {
        if (store->processed_ids[i].lru_counter < min_lru) {
            min_lru = store->processed_ids[i].lru_counter;
            min_idx = i;
        }
    }

    for (int32_t i = min_idx; i < store->count - 1; i++) {
        store->processed_ids[i] = store->processed_ids[i + 1];
    }
    store->count--;
}

void retry_policy_init(RetryPolicy *policy, int32_t max_retries,
                       int64_t base_delay_ms, int64_t max_delay_ms, bool use_jitter) {
    policy->max_retries = max_retries;
    policy->base_delay_ms = base_delay_ms;
    policy->max_delay_ms = max_delay_ms;
    policy->use_jitter = use_jitter;
    policy->jitter_factor = 0.25;
}

int64_t idempotent_compute_backoff(const RetryPolicy *policy, int32_t attempt) {
    int64_t delay = policy->base_delay_ms;
    for (int32_t i = 0; i < attempt && delay < policy->max_delay_ms; i++) {
        delay *= 2;
    }
    if (delay > policy->max_delay_ms) delay = policy->max_delay_ms;

    if (policy->use_jitter) {
        double jitter_range = delay * policy->jitter_factor;
        double jitter_val = ((double)rand() / RAND_MAX) * jitter_range;
        delay = (int64_t)(delay - jitter_range / 2 + jitter_val);
    }
    if (delay < 1) delay = 1;
    return delay;
}

bool idempotent_retry_with_backoff(bool (*operation)(void *ctx), void *ctx,
                                    const RetryPolicy *policy) {
    if (!operation) return false;

    for (int32_t attempt = 0; attempt <= policy->max_retries; attempt++) {
        if (attempt > 0) {
            int64_t delay = idempotent_compute_backoff(policy, attempt - 1);
            SLEEP_MS(delay);
            printf("  [RETRY] Attempt %d/%d after %lldms backoff\n",
                   attempt, policy->max_retries, (long long)delay);
        }

        if (operation(ctx)) {
            return true;
        }
    }
    return false;
}

void idempotent_store_print(IdempotentStore *store) {
    printf("=== Idempotent Store ===\n");
    printf("  Entries: %d / %d  LRU counter: %d\n",
           store->count, IDEMPOTENT_CACHE_SIZE, store->lru_counter);
    for (int32_t i = 0; i < store->count; i++) {
        IdempotentRequest *r = &store->processed_ids[i];
        printf("    [%s] %s %s  hash=%016llx  status=%s  lru=%d\n",
               r->request_id, r->method, r->resource,
               (unsigned long long)r->body_hash,
               idempotent_status_str(r->status),
               r->lru_counter);
    }
}
