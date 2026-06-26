#include "raft.h"
#include <assert.h>
#include <stdio.h>

static void test_init_cluster(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);
    for (int i = 0; i < 3; i++) {
        assert(nodes[i].id == i);
        assert(nodes[i].state == RAFT_FOLLOWER);
        assert(nodes[i].current_term == 0);
        assert(nodes[i].voted_for == -1);
        assert(nodes[i].log_count == 0);
        assert(nodes[i].active == true);
        assert(nodes[i].cluster_size == 3);
    }
}

static void test_quorum_size(void) {
    assert(raft_quorum_size(1) == 1);
    assert(raft_quorum_size(2) == 2);
    assert(raft_quorum_size(3) == 2);
    assert(raft_quorum_size(4) == 3);
    assert(raft_quorum_size(5) == 3);
    assert(raft_quorum_size(6) == 4);
    assert(raft_quorum_size(7) == 4);
}

static void test_state_transitions(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);

    /* Follower -> Candidate */
    raft_become_candidate(&nodes[0]);
    assert(nodes[0].state == RAFT_CANDIDATE);
    assert(nodes[0].current_term == 1);
    assert(nodes[0].voted_for == 0);
    assert(nodes[0].votes_received == 1);

    /* Candidate -> Leader via quorum */
    nodes[1].current_term = nodes[0].current_term;
    nodes[0].votes_received = 2;
    assert(nodes[0].votes_received >= raft_quorum_size(3));

    /* Candidate -> Follower (higher term) */
    raft_become_follower(&nodes[0], 10);
    assert(nodes[0].state == RAFT_FOLLOWER);
    assert(nodes[0].current_term == 10);
    assert(nodes[0].voted_for == -1);
}

static void test_request_vote(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);

    /* Node 0 becomes candidate */
    raft_become_candidate(&nodes[0]);

    /* Node 0 requests vote from Node 1 */
    RequestVoteRPC rpc;
    rpc.term           = nodes[0].current_term;
    rpc.candidate_id   = 0;
    rpc.last_log_index = 0;
    rpc.last_log_term  = 0;

    bool granted = false;
    bool ok = raft_handle_request_vote(&nodes[1], &rpc, &granted);
    assert(ok);
    assert(granted);
    assert(nodes[1].voted_for == 0);

    /* Cannot vote for different candidate in same term */
    RequestVoteRPC rpc2;
    rpc2.term           = nodes[0].current_term;
    rpc2.candidate_id   = 2;
    rpc2.last_log_index = 0;
    rpc2.last_log_term  = 0;
    bool granted2 = false;
    raft_handle_request_vote(&nodes[1], &rpc2, &granted2);
    assert(!granted2);  /* already voted for 0 */
}

static void test_append_entries(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);

    /* Set up leader at node 0 */
    raft_become_candidate(&nodes[0]);
    nodes[0].votes_received = 3;
    raft_become_leader(&nodes[0]);

    /* Append an entry */
    AppendEntriesRPC rpc;
    rpc.term          = nodes[0].current_term;
    rpc.leader_id     = 0;
    rpc.prev_log_index = 0;
    rpc.prev_log_term  = 0;
    rpc.leader_commit  = 0;
    LogEntry e;
    e.term    = nodes[0].current_term;
    e.command = 42;
    rpc.entries[0]    = e;
    rpc.entries_count  = 1;

    AppendEntriesRPC reply;
    bool success = raft_handle_append_entries(&nodes[1], &rpc, &reply);
    assert(success);
    assert(nodes[1].log_count == 1);
    assert(nodes[1].log[0].command == 42);

    /* Lower term -> reject */
    rpc.term = 0;
    success = raft_handle_append_entries(&nodes[1], &rpc, &reply);
    assert(!success);
}

static void test_submit_and_query(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);

    /* Elect leader */
    raft_become_candidate(&nodes[0]);
    nodes[0].votes_received = 2;
    raft_become_leader(&nodes[0]);

    bool ok = raft_submit_command(&nodes[0], nodes, 99);
    assert(ok);
    assert(nodes[0].log_count == 1);
    assert(nodes[0].log[0].command == 99);

    int leader = raft_find_leader(nodes, 3);
    assert(leader == 0);

    assert(raft_follower_count(nodes, 3) == 2);
    assert(raft_candidate_count(nodes, 3) == 0);
    assert(raft_quorum_active(nodes, 3) == true);
}

static void test_isolation_and_reconnect(void) {
    RaftNode nodes[3];
    raft_init_cluster(nodes, 3);

    raft_become_candidate(&nodes[0]);
    nodes[0].votes_received = 2;
    raft_become_leader(&nodes[0]);

    assert(raft_find_leader(nodes, 3) == 0);

    raft_isolate_node(&nodes[0]);
    assert(!nodes[0].active);
    assert(raft_quorum_active(nodes, 3) == true); /* 2 of 3 still active */

    raft_reconnect_node(nodes, 3, 0);
    assert(nodes[0].active);
    assert(nodes[0].state == RAFT_FOLLOWER);
}

static void test_random_timeout_range(void) {
    for (int i = 0; i < 100; i++) {
        uint64_t t = raft_random_timeout();
        assert(t >= RAFT_ELECTION_TIMEOUT_MIN_MS);
        assert(t <= RAFT_ELECTION_TIMEOUT_MAX_MS);
    }
}

static void test_state_name(void) {
    assert(raft_quorum_active(NULL, 0) == false); /* edge */
}

int main(void) {
    printf("Running Raft tests...\n");
    test_init_cluster();
    test_quorum_size();
    test_state_transitions();
    test_request_vote();
    test_append_entries();
    test_submit_and_query();
    test_isolation_and_reconnect();
    test_random_timeout_range();
    test_state_name();
    printf("All Raft tests passed!\n");
    return 0;
}
