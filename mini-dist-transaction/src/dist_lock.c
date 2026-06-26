#include "dist_lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *lock_state_str(LockState s) {
    switch (s) {
    case LOCK_FREE:      return "FREE";
    case LOCK_ACQUIRED:  return "ACQUIRED";
    case LOCK_EXPIRED:   return "EXPIRED";
    case LOCK_WAITING:   return "WAITING";
    default:             return "UNKNOWN";
    }
}

void dist_lock_init(DistLock *lock, const char *resource) {
    strncpy(lock->resource, resource, DIST_LOCK_MAX_RESOURCE_LEN - 1);
    lock->resource[DIST_LOCK_MAX_RESOURCE_LEN - 1] = '\0';
    lock->owner[0] = '\0';
    lock->lease_timeout_ms = 0;
    lock->acquired_at_ms = 0;
    lock->version = 0;
    lock->state = LOCK_FREE;
}

void lock_manager_init(LockManager *lm) {
    lm->lock_count = 0;
    lm->wait_count = 0;
    lm->current_time_ms = 0;
    memset(lm->locks, 0, sizeof(lm->locks));
    memset(lm->wait_queue, 0, sizeof(lm->wait_queue));
}

int32_t lock_manager_find_or_create(LockManager *lm, const char *resource) {
    for (int32_t i = 0; i < lm->lock_count; i++) {
        if (strcmp(lm->locks[i].resource, resource) == 0) {
            return i;
        }
    }
    if (lm->lock_count >= DIST_LOCK_MAX_LOCKS) return -1;
    dist_lock_init(&lm->locks[lm->lock_count], resource);
    int32_t idx = lm->lock_count;
    lm->lock_count++;
    return idx;
}

bool lock_acquire(LockManager *lm, const char *resource, const char *owner,
                  int64_t lease_ms, int64_t now_ms) {
    int32_t idx = lock_manager_find_or_create(lm, resource);
    if (idx < 0) return false;
    DistLock *lock = &lm->locks[idx];

    if (lock->state == LOCK_ACQUIRED && lock->acquired_at_ms + lock->lease_timeout_ms > now_ms) {
        if (lm->wait_count < DIST_LOCK_WAIT_QUEUE_SIZE) {
            LockWaitEntry *w = &lm->wait_queue[lm->wait_count];
            strncpy(w->owner, owner, DIST_LOCK_MAX_OWNER_LEN - 1);
            w->owner[DIST_LOCK_MAX_OWNER_LEN - 1] = '\0';
            w->wait_since_ms = now_ms;
            lm->wait_count++;
            lock->state = LOCK_WAITING;
        }
        return false;
    }

    strncpy(lock->owner, owner, DIST_LOCK_MAX_OWNER_LEN - 1);
    lock->owner[DIST_LOCK_MAX_OWNER_LEN - 1] = '\0';
    lock->lease_timeout_ms = lease_ms;
    lock->acquired_at_ms = now_ms;
    lock->version++;
    lock->state = LOCK_ACQUIRED;
    return true;
}

bool lock_release(LockManager *lm, const char *resource, const char *owner) {
    int32_t idx = lock_manager_find_or_create(lm, resource);
    if (idx < 0) return false;
    DistLock *lock = &lm->locks[idx];

    if (lock->state != LOCK_ACQUIRED) return false;
    if (strcmp(lock->owner, owner) != 0) return false;

    lock->state = LOCK_FREE;
    lock->owner[0] = '\0';
    lock->lease_timeout_ms = 0;

    if (lm->wait_count > 0) {
        for (int32_t i = 0; i < lm->wait_count - 1; i++) {
            lm->wait_queue[i] = lm->wait_queue[i + 1];
        }
        lm->wait_count--;
    }
    return true;
}

bool lock_renew_lease(LockManager *lm, const char *resource, const char *owner,
                      int64_t extend_ms, int64_t now_ms) {
    int32_t idx = lock_manager_find_or_create(lm, resource);
    if (idx < 0) return false;
    DistLock *lock = &lm->locks[idx];

    if (lock->state != LOCK_ACQUIRED) return false;
    if (strcmp(lock->owner, owner) != 0) return false;

    lock->lease_timeout_ms += extend_ms;
    lock->acquired_at_ms = now_ms;
    return true;
}

int32_t lock_handle_expiry(LockManager *lm, int64_t now_ms) {
    int32_t expired_count = 0;
    for (int32_t i = 0; i < lm->lock_count; i++) {
        DistLock *lock = &lm->locks[i];
        if (lock->state == LOCK_ACQUIRED &&
            lock->acquired_at_ms + lock->lease_timeout_ms <= now_ms) {
            lock->state = LOCK_EXPIRED;
            expired_count++;
        }
    }
    return expired_count;
}

bool lock_is_owned_by(LockManager *lm, const char *resource, const char *owner) {
    int32_t idx = lock_manager_find_or_create(lm, resource);
    if (idx < 0) return false;
    DistLock *lock = &lm->locks[idx];
    return lock->state == LOCK_ACQUIRED && strcmp(lock->owner, owner) == 0;
}

int64_t lock_remaining_lease_ms(LockManager *lm, const char *resource, int64_t now_ms) {
    int32_t idx = lock_manager_find_or_create(lm, resource);
    if (idx < 0) return 0;
    DistLock *lock = &lm->locks[idx];
    if (lock->state != LOCK_ACQUIRED) return 0;
    int64_t remaining = lock->acquired_at_ms + lock->lease_timeout_ms - now_ms;
    return remaining > 0 ? remaining : 0;
}

void lock_manager_print(LockManager *lm) {
    printf("=== Lock Manager ===\n");
    printf("  Locks: %d  Wait queue: %d\n", lm->lock_count, lm->wait_count);
    for (int32_t i = 0; i < lm->lock_count; i++) {
        DistLock *l = &lm->locks[i];
        printf("    [%s] owner=%-16s state=%-10s version=%d lease=%lldms\n",
               l->resource, l->owner[0] ? l->owner : "(none)",
               lock_state_str(l->state), l->version,
               (long long)l->lease_timeout_ms);
    }
    if (lm->wait_count > 0) {
        printf("  Wait queue:\n");
        for (int32_t i = 0; i < lm->wait_count; i++) {
            printf("    [%s] waiting since %lldms\n",
                   lm->wait_queue[i].owner,
                   (long long)lm->wait_queue[i].wait_since_ms);
        }
    }
}

int32_t redlock_majority(int32_t votes_for, int32_t total, int32_t quorum) {
    return votes_for >= quorum && votes_for > total / 2;
}

bool redlock_acquire(LockManager *nodes[], int32_t node_count, int32_t quorum,
                     const char *resource, const char *owner,
                     int64_t lease_ms, int64_t now_ms) {
    int32_t acquired = 0;
    for (int32_t i = 0; i < node_count; i++) {
        if (lock_acquire(nodes[i], resource, owner, lease_ms, now_ms)) {
            acquired++;
        }
    }
    if (!redlock_majority(acquired, node_count, quorum)) {
        redlock_release(nodes, node_count, resource, owner);
        return false;
    }
    return true;
}

bool redlock_release(LockManager *nodes[], int32_t node_count,
                     const char *resource, const char *owner) {
    int32_t released = 0;
    for (int32_t i = 0; i < node_count; i++) {
        if (lock_release(nodes[i], resource, owner)) {
            released++;
        }
    }
    return released > 0;
}
