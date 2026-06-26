#include "paxos.h"
#include <assert.h>
#include <stdio.h>

static void test_init_cluster(void) {
    PaxosCluster cluster;
    paxos_init_cluster(&cluster, 5);
    assert(cluster.node_count == 5);
    assert(cluster.quorum == 3);
    for (int i = 0; i < 5; i++) {
        assert(cluster.proposers[i].proposer_id == i);
        assert(cluster.acceptors[i].acceptor_id == i);
        assert(cluster.acceptors[i].has_accepted == false);
        assert(cluster.learners[i].learner_id == i);
        assert(cluster.learners[i].has_learned == false);
    }
}

static void test_promise_rejects_lower_num(void) {
    PaxosAcceptor acceptor;
    acceptor.promised_num = 0;
    acceptor.accepted_num = 0;
    acceptor.has_accepted = false;

    uint64_t prev_num = 0;
    int      prev_val = 0;
    bool     has_prev = false;

    /* First promise with num=5 should succeed */
    bool ok = paxos_promise(&acceptor, 5, &prev_num, &prev_val, &has_prev);
    assert(ok);
    assert(acceptor.promised_num == 5);

    /* Promise with num=3 (<5) should fail */
    ok = paxos_promise(&acceptor, 3, &prev_num, &prev_val, &has_prev);
    assert(!ok);
    assert(acceptor.promised_num == 5); /* Unchanged */

    /* Promise with num=7 (>5) should succeed */
    ok = paxos_promise(&acceptor, 7, &prev_num, &prev_val, &has_prev);
    assert(ok);
    assert(acceptor.promised_num == 7);
}

static void test_accept_rejects_lower_num(void) {
    PaxosAcceptor acceptor;
    acceptor.promised_num   = 10;
    acceptor.accepted_num   = 0;
    acceptor.has_accepted   = false;

    bool ok = paxos_accepted(&acceptor, 5, 42);
    assert(!ok);

    ok = paxos_accepted(&acceptor, 15, 99);
    assert(ok);
    assert(acceptor.accepted_num == 15);
    assert(acceptor.accepted_value == 99);
    assert(acceptor.has_accepted == true);
}

static void test_prepare_quorum(void) {
    PaxosCluster cluster;
    paxos_init_cluster(&cluster, 5);

    PaxosProposer *prop = &cluster.proposers[0];
    prop->proposal_num = 100;
    prop->value = 77;

    int promise_count = 0;
    uint64_t highest_num = 0;
    int highest_val = 0;
    bool prepared = paxos_prepare(prop, cluster.acceptors, 5,
                                   &promise_count, &highest_num,
                                   &highest_val);
    assert(prepared);
    assert(promise_count >= 3);
}

static void test_full_paxos_instance(void) {
    PaxosCluster cluster;
    paxos_init_cluster(&cluster, 5);

    bool ok = paxos_run_instance(&cluster, 0, 123);
    assert(ok);

    /* All learners should have learned the value */
    for (int i = 0; i < 5; i++) {
        assert(cluster.learners[i].has_learned);
        assert(cluster.learners[i].learned_value == 123);
    }
}

static void test_multi_paxos(void) {
    PaxosCluster cluster;
    paxos_init_cluster(&cluster, 5);

    multi_paxos_become_leader(&cluster, 0);

    for (int v = 1; v <= 5; v++) {
        bool ok = multi_paxos_replicate(&cluster, 0, v * 10);
        assert(ok);
    }

    /* Replication from non-leader should fail */
    bool ok = multi_paxos_replicate(&cluster, 1, 999);
    assert(!ok);
}

static void test_phase_name(void) {
    assert(paxos_phase_name(1) != NULL);
    assert(paxos_phase_name(99) != NULL);
}

int main(void) {
    printf("Running Paxos tests...\n");
    test_init_cluster();
    test_promise_rejects_lower_num();
    test_accept_rejects_lower_num();
    test_prepare_quorum();
    test_full_paxos_instance();
    test_multi_paxos();
    test_phase_name();
    printf("All Paxos tests passed!\n");
    return 0;
}
