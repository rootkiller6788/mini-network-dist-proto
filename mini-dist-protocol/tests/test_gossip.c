#include "gossip.h"
#include <assert.h>
#include <stdio.h>

static void test_init_ring(void) {
    GossipNode nodes[4];
    gossip_init_ring(nodes, 4);
    for (int i = 0; i < 4; i++) {
        assert(nodes[i].id == i);
        assert(nodes[i].active == true);
        assert(nodes[i].neighbor_count == 2);
    }
    assert(nodes[0].neighbors[0] == 1);
    assert(nodes[0].neighbors[1] == 3);
}

static void test_init_full(void) {
    GossipNode nodes[3];
    gossip_init_full(nodes, 3);
    for (int i = 0; i < 3; i++)
        assert(nodes[i].neighbor_count == 2);
}

static void test_set_and_get(void) {
    GossipNode nodes[1];
    gossip_init_ring(nodes, 1);

    gossip_set_data(&nodes[0], 5, 100);
    int val;
    uint64_t ver;
    assert(gossip_get_data(&nodes[0], 5, &val, &ver));
    assert(val == 100);

    /* Overwrite */
    gossip_set_data(&nodes[0], 5, 200);
    assert(gossip_get_data(&nodes[0], 5, &val, &ver));
    assert(val == 200);

    /* Missing key */
    assert(!gossip_get_data(&nodes[0], 99, &val, &ver));
}

static void test_push_message(void) {
    GossipNode nodes[1];
    gossip_init_ring(nodes, 1);
    gossip_set_data(&nodes[0], 1, 10);
    gossip_set_data(&nodes[0], 2, 20);

    GossipMessage msg = gossip_create_push(&nodes[0]);
    assert(msg.type == GOSSIP_PUSH);
    assert(msg.entry_count == 2);
    assert(msg.sender_id == 0);
}

static void test_spread_and_converge(void) {
    GossipNode nodes[4];
    gossip_init_full(nodes, 4);

    gossip_set_data(&nodes[0], 1, 100);
    gossip_set_data(&nodes[1], 2, 200);

    int rounds = 0;
    bool ok = gossip_converge(nodes, 4, 20, &rounds);
    assert(ok);
    assert(gossip_all_synced(nodes, 4));

    /* Both keys should be on all nodes */
    for (int i = 0; i < 4; i++) {
        int val;
        uint64_t ver;
        assert(gossip_get_data(&nodes[i], 1, &val, &ver));
        assert(val == 100);
        assert(gossip_get_data(&nodes[i], 2, &val, &ver));
        assert(val == 200);
    }
}

static void test_select_peer(void) {
    GossipNode nodes[3];
    gossip_init_full(nodes, 3);
    int peer = gossip_select_peer(&nodes[0]);
    assert(peer >= 0 && peer < 3 && peer != 0);
}

static void test_msg_type_name(void) {
    assert(gossip_msg_type_name(GOSSIP_PUSH) != NULL);
    assert(gossip_msg_type_name(GOSSIP_PULL) != NULL);
    assert(gossip_msg_type_name(GOSSIP_PUSH_PULL) != NULL);
}

static void test_missing_count(void) {
    GossipNode nodes[2];
    gossip_init_ring(nodes, 2);
    gossip_set_data(&nodes[0], 1, 10);
    gossip_set_data(&nodes[0], 2, 20);
    gossip_set_data(&nodes[1], 1, 10);

    int missing = gossip_missing_count(&nodes[0], &nodes[1]);
    assert(missing == 1);
}

int main(void) {
    printf("Running Gossip tests...\n");
    test_init_ring();
    test_init_full();
    test_set_and_get();
    test_push_message();
    test_spread_and_converge();
    test_select_peer();
    test_msg_type_name();
    test_missing_count();
    printf("All Gossip tests passed!\n");
    return 0;
}
