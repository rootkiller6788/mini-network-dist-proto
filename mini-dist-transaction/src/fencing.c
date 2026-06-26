#include "fencing.h"
#include <stdio.h>
#include <string.h>

void fencing_token_init(FencingToken *t, const char *resource, const char *owner) {
    t->value = 0; t->generation = 0; t->issued_at_ms = 0; t->valid = false;
    strncpy(t->resource, resource ? resource : "", FENCING_MAX_RESOURCE_LEN - 1);
    t->resource[FENCING_MAX_RESOURCE_LEN - 1] = '\0';
    strncpy(t->owner, owner ? owner : "", FENCING_MAX_OWNER_LEN - 1);
    t->owner[FENCING_MAX_OWNER_LEN - 1] = '\0';
}

void fencing_store_init(FencingTokenStore *store, const char *validator_id) {
    store->count = 0; store->global_counter = 1;
    strncpy(store->validator_id, validator_id ? validator_id : "default", sizeof(store->validator_id) - 1);
    store->validator_id[sizeof(store->validator_id) - 1] = '\0';
    memset(store->tokens, 0, sizeof(store->tokens));
}

void fencing_guard_init(FencingGuard *guard) {
    guard->highest_seen = 0; guard->rejected_count = 0; guard->accepted_count = 0;
}

static int32_t fencing_find_token(FencingTokenStore *store, const char *resource) {
    for (int32_t i = 0; i < store->count; i++)
        if (strcmp(store->tokens[i].resource, resource) == 0) return i;
    return -1;
}

FencingToken *fencing_issue_token(FencingTokenStore *store, const char *resource, const char *owner) {
    int32_t idx = fencing_find_token(store, resource);
    if (idx < 0) {
        if (store->count >= FENCING_MAX_TOKENS) return NULL;
        idx = store->count++;
        fencing_token_init(&store->tokens[idx], resource, owner);
    }
    FencingToken *t = &store->tokens[idx];
    t->value = store->global_counter++; t->generation++;
    t->issued_at_ms = (int64_t)time(NULL); t->valid = true;
    strncpy(t->owner, owner, FENCING_MAX_OWNER_LEN - 1);
    t->owner[FENCING_MAX_OWNER_LEN - 1] = '\0';
    return t;
}

bool fencing_validate_token(FencingGuard *guard, const FencingToken *token) {
    if (!guard || !token || !token->valid) return false;
    return fencing_check_and_advance(guard, token->value);
}

bool fencing_check_and_advance(FencingGuard *guard, int64_t token_value) {
    if (token_value < guard->highest_seen) { guard->rejected_count++; return false; }
    if (token_value > guard->highest_seen) guard->highest_seen = token_value;
    guard->accepted_count++;
    return true;
}

bool fencing_revoke_token(FencingTokenStore *store, const char *resource) {
    int32_t idx = fencing_find_token(store, resource);
    if (idx < 0) return false;
    store->tokens[idx].valid = false;
    return true;
}

FencingToken *fencing_get_current(FencingTokenStore *store, const char *resource) {
    int32_t idx = fencing_find_token(store, resource);
    if (idx < 0 || !store->tokens[idx].valid) return NULL;
    return &store->tokens[idx];
}

int64_t fencing_next_value(FencingTokenStore *store) { return store->global_counter; }

bool fencing_is_monotonic(FencingTokenStore *store) {
    if (store->count <= 1) return true;
    for (int32_t i = 0; i < store->count; i++) {
        if (!store->tokens[i].valid) continue;
        if (store->tokens[i].value >= store->global_counter) return false;
    }
    return true;
}

void fencing_print_store(FencingTokenStore *store) {
    printf("=== Fencing Token Store [%s] ===\n", store->validator_id);
    printf("  Tokens: %d  Global counter: %lld\n", store->count, (long long)store->global_counter);
    for (int32_t i = 0; i < store->count; i++) {
        FencingToken *t = &store->tokens[i];
        printf("    [%s] owner=%s value=%lld gen=%lld valid=%s\n",
               t->resource, t->owner, (long long)t->value, (long long)t->generation,
               t->valid ? "yes" : "no");
    }
}

void fencing_print_guard(FencingGuard *guard) {
    printf("=== Fencing Guard ===\n");
    printf("  Highest seen: %lld  Accepted: %lld  Rejected: %lld\n",
           (long long)guard->highest_seen, (long long)guard->accepted_count,
           (long long)guard->rejected_count);
}