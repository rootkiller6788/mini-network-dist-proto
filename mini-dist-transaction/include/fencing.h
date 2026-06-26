#ifndef FENCING_H
#define FENCING_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define FENCING_MAX_TOKENS 128
#define FENCING_MAX_OWNER_LEN 48
#define FENCING_MAX_RESOURCE_LEN 64

/* L1: Fencing Token -- prevents split-brain in distributed locks
 *
 * Theorem (Fencing Token Safety):
 *   If a storage system checks fencing tokens on every write and rejects
 *   tokens less than the highest-seen token, then no stale writer can
 *   corrupt data, regardless of clock skew or GC pauses.
 *
 * Reference: Kleppmann (2017) "Designing Data-Intensive Applications", Ch.8
 */

typedef struct {
    int64_t value;
    int64_t generation;
    int64_t issued_at_ms;
    char owner[FENCING_MAX_OWNER_LEN];
    char resource[FENCING_MAX_RESOURCE_LEN];
    bool valid;
} FencingToken;

typedef struct {
    FencingToken tokens[FENCING_MAX_TOKENS];
    int32_t count;
    int64_t global_counter;
    char validator_id[32];
} FencingTokenStore;

typedef struct {
    int64_t highest_seen;
    int64_t rejected_count;
    int64_t accepted_count;
} FencingGuard;

void fencing_token_init(FencingToken *t, const char *resource, const char *owner);
void fencing_store_init(FencingTokenStore *store, const char *validator_id);
void fencing_guard_init(FencingGuard *guard);

FencingToken *fencing_issue_token(FencingTokenStore *store,
                                   const char *resource, const char *owner);
bool fencing_validate_token(FencingGuard *guard, const FencingToken *token);
bool fencing_revoke_token(FencingTokenStore *store, const char *resource);
bool fencing_check_and_advance(FencingGuard *guard, int64_t token_value);
FencingToken *fencing_get_current(FencingTokenStore *store, const char *resource);

int64_t fencing_next_value(FencingTokenStore *store);
bool fencing_is_monotonic(FencingTokenStore *store);
void fencing_print_store(FencingTokenStore *store);
void fencing_print_guard(FencingGuard *guard);

#endif
