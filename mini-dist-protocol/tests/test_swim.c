#include "swim.h"
#include <assert.h>
#include <stdio.h>

static void test_init(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 5);
    assert(cluster.member_count == 5);
    for (int i = 0; i < 5; i++) {
        assert(cluster.members[i].id == i);
        assert(cluster.members[i].state == SWIM_ALIVE);
        assert(cluster.members[i].active == true);
    }
}

static void test_state_names(void) {
    assert(swim_state_name(SWIM_ALIVE) != NULL);
    assert(swim_state_name(SWIM_SUSPECTED) != NULL);
    assert(swim_state_name(SWIM_DEAD) != NULL);
}

static void test_msg_names(void) {
    assert(swim_msg_name(SWIM_MSG_PING) != NULL);
    assert(swim_msg_name(SWIM_MSG_ACK) != NULL);
    assert(swim_msg_name(SWIM_MSG_INDIRECT_PING) != NULL);
}

static void test_ping_success(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 3);

    bool ok = swim_ping(&cluster, 0, 1);
    assert(ok);
    assert(cluster.members[0].ping_target == 1);

    ok = swim_ping_success(&cluster, 0, 1, 0);
    assert(ok);
    assert(cluster.members[1].last_heard_ms == cluster.protocol_time_ms);
}

static void test_suspect_and_dead(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 3);

    assert(swim_alive_count(&cluster) == 3);
    assert(swim_suspected_count(&cluster) == 0);
    assert(swim_dead_count(&cluster) == 0);

    swim_suspect(&cluster, 1);
    assert(cluster.members[1].state == SWIM_SUSPECTED);
    assert(swim_alive_count(&cluster) == 2);
    assert(swim_suspected_count(&cluster) == 1);

    swim_confirm_dead(&cluster, 1);
    assert(cluster.members[1].state == SWIM_DEAD);
    assert(swim_dead_count(&cluster) == 1);
}

static void test_join_and_leave(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 3);

    swim_join(&cluster, 10, 0x0A000001, 0);
    assert(cluster.member_count == 4);
    assert(cluster.members[3].id == 10);

    swim_leave(&cluster, 10);
    assert(cluster.members[3].state == SWIM_DEAD);
}

static void test_random_member(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 5);

    int target = swim_random_member(&cluster, 0);
    assert(target >= 0 && target < 5 && target != 0);
}

static void test_create_messages(void) {
    SWIMMessage ping = swim_create_ping(0, 1);
    assert(ping.type == SWIM_MSG_PING);
    assert(ping.sender_id == 0);
    assert(ping.target_id == 1);

    SWIMMessage ack = swim_create_ack(1, 0, 42);
    assert(ack.type == SWIM_MSG_ACK);
    assert(ack.incarnation == 42);
}

static void test_tick_progression(void) {
    SWIMCluster cluster;
    swim_init(&cluster, 5);

    swim_tick(&cluster, 100);
    assert(cluster.protocol_time_ms == 100);

    /* Suspect then let dead timeout expire */
    swim_suspect(&cluster, 2);
    cluster.members[2].suspect_since_ms = 0;
    cluster.protocol_time_ms = SWIM_DEAD_TIMEOUT_MS + 1;
    swim_tick(&cluster, 0);
    assert(cluster.members[2].state == SWIM_DEAD);
}

int main(void) {
    printf("Running SWIM tests...\n");
    test_init();
    test_state_names();
    test_msg_names();
    test_ping_success();
    test_suspect_and_dead();
    test_join_and_leave();
    test_random_member();
    test_create_messages();
    test_tick_progression();
    printf("All SWIM tests passed!\n");
    return 0;
}
