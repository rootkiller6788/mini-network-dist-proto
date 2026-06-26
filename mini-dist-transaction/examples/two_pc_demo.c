#include "two_pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    printf("========================================\n");
    printf("  2PC Demo — All YES → Commit\n");
    printf("========================================\n\n");

    TPCCoordinator coord;
    tpc_coordinator_init(&coord, 1001);

    TPCParticipant p1, p2, p3;
    tpc_participant_init(&p1, 1, "PaymentService");
    tpc_participant_init(&p2, 2, "InventoryService");
    tpc_participant_init(&p3, 3, "OrderService");

    tpc_participant_vote(&p1, true);
    tpc_participant_vote(&p2, true);
    tpc_participant_vote(&p3, true);

    tpc_coordinator_add_participant(&coord, &p1);
    tpc_coordinator_add_participant(&coord, &p2);
    tpc_coordinator_add_participant(&coord, &p3);

    printf("[Coordinator] Sending PREPARE...\n");
    bool prepared = tpc_coordinator_prepare(&coord);
    printf("[Coordinator] PREPARE result: %s\n", prepared ? "ALL_READY" : "ABORT");

    if (prepared) {
        printf("[Coordinator] Sending COMMIT...\n");
        bool committed = tpc_coordinator_commit(&coord);
        printf("[Coordinator] COMMIT result: %s\n", committed ? "COMMITTED" : "FAILED");
    }

    printf("\n");
    tpc_print_state(&coord);

    printf("\n========================================\n");
    printf("  2PC Demo — One NO → Abort\n");
    printf("========================================\n\n");

    TPCCoordinator coord2;
    tpc_coordinator_init(&coord2, 1002);

    TPCParticipant p4, p5, p6;
    tpc_participant_init(&p4, 4, "PaymentService");
    tpc_participant_init(&p5, 5, "InventoryService");
    tpc_participant_init(&p6, 6, "OrderService");

    tpc_participant_vote(&p4, true);
    tpc_participant_vote(&p5, false);
    tpc_participant_vote(&p6, true);

    tpc_coordinator_add_participant(&coord2, &p4);
    tpc_coordinator_add_participant(&coord2, &p5);
    tpc_coordinator_add_participant(&coord2, &p6);

    printf("[Coordinator] Sending PREPARE...\n");
    prepared = tpc_coordinator_prepare(&coord2);
    printf("[Coordinator] PREPARE result: %s\n", prepared ? "ALL_READY" : "ABORT");
    printf("[Coordinator] ABORTING due to NO vote from participant 5\n");

    printf("\n");
    tpc_print_state(&coord2);

    printf("\n========================================\n");
    printf("  2PC Demo — Coordinator Timeout → Abort\n");
    printf("========================================\n\n");

    TPCCoordinator coord3;
    tpc_coordinator_init(&coord3, 1003);

    TPCParticipant p7, p8;
    tpc_participant_init(&p7, 7, "DBNode1");
    tpc_participant_init(&p8, 8, "DBNode2");
    tpc_participant_vote(&p7, true);
    tpc_participant_vote(&p8, true);

    tpc_coordinator_add_participant(&coord3, &p7);
    tpc_coordinator_add_participant(&coord3, &p8);

    printf("[Coordinator] Timeout simulated while waiting...\n");
    tpc_handle_timeout(&coord3);
    printf("[Coordinator] All participants ABORTED\n");

    printf("\n");
    tpc_print_state(&coord3);

    printf("\n========================================\n");
    printf("  3PC Demo — Pre-commit avoids blocking\n");
    printf("========================================\n\n");

    ThreePCCoordinator c3pc;
    threepc_coordinator_init(&c3pc, 2001);

    TPCParticipant q1, q2, q3;
    tpc_participant_init(&q1, 10, "ShardA");
    tpc_participant_init(&q2, 11, "ShardB");
    tpc_participant_init(&q3, 12, "ShardC");
    tpc_participant_vote(&q1, true);
    tpc_participant_vote(&q2, true);
    tpc_participant_vote(&q3, true);

    threepc_coordinator_add_participant(&c3pc, &q1);
    threepc_coordinator_add_participant(&c3pc, &q2);
    threepc_coordinator_add_participant(&c3pc, &q3);

    printf("[3PC] Phase 1: PREPARE...\n");
    if (threepc_prepare(&c3pc)) {
        printf("[3PC] Phase 1: SUCCESS\n");
        printf("[3PC] Phase 2: PRECOMMIT... (coordinator can fail, "
               "participants will commit on timeout)\n");
        if (threepc_precommit(&c3pc)) {
            printf("[3PC] Phase 2: SUCCESS\n");
            printf("[3PC] Phase 3: COMMIT...\n");
            if (threepc_commit(&c3pc)) {
                printf("[3PC] Phase 3: SUCCESS\n");
            }
        }
    }

    printf("\n");
    threepc_print_state(&c3pc);

    printf("\n=== 2PC vs 3PC Comparison ===\n");
    printf("  2PC: coordinator failure during PREPARE → participants block\n");
    printf("  3PC: pre-commit phase → participants can commit on timeout\n");
    printf("  Tradeoff: 3PC adds one round trip but avoids blocking\n");

    return 0;
}
