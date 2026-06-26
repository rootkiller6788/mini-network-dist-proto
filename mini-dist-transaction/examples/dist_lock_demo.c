#include "dist_lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#else
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#endif

int main(void) {
    printf("========================================\n");
    printf("  Distributed Lock — Basic Acquire/Release\n");
    printf("========================================\n\n");

    LockManager lm;
    lock_manager_init(&lm);
    int64_t now = 1000;

    bool ok = lock_acquire(&lm, "resource:order:42", "client-A",
                            DIST_LOCK_DEFAULT_LEASE_MS, now);
    printf("[Client-A] Acquire lock on 'resource:order:42': %s\n",
           ok ? "SUCCESS" : "FAILED");

    printf("[Client-A] Is owned? %s\n",
           lock_is_owned_by(&lm, "resource:order:42", "client-A") ? "yes" : "no");

    int64_t remaining = lock_remaining_lease_ms(&lm, "resource:order:42", now);
    printf("[Client-A] Remaining lease: %lld ms\n", (long long)remaining);

    printf("\n--- Lock contention ---\n");
    ok = lock_acquire(&lm, "resource:order:42", "client-B",
                      DIST_LOCK_DEFAULT_LEASE_MS, now);
    printf("[Client-B] Acquire lock on 'resource:order:42': %s (owned by A)\n",
           ok ? "SUCCESS" : "FAILED");

    printf("\n--- Renew lease (heartbeat) ---\n");
    now += 25000;
    ok = lock_renew_lease(&lm, "resource:order:42", "client-A", 15000, now);
    printf("[Client-A] Renew lease (+15s): %s\n", ok ? "SUCCESS" : "FAILED");
    remaining = lock_remaining_lease_ms(&lm, "resource:order:42", now);
    printf("[Client-A] Remaining lease after renew: %lld ms\n", (long long)remaining);

    printf("\n--- Unauthorized release attempt ---\n");
    ok = lock_release(&lm, "resource:order:42", "client-B");
    printf("[Client-B] Release lock: %s (not owner)\n",
           ok ? "SUCCESS" : "FAILED");

    printf("\n--- Release ---\n");
    ok = lock_release(&lm, "resource:order:42", "client-A");
    printf("[Client-A] Release lock: %s\n", ok ? "SUCCESS" : "FAILED");

    printf("\n");
    lock_manager_print(&lm);

    printf("\n========================================\n");
    printf("  Distributed Lock — Lease Expiry\n");
    printf("========================================\n\n");

    LockManager lm2;
    lock_manager_init(&lm2);
    int64_t now2 = 0;

    lock_acquire(&lm2, "resource:inventory:99", "worker-1", 5000, now2);
    printf("[worker-1] Acquired lock with 5s lease at t=0\n");

    now2 += 3000;
    remaining = lock_remaining_lease_ms(&lm2, "resource:inventory:99", now2);
    printf("[t=3s] Remaining lease: %lld ms\n", (long long)remaining);

    now2 += 3000;
    int32_t expired = lock_handle_expiry(&lm2, now2);
    printf("[t=6s] Expired locks: %d\n", expired);

    ok = lock_acquire(&lm2, "resource:inventory:99", "worker-2", 5000, now2);
    printf("[worker-2] Acquire expired lock: %s\n", ok ? "SUCCESS" : "FAILED");

    printf("\n");
    lock_manager_print(&lm2);

    printf("\n========================================\n");
    printf("  Distributed Lock — Redlock Algorithm\n");
    printf("========================================\n\n");

    LockManager nodes[DIST_LOCK_REDLOCK_NODES];
    LockManager *node_ptrs[DIST_LOCK_REDLOCK_NODES];
    for (int32_t i = 0; i < DIST_LOCK_REDLOCK_NODES; i++) {
        lock_manager_init(&nodes[i]);
        node_ptrs[i] = &nodes[i];
    }

    int64_t now3 = 5000;
    ok = redlock_acquire(node_ptrs, DIST_LOCK_REDLOCK_NODES, DIST_LOCK_REDLOCK_QUORUM,
                         "resource:critical:section", "client-X", 10000, now3);
    printf("[client-X] Redlock acquire: %s\n", ok ? "SUCCESS" : "FAILED");
    printf("  (acquired on %d/%d nodes, quorum=%d)\n",
           DIST_LOCK_REDLOCK_NODES, DIST_LOCK_REDLOCK_NODES, DIST_LOCK_REDLOCK_QUORUM);

    for (int32_t i = 0; i < DIST_LOCK_REDLOCK_NODES; i++) {
        printf("  Node %d: ", i);
        lock_manager_print(&nodes[i]);
    }

    printf("Releasing Redlock...\n");
    redlock_release(node_ptrs, DIST_LOCK_REDLOCK_NODES,
                    "resource:critical:section", "client-X");
    printf("All nodes released.\n");

    printf("\n=== Distributed Lock Comparison ===\n");
    printf("  Redlock: N independent Redis/Memcached nodes, majority quorum\n");
    printf("  Zookeeper: Ephemeral sequential znodes, watch mechanism\n");
    printf("  etcd: Lease-based, watch, compare-and-swap\n");
    printf("  Fencing token: Monotonic token prevents split-brain\n");

    return 0;
}
