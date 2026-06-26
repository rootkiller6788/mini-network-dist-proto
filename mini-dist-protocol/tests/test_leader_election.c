#include "leader_election.h"
#include <assert.h>
#include <stdio.h>

static void test_bully_init(void) {
    BullyCluster cluster;
    bully_init(&cluster, 5);
    assert(cluster.node_count == 5);
    for (int i = 0; i < 5; i++) {
        assert(cluster.nodes[i].id == i);
        assert(cluster.nodes[i].state == BULLY_IDLE);
        assert(cluster.nodes[i].active == true);
    }
}

static void test_bully_election(void) {
    BullyCluster cluster;
    bully_init(&cluster, 5);

    int winner = bully_election(&cluster, 2);
    assert(winner == 4);  /* Highest ID wins */
    for (int i = 0; i < 5; i++)
        assert(cluster.nodes[i].leader_id == winner);
}

static void test_bully_crash_recover(void) {
    BullyCluster cluster;
    bully_init(&cluster, 5);
    bully_election(&cluster, 2);

    bully_node_crash(&cluster, 4);
    assert(!cluster.nodes[4].active);
    /* New leader is highest remaining */
    assert(cluster.nodes[0].leader_id == 3);

    bully_node_recover(&cluster, 4);
    assert(cluster.nodes[4].active);
}

static void test_ring_init(void) {
    RingCluster cluster;
    ring_init(&cluster, 5);
    assert(cluster.node_count == 5);
    for (int i = 0; i < 5; i++) {
        assert(cluster.nodes[i].id == i);
        assert(cluster.nodes[i].next_id == (i + 1) % 5);
    }
}

static void test_ring_election(void) {
    RingCluster cluster;
    ring_init(&cluster, 5);

    int winner = ring_election(&cluster, 0);
    assert(winner == 4);  /* Highest ID wins */
    for (int i = 0; i < 5; i++)
        assert(cluster.nodes[i].leader_id == winner);
}

static void test_ring_crash(void) {
    RingCluster cluster;
    ring_init(&cluster, 5);
    ring_election(&cluster, 0);

    ring_node_crash(&cluster, 4);
    assert(!cluster.nodes[4].active);
}

static void test_ring_pass_token(void) {
    RingCluster cluster;
    ring_init(&cluster, 3);

    /* Manually set up a token on node 0 for pass test */
    cluster.nodes[0].token_owner = true;
    cluster.nodes[0].token.candidate_id = 0;
    cluster.nodes[0].token.highest_id   = 0;
    cluster.nodes[0].token.hop_count    = 0;
    cluster.nodes[0].token.complete     = false;

    bool ok = ring_pass_token(&cluster, 0);
    assert(ok);
    assert(!cluster.nodes[0].token_owner);
    assert(cluster.nodes[1].token_owner);
}

static void test_zk_init(void) {
    ZKCluster cluster;
    zk_init(&cluster, 5);
    assert(cluster.node_count == 5);
    for (int i = 0; i < 5; i++) {
        assert(cluster.nodes[i].id == i);
        assert(cluster.nodes[i].sequence == -1);
    }
}

static void test_zk_election(void) {
    ZKCluster cluster;
    zk_init(&cluster, 5);

    int winner = zk_leader_election(&cluster);
    assert(winner == 0);  /* Lowest sequence wins */
    for (int i = 0; i < 5; i++)
        assert(cluster.nodes[i].leader_id == 0);
}

static void test_zk_crash(void) {
    ZKCluster cluster;
    zk_init(&cluster, 5);
    zk_leader_election(&cluster);

    zk_node_crash(&cluster, 0);
    assert(!cluster.nodes[0].active);
    assert(cluster.nodes[1].leader_id == 1);
}

static void test_message_complexity(void) {
    assert(bully_message_complexity(5) == 25);
    assert(ring_message_complexity(5) == 10);
    assert(zk_message_complexity(5) == 10);
}

static void test_state_name(void) {
    assert(bully_state_name(BULLY_IDLE) != NULL);
    assert(bully_state_name(BULLY_ELECTION) != NULL);
    assert(bully_state_name(BULLY_LEADER) != NULL);
}

int main(void) {
    printf("Running Leader Election tests...\n");
    test_bully_init();
    test_bully_election();
    test_bully_crash_recover();
    test_ring_init();
    test_ring_election();
    test_ring_crash();
    test_ring_pass_token();
    test_zk_init();
    test_zk_election();
    test_zk_crash();
    test_message_complexity();
    test_state_name();
    printf("All Leader Election tests passed!\n");
    return 0;
}
