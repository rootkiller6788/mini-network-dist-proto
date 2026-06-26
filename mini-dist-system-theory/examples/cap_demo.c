#include "cap_theorem.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    DistributedStore cp_store, ap_store;
    char buf[CAP_MAX_VALUE_LEN];
    bool ok;

    printf("========================================\n");
    printf("  CAP Theorem Demonstration\n");
    printf("  Side-by-side: CP vs AP behavior\n");
    printf("========================================\n\n");

    cap_configure(&cp_store, CP_MODE);
    cap_print_mode(&cp_store);

    cap_configure(&ap_store, AP_MODE);
    cap_print_mode(&ap_store);

    printf("\n--- Phase 1: No partition, both write successfully ---\n");
    ok = cap_write(&cp_store, 0, "hello");
    printf("CP write result: %s\n\n", ok ? "OK" : "FAIL");

    ok = cap_write(&ap_store, 0, "hello");
    printf("AP write result: %s\n\n", ok ? "OK" : "FAIL");

    printf("\n--- Phase 2: Create partition between node 0 and nodes 1,2 ---\n");
    cap_create_partition(&cp_store, 0, 1);
    cap_create_partition(&cp_store, 0, 2);
    cap_create_partition(&ap_store, 0, 1);
    cap_create_partition(&ap_store, 0, 2);

    cap_print_mode(&cp_store);
    cap_print_mode(&ap_store);

    printf("\n--- Phase 3: Write during partition ---\n");
    printf(">>> Writing 'cp-data' to CP store via node 0 (isolated):\n");
    ok = cap_write(&cp_store, 0, "cp-data");
    printf("CP write result: %s\n\n", ok ? "OK" : "FAIL");

    printf(">>> Writing 'ap-data' to AP store via node 0 (isolated):\n");
    ok = cap_write(&ap_store, 0, "ap-data");
    printf("AP write result: %s\n\n", ok ? "OK" : "FAIL");

    printf("--- Phase 4: Read during partition ---\n");
    printf(">>> Reading from CP store node 0:\n");
    cap_read(&cp_store, 0, buf, sizeof(buf));
    printf(">>> Reading from CP store node 1 (in majority):\n");
    cap_read(&cp_store, 1, buf, sizeof(buf));

    printf("\n>>> Reading from AP store node 0:\n");
    cap_read(&ap_store, 0, buf, sizeof(buf));
    printf(">>> Reading from AP store node 1 (diverged):\n");
    cap_read(&ap_store, 1, buf, sizeof(buf));

    printf("\n--- Phase 5: Heal partition ---\n");
    cap_heal_partition(&cp_store, 0, 1);
    cap_heal_partition(&cp_store, 0, 2);
    cap_heal_partition(&ap_store, 0, 1);
    cap_heal_partition(&ap_store, 0, 2);

    printf("\n--- Phase 6: Read after healing ---\n");
    printf(">>> CP store node 0:\n");
    cap_read(&cp_store, 0, buf, sizeof(buf));
    printf(">>> CP store node 1:\n");
    cap_read(&cp_store, 1, buf, sizeof(buf));

    printf("\n>>> AP store node 0:\n");
    cap_read(&ap_store, 0, buf, sizeof(buf));
    printf(">>> AP store node 1:\n");
    cap_read(&ap_store, 1, buf, sizeof(buf));

    printf("\n*** END: During partition, CP rejects writes to maintain consistency.\n");
    printf("*** AP accepts writes but nodes diverge (sacrificing consistency).\n");

    return 0;
}
