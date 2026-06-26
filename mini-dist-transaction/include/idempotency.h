#ifndef IDEMPOTENCY_H
#define IDEMPOTENCY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define IDEMPOTENT_CACHE_SIZE 1024
#define IDEMPOTENT_MAX_RETRIES 10
#define IDEMPOTENT_BASE_DELAY_MS 100
#define IDEMPOTENT_MAX_DELAY_MS 30000

typedef enum {
    REQ_NEW,
    REQ_PROCESSING,
    REQ_PROCESSED
} IdempotentStatus;

typedef struct {
    char request_id[64];
    char method[16];
    char resource[128];
    uint64_t body_hash;
    int64_t timestamp;
    IdempotentStatus status;
    char cached_result[256];
    int32_t lru_counter;
} IdempotentRequest;

typedef struct {
    IdempotentRequest processed_ids[IDEMPOTENT_CACHE_SIZE];
    int32_t count;
    int32_t lru_counter;
} IdempotentStore;

typedef struct {
    int32_t max_retries;
    int64_t base_delay_ms;
    int64_t max_delay_ms;
    bool use_jitter;
    double jitter_factor;
} RetryPolicy;

void idempotent_store_init(IdempotentStore *store);
IdempotentStatus idempotent_check(IdempotentStore *store, const char *request_id,
                                   const char *method, const char *resource,
                                   uint64_t body_hash, char *cached_result_out);
bool idempotent_record(IdempotentStore *store, const char *request_id,
                       const char *method, const char *resource,
                       uint64_t body_hash, const char *result);
void idempotent_store_evict_lru(IdempotentStore *store);
int32_t idempotent_store_find(IdempotentStore *store, const char *request_id);

int64_t idempotent_compute_backoff(const RetryPolicy *policy, int32_t attempt);
bool idempotent_retry_with_backoff(bool (*operation)(void *ctx), void *ctx,
                                    const RetryPolicy *policy);
void retry_policy_init(RetryPolicy *policy, int32_t max_retries,
                       int64_t base_delay_ms, int64_t max_delay_ms, bool use_jitter);
const char *idempotent_status_str(IdempotentStatus s);
void idempotent_store_print(IdempotentStore *store);

#endif
