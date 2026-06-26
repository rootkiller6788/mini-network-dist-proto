#include "two_pc.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("2PC test start\n"); fflush(stdout);
    TPCCoordinator c; tpc_coordinator_init(&c, 1);
    TPCParticipant p1, p2;
    tpc_participant_init(&p1, 1, "A");
    tpc_participant_init(&p2, 2, "B");
    tpc_participant_vote(&p1, true);
    tpc_participant_vote(&p2, true);
    tpc_coordinator_add_participant(&c, &p1);
    tpc_coordinator_add_participant(&c, &p2);
    printf("preparing...\n"); fflush(stdout);
    assert(tpc_coordinator_prepare(&c));
    printf("committing...\n"); fflush(stdout);
    assert(tpc_coordinator_commit(&c));
    printf("2PC test PASSED\n");
    return 0;
}
