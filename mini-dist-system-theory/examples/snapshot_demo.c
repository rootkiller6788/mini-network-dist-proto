#include "snapshot.h"
#include <stdio.h>

int main(void)
{
    int states[] = {100, 200, 0};
    SnapSystem sys;
    snap_init_system(&sys, 3, states);
    snap_add_channel(&sys, 0, 1);
    snap_add_channel(&sys, 1, 2);
    snap_add_channel(&sys, 2, 0);

    printf("=== Chandy-Lamport Snapshot Demo ===\n");
    printf("3 processes with a ring topology\n");
    printf("Initiating snapshot from P0...\n");

    snap_initiate_snapshot(&sys, 0);
    snap_print_snapshot(&sys);
    printf("Snapshot complete: %s\n", snap_is_complete(&sys) ? "YES" : "NO");
    return 0;
}
