#include "crdt.h"
#include <stdio.h>
#include <string.h>

static void print_ors(const ORSet *ors, const char *label)
{
    int i;
    printf("%s: {", label);
    for (i = 0; i < CRDT_SET_SIZE; i++) {
        if (ors_contains(ors, i)) {
            printf(" item%d", i);
        }
    }
    printf(" } (count=%d)\n", ors_count(ors));
}

int main(void)
{
    ORSet replica_a, replica_b;
    GCounter gcounter;
    PNCounter pncounter;
    LWWRegister reg_a, reg_b;

    printf("========================================\n");
    printf("  CRDT Demo: Eventual Consistency\n");
    printf("========================================\n\n");

    printf("--- Part 1: G-Counter (increment-only) ---\n");
    gc_init(&gcounter, 3);
    printf("Initial: %llu\n", (unsigned long long)gc_value(&gcounter));
    gc_inc(&gcounter, 0);
    gc_inc(&gcounter, 0);
    gc_inc(&gcounter, 1);
    printf("After inc(A)x2, inc(B)x1: %llu\n", (unsigned long long)gc_value(&gcounter));
    printf("Converges via max-merge (no conflict resolution needed)\n");

    printf("\n--- Part 2: PN-Counter (increment/decrement) ---\n");
    pn_init(&pncounter, 3);
    pn_inc(&pncounter, 0);
    pn_inc(&pncounter, 0);
    pn_inc(&pncounter, 1);
    pn_dec(&pncounter, 0);
    printf("2 x inc(A), 1 x inc(B), 1 x dec(A) = %lld\n", (long long)pn_value(&pncounter));

    printf("\n--- Part 3: OR-Set (add/remove, add-wins) ---\n");
    printf("Two replicas go offline, make independent changes, then merge:\n\n");

    orset_init(&replica_a, 0);
    orset_init(&replica_b, 1);

    printf("Replica A: add item1, add item2\n");
    ors_add(&replica_a, 1);
    ors_add(&replica_a, 2);
    print_ors(&replica_a, "  A");

    printf("Replica B: add item3, add item4\n");
    ors_add(&replica_b, 3);
    ors_add(&replica_b, 4);
    print_ors(&replica_b, "  B");

    printf("\n--- Offline divergence ---\n");
    printf("A removes item1: ");
    ors_remove(&replica_a, 1);
    print_ors(&replica_a, "  A");
    printf("B removes item3: ");
    ors_remove(&replica_b, 3);
    print_ors(&replica_b, "  B");

    printf("\n--- Merge A <- B ---\n");
    ors_merge(&replica_a, &replica_b);
    print_ors(&replica_a, "  A after merge");
    printf("\n--- Merge B <- A ---\n");
    ors_merge(&replica_b, &replica_a);
    print_ors(&replica_b, "  B after merge");

    printf("\nBoth replicas converge to the same set.\n");

    printf("\n--- Part 4: Add-Wins demo ---\n");
    ORSet replica_x, replica_y;
    orset_init(&replica_x, 0);
    orset_init(&replica_y, 1);

    printf("X adds item5: ");
    ors_add(&replica_x, 5);
    print_ors(&replica_x, "  X");
    printf("Y adds item5 (concurrent): ");
    ors_add(&replica_y, 5);
    print_ors(&replica_y, "  Y");

    printf("X removes item5: ");
    ors_remove(&replica_x, 5);
    print_ors(&replica_x, "  X");

    printf("Merge X into Y: ");
    ors_merge(&replica_y, &replica_x);
    print_ors(&replica_y, "  Y");
    printf("(add-wins: even though X removed, Y's add survives)\n");

    printf("\n--- Part 5: LWW-Register ---\n");
    lww_init(&reg_a, 0);
    lww_init(&reg_b, 0);

    lww_set(&reg_a, "alice-value");
    printf("A sets: '%s' (ts=%llu)\n", lww_get(&reg_a),
           (unsigned long long)reg_a.timestamp);

    lww_set(&reg_b, "bob-value");
    printf("B sets: '%s' (ts=%llu)\n", lww_get(&reg_b),
           (unsigned long long)reg_b.timestamp);

    lww_set(&reg_b, "bob-final");
    printf("B sets later: '%s' (ts=%llu)\n", lww_get(&reg_b),
           (unsigned long long)reg_b.timestamp);

    printf("Merge A <- B: ");
    lww_merge(&reg_a, &reg_b);
    printf("'%s' (highest timestamp wins)\n", lww_get(&reg_a));

    printf("\n*** All CRDTs converge without coordination - eventual consistency. ***\n");
    return 0;
}
