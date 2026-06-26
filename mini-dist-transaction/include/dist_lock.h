#ifndef DIST_LOCK_H
#define DIST_LOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define DIST_LOCK_MAX_LOCKS 64
#define DIST_LOCK_DEFAULT_LEASE_MS 30000
#define DIST_LOCK_MAX_OWNER_LEN 64
#define DIST_LOCK_MAX_RESOURCE_LEN 64
#define DIST_LOCK_WAIT_QUEUE_SIZE 16
#define DIST_LOCK_HEARTBEAT_INTERVAL_MS 10000
#define DIST_LOCK_REDLOCK_NODES 5
#define DIST_LOCK_REDLOCK_QUORUM 3

typedef enum {
    LOCK_FREE,
    LOCK_ACQUIRED,
    LOCK_EXPIRED,
    LOCK_WAITING
} LockState;

typedef struct {
    char resource[DIST_LOCK_MAX_RESOURCE_LEN];
    char owner[DIST_LOCK_MAX_OWNER_LEN];
    int64_t lease_timeout_ms;
    int64_t acquired_at_ms;
    int32_t version;
    LockState state;
} DistLock;

typedef struct {
    char owner[DIST_LOCK_MAX_OWNER_LEN];
    int64_t wait_since_ms;
} LockWaitEntry;

typedef struct {
    DistLock locks[DIST_LOCK_MAX_LOCKS];
    LockWaitEntry wait_queue[DIST_LOCK_WAIT_QUEUE_SIZE];
    int32_t lock_count;
    int32_t wait_count;
    int64_t current_time_ms;
} LockManager;

void dist_lock_init(DistLock *lock, const char *resource);
void lock_manager_init(LockManager *lm);
int32_t lock_manager_find_or_create(LockManager *lm, const char *resource);
bool lock_acquire(LockManager *lm, const char *resource, const char *owner,
                  int64_t lease_ms, int64_t now_ms);
bool lock_release(LockManager *lm, const char *resource, const char *owner);
bool lock_renew_lease(LockManager *lm, const char *resource, const char *owner,
                      int64_t extend_ms, int64_t now_ms);
int32_t lock_handle_expiry(LockManager *lm, int64_t now_ms);
bool lock_is_owned_by(LockManager *lm, const char *resource, const char *owner);
int64_t lock_remaining_lease_ms(LockManager *lm, const char *resource, int64_t now_ms);
void lock_manager_print(LockManager *lm);

bool redlock_acquire(LockManager *nodes[], int32_t node_count, int32_t quorum,
                     const char *resource, const char *owner,
                     int64_t lease_ms, int64_t now_ms);
bool redlock_release(LockManager *nodes[], int32_t node_count,
                     const char *resource, const char *owner);
int32_t redlock_majority(int32_t votes_for, int32_t total, int32_t quorum);
const char *lock_state_str(LockState s);

bool lock_process_wait_queue(LockManager *lm, const char *resource, int64_t now_ms);
int32_t lock_deadlock_detect(LockManager *lm);
double lock_contention_ratio(LockManager *lm);
bool lock_try_upgrade(LockManager *lm, const char *resource, const char *owner, int64_t now_ms);

#endif
