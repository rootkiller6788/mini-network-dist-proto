#include "time_ordering.h"
#include <stdio.h>

static void print_vc(const VectorClock *vc)
{
    int i;
    printf("[");
    for (i = 0; i < vc->node_count; i++) {
        printf("%llu%s", (unsigned long long)vc->counters[i],
               i < vc->node_count - 1 ? ", " : "");
    }
    printf("]");
}

static const char *vc_result_name(VCCompareResult r)
{
    switch (r) {
        case VC_BEFORE:     return "BEFORE (happens-before)";
        case VC_AFTER:      return "AFTER (happens-after)";
        case VC_CONCURRENT: return "CONCURRENT";
        case VC_EQUAL:      return "EQUAL";
        default:            return "?";
    }
}

int main(void)
{
    VectorClock vc_a, vc_b, vc_c;
    LamportClock lc_a, lc_b;

    printf("========================================\n");
    printf("  Vector Clock & Logical Clock Demo\n");
    printf("========================================\n\n");

    printf("--- Part 1: Lamport Clock (Total Order) ---\n");
    lc_a.counter = 0;
    lc_b.counter = 0;

    printf("A increments: %llu\n", (unsigned long long)lamport_increment(&lc_a));
    printf("B increments: %llu\n", (unsigned long long)lamport_increment(&lc_b));
    printf("B receives msg from A (clock=%llu) -> ticks: %llu\n",
           (unsigned long long)lc_a.counter,
           (unsigned long long)lamport_tick(&lc_b, lc_a.counter));
    printf("Lamport total order: if L(a) < L(b), a happened before b (or they're concurrent)\n");

    printf("\n--- Part 2: Vector Clock (Partial Order) ---\n");
    printf("3 nodes: A(ID=0), B(ID=1), C(ID=2)\n\n");

    vector_clock_init(&vc_a, 3);
    vector_clock_init(&vc_b, 3);
    vector_clock_init(&vc_c, 3);

    printf("A does event: ");
    vector_clock_increment(&vc_a, 0);
    print_vc(&vc_a);
    printf("\n");

    printf("B does event: ");
    vector_clock_increment(&vc_b, 1);
    print_vc(&vc_b);
    printf("\n");

    printf("C does event: ");
    vector_clock_increment(&vc_c, 2);
    print_vc(&vc_c);
    printf("\n");

    printf("\nCompare A and B: %s\n",
           vc_result_name(vector_clock_compare(&vc_a, &vc_b)));

    printf("A receives msg from B (vc_b): ");
    vector_clock_merge(&vc_a, &vc_b);
    print_vc(&vc_a);
    printf("\n");

    printf("A does event: ");
    vector_clock_increment(&vc_a, 0);
    print_vc(&vc_a);
    printf("\n");

    printf("Compare new A and B: %s\n",
           vc_result_name(vector_clock_compare(&vc_a, &vc_b)));

    printf("\nB receives msg from C (vc_c): ");
    vector_clock_merge(&vc_b, &vc_c);
    print_vc(&vc_b);
    printf("\n");

    printf("Compare A and B now: %s\n",
           vc_result_name(vector_clock_compare(&vc_a, &vc_b)));

    printf("\n--- Part 3: Scenario with concurrent updates ---\n");
    VectorClock vc_x, vc_y;
    vector_clock_init(&vc_x, 2);
    vector_clock_init(&vc_y, 2);

    printf("X does event: ");
    vector_clock_increment(&vc_x, 0);
    print_vc(&vc_x);
    printf(" -> Y does event: ");
    vector_clock_increment(&vc_y, 1);
    print_vc(&vc_y);
    printf("\n");

    printf("X does another event: ");
    vector_clock_increment(&vc_x, 0);
    print_vc(&vc_x);
    printf("\n");

    printf("Compare X and Y: %s\n",
           vc_result_name(vector_clock_compare(&vc_x, &vc_y)));

    printf("X merges Y: ");
    vector_clock_merge(&vc_x, &vc_y);
    print_vc(&vc_x);
    printf("\n");

    printf("\n--- Part 4: Hybrid Logical Clock ---\n");
    HybridLogicalClock hlc = {0, 0};
    hlc_tick(&hlc);
    printf("HLC tick: physical=%llu logical=%llu\n",
           (unsigned long long)hlc.physical_time_us,
           (unsigned long long)hlc.logical_counter);
    hlc_tick(&hlc);
    printf("HLC tick: physical=%llu logical=%llu\n",
           (unsigned long long)hlc.physical_time_us,
           (unsigned long long)hlc.logical_counter);

    printf("\n*** Summary: Lamport clocks give total order (L(a)<L(b)).\n");
    printf("*** Vector clocks give partial order (before/concurrent/after).\n");
    printf("*** HLC combines physical time with logical counters.\n");

    return 0;
}
