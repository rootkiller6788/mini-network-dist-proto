/* raft.c - Raft Consensus Algorithm. Reference: MIT 6.824, Ongaro 2014. */
#include "raft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t raft_random_election_timeout(void) {
    return 150 + (uint64_t)(rand() % 151);
}
static int raft_last_log_index(const RaftNode *n) {
    return n->log_count - 1;
}
static uint64_t raft_last_log_term(const RaftNode *n) {
    return n->log_count == 0 ? 0 : n->log[n->log_count - 1].term;
}
static void raft_enqueue_msg(RaftCluster *rc, const RaftMessage *msg) {
    int nxt = (rc->msg_tail + 1) % RAFT_MAX_MSG_QUEUE;
    if (nxt == rc->msg_head)
        return;
    rc->message_queue[rc->msg_tail] = *msg;
    rc->msg_tail = nxt;
}
static bool __attribute__((unused)) raft_dequeue_msg(RaftCluster *rc, RaftMessage *out) {
    if (rc->msg_head == rc->msg_tail)
        return false;
    *out = rc->message_queue[rc->msg_head];
    rc->msg_head = (rc->msg_head + 1) % RAFT_MAX_MSG_QUEUE;
    return true;
}
static bool raft_log_ok(const RaftNode *n, int pi, uint64_t pt) {
    if (pi < 0)
        return true;
    if (pi >= n->log_count)
        return false;
    return n->log[pi].term == pt;
}
static void raft_advance_commit(RaftCluster *rc, int lid) {
    RaftNode *l = &rc->nodes[lid];
    int q = raft_quorum_size(rc->node_count);
    int n;
    for (n = l->commit_index + 1; n < l->log_count; n++) {
        if (l->log[n].term != l->current_term)
            continue;
        int r = 1, i;
        for (i = 0; i < rc->node_count; i++) {
            if (i == lid)
                continue;
            if (l->match_index[i] >= n)
                r++;
        }
        if (r >= q)
            l->commit_index = n;
    }
}
void raft_init_cluster(RaftCluster *rc, int n) {
    int i;
    if (n > RAFT_MAX_NODES)
        n = RAFT_MAX_NODES;
    if (n < 1)
        n = 3;
    rc->node_count = n;
    rc->msg_head = 0;
    rc->msg_tail = 0;
    rc->leader_id = -1;
    rc->sim_time_ms = 0;
    for (i = 0; i < n; i++) {
        RaftNode *nd = &rc->nodes[i];
        nd->id = i;
        nd->role = RAFT_FOLLOWER;
        nd->current_term = 0;
        nd->voted_for = -1;
        nd->log_count = 0;
        nd->commit_index = -1;
        nd->last_applied = -1;
        nd->votes_received = 0;
        nd->election_deadline_ms = raft_random_election_timeout();
        nd->last_heartbeat_ms = 0;
        int j;
        for (j = 0; j < RAFT_MAX_NODES; j++) {
            nd->next_index[j] = 0;
            nd->match_index[j] = -1;
        }
    }
}
void raft_tick(RaftCluster *rc) {
    int i;
    rc->sim_time_ms++;
    for (i = 0; i < rc->node_count; i++) {
        RaftNode *nd = &rc->nodes[i];
        if (nd->role == RAFT_LEADER)
            continue;
        if (rc->sim_time_ms >= nd->election_deadline_ms) {
            raft_start_election(rc, i);
            nd->election_deadline_ms = rc->sim_time_ms + raft_random_election_timeout();
        }
    }
}
bool raft_is_running(const RaftCluster *rc) {
    return rc->leader_id >= 0;
}
void raft_start_election(RaftCluster *rc, int nid) {
    RaftNode *nd = &rc->nodes[nid];
    nd->role = RAFT_CANDIDATE;
    nd->current_term++;
    nd->voted_for = nid;
    nd->votes_received = 1;
    nd->election_deadline_ms = rc->sim_time_ms + raft_random_election_timeout();
    int i;
    for (i = 0; i < rc->node_count; i++) {
        if (i == nid)
            continue;
        RaftMessage rv = raft_build_request_vote(rc, nid, i);
        raft_enqueue_msg(rc, &rv);
    }
    if (nd->votes_received >= raft_quorum_size(rc->node_count))
        raft_become_leader(rc, nid);
}
void raft_become_follower(RaftCluster *rc, int nid, uint64_t term) {
    RaftNode *nd = &rc->nodes[nid];
    nd->role = RAFT_FOLLOWER;
    nd->current_term = term;
    nd->voted_for = -1;
    nd->election_deadline_ms = rc->sim_time_ms + raft_random_election_timeout();
}
void raft_become_leader(RaftCluster *rc, int nid) {
    RaftNode *nd = &rc->nodes[nid];
    nd->role = RAFT_LEADER;
    rc->leader_id = nid;
    int i;
    for (i = 0; i < RAFT_MAX_NODES; i++) {
        nd->next_index[i] = nd->log_count;
        nd->match_index[i] = -1;
    }
    raft_leader_send_heartbeats(rc);
}
RaftMessage raft_build_request_vote(const RaftCluster *rc, int from, int to) {
    const RaftNode *c = &rc->nodes[from];
    RaftMessage m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_REQUEST_VOTE;
    m.from = from;
    m.to = to;
    m.term = c->current_term;
    m.last_log_index = raft_last_log_index(c);
    m.last_log_term = raft_last_log_term(c);
    return m;
}
RaftMessage raft_build_request_vote_reply(const RaftCluster *rc, int from, int to, uint64_t term,
                                          int lli, uint64_t llt, bool g) {
    (void)rc;
    (void)lli;
    (void)llt;
    RaftMessage m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_REQUEST_VOTE_REPLY;
    m.from = from;
    m.to = to;
    m.term = term;
    m.vote_granted = g;
    return m;
}
RaftMessage raft_build_append_entries(const RaftCluster *rc, int from, int to) {
    const RaftNode *l = &rc->nodes[from];
    RaftMessage m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_APPEND_ENTRIES;
    m.from = from;
    m.to = to;
    m.term = l->current_term;
    m.prev_log_index = l->next_index[to] - 1;
    m.prev_log_term = (m.prev_log_index >= 0 && m.prev_log_index < l->log_count)
                          ? l->log[m.prev_log_index].term
                          : 0;
    m.entry_count = 0;
    m.leader_commit = l->commit_index;
    return m;
}
RaftMessage raft_build_append_entries_reply(const RaftCluster *rc, int from, int to, uint64_t term,
                                            bool s, int mi) {
    (void)rc;
    RaftMessage m;
    memset(&m, 0, sizeof(m));
    m.type = RAFT_MSG_APPEND_ENTRIES_REPLY;
    m.from = from;
    m.to = to;
    m.term = term;
    m.success = s;
    m.match_index_reply = mi;
    return m;
}
void raft_process_message(RaftCluster *rc, const RaftMessage *m) {
    switch (m->type) {
    case RAFT_MSG_REQUEST_VOTE:
        raft_handle_request_vote(rc, m);
        break;
    case RAFT_MSG_REQUEST_VOTE_REPLY:
        raft_handle_request_vote_reply(rc, m);
        break;
    case RAFT_MSG_APPEND_ENTRIES:
        raft_handle_append_entries(rc, m);
        break;
    case RAFT_MSG_APPEND_ENTRIES_REPLY:
        raft_handle_append_entries_reply(rc, m);
        break;
    case RAFT_MSG_CLIENT_COMMAND:
        if (rc->leader_id >= 0)
            raft_leader_append_entry(rc, m->command);
        break;
    default:
        break;
    }
}
void raft_handle_request_vote(RaftCluster *rc, const RaftMessage *m) {
    RaftNode *nd = &rc->nodes[m->to];
    bool grant = false;
    if (m->term < nd->current_term) {
        RaftMessage r =
            raft_build_request_vote_reply(rc, m->to, m->from, nd->current_term,
                                          raft_last_log_index(nd), raft_last_log_term(nd), false);
        raft_enqueue_msg(rc, &r);
        return;
    }
    if (m->term > nd->current_term)
        raft_become_follower(rc, m->to, m->term);
    if (nd->voted_for == -1 || nd->voted_for == m->from) {
        uint64_t mlt = raft_last_log_term(nd);
        int mli = raft_last_log_index(nd);
        if (m->last_log_term > mlt || (m->last_log_term == mlt && m->last_log_index >= mli)) {
            grant = true;
            nd->voted_for = m->from;
            nd->election_deadline_ms = rc->sim_time_ms + raft_random_election_timeout();
        }
    }
    RaftMessage r =
        raft_build_request_vote_reply(rc, m->to, m->from, nd->current_term, raft_last_log_index(nd),
                                      raft_last_log_term(nd), grant);
    raft_enqueue_msg(rc, &r);
}
void raft_handle_request_vote_reply(RaftCluster *rc, const RaftMessage *m) {
    RaftNode *nd = &rc->nodes[m->to];
    if (m->term > nd->current_term) {
        raft_become_follower(rc, m->to, m->term);
        return;
    }
    if (m->term < nd->current_term)
        return;
    if (nd->role == RAFT_CANDIDATE && m->vote_granted) {
        nd->votes_received++;
        if (nd->votes_received >= raft_quorum_size(rc->node_count))
            raft_become_leader(rc, m->to);
    }
}
void raft_handle_append_entries(RaftCluster *rc, const RaftMessage *m) {
    RaftNode *nd = &rc->nodes[m->to];
    if (m->term < nd->current_term) {
        RaftMessage r =
            raft_build_append_entries_reply(rc, m->to, m->from, nd->current_term, false, -1);
        raft_enqueue_msg(rc, &r);
        return;
    }
    nd->election_deadline_ms = rc->sim_time_ms + raft_random_election_timeout();
    nd->last_heartbeat_ms = rc->sim_time_ms;
    if (m->term > nd->current_term)
        raft_become_follower(rc, m->to, m->term);
    if (nd->role == RAFT_CANDIDATE)
        nd->role = RAFT_FOLLOWER;
    if (rc->leader_id != m->from)
        rc->leader_id = m->from;
    if (!raft_log_ok(nd, m->prev_log_index, m->prev_log_term)) {
        RaftMessage r =
            raft_build_append_entries_reply(rc, m->to, m->from, nd->current_term, false, -1);
        raft_enqueue_msg(rc, &r);
        return;
    }
    int mi = m->prev_log_index, e;
    for (e = 0; e < m->entry_count; e++) {
        int idx = m->prev_log_index + 1 + e;
        if (idx < nd->log_count && nd->log[idx].term != m->entries[e].term)
            nd->log_count = idx;
        if (idx >= nd->log_count && nd->log_count < RAFT_MAX_LOG) {
            nd->log[nd->log_count] = m->entries[e];
            nd->log_count++;
        }
        mi = idx;
    }
    if (m->leader_commit > nd->commit_index) {
        int nc = m->leader_commit;
        if (nc >= nd->log_count)
            nc = nd->log_count - 1;
        nd->commit_index = nc;
    }
    RaftMessage r = raft_build_append_entries_reply(rc, m->to, m->from, nd->current_term, true, mi);
    raft_enqueue_msg(rc, &r);
}
void raft_handle_append_entries_reply(RaftCluster *rc, const RaftMessage *m) {
    RaftNode *l = &rc->nodes[m->to];
    if (m->term > l->current_term) {
        raft_become_follower(rc, m->to, m->term);
        rc->leader_id = -1;
        return;
    }
    if (l->role != RAFT_LEADER)
        return;
    if (m->success) {
        l->match_index[m->from] = m->match_index_reply;
        l->next_index[m->from] = m->match_index_reply + 1;
        raft_advance_commit(rc, m->to);
    } else if (l->next_index[m->from] > 0)
        l->next_index[m->from]--;
}
void raft_leader_send_heartbeats(RaftCluster *rc) {
    int lid = rc->leader_id;
    if (lid < 0)
        return;
    int i;
    for (i = 0; i < rc->node_count; i++) {
        if (i == lid)
            continue;
        RaftMessage ae = raft_build_append_entries(rc, lid, i);
        raft_enqueue_msg(rc, &ae);
    }
}
void raft_leader_append_entry(RaftCluster *rc, int cmd) {
    int lid = rc->leader_id;
    if (lid < 0)
        return;
    RaftNode *l = &rc->nodes[lid];
    if (l->log_count >= RAFT_MAX_LOG)
        return;
    l->log[l->log_count].term = l->current_term;
    l->log[l->log_count].command = cmd;
    l->log_count++;
    l->match_index[lid] = l->log_count - 1;
    l->next_index[lid] = l->log_count;
}
bool raft_leader_commit_entries(RaftCluster *rc) {
    if (rc->leader_id < 0)
        return false;
    raft_advance_commit(rc, rc->leader_id);
    return true;
}
bool raft_client_submit(RaftCluster *rc, int cmd, int *oli) {
    if (rc->leader_id < 0)
        return false;
    raft_leader_append_entry(rc, cmd);
    if (oli)
        *oli = rc->nodes[rc->leader_id].log_count - 1;
    return true;
}
int raft_get_committed(const RaftCluster *rc, int nid) {
    if (nid < 0 || nid >= rc->node_count)
        return -1;
    const RaftNode *nd = &rc->nodes[nid];
    if (nd->commit_index < 0 || nd->commit_index >= nd->log_count)
        return -1;
    return nd->log[nd->commit_index].command;
}
bool raft_is_leader(const RaftCluster *rc, int nid) {
    if (nid < 0 || nid >= rc->node_count)
        return false;
    return rc->nodes[nid].role == RAFT_LEADER;
}
int raft_get_leader(const RaftCluster *rc) {
    return rc->leader_id;
}
int raft_quorum_size(int n) {
    return (n / 2) + 1;
}
const char *raft_role_str(RaftRole r) {
    switch (r) {
    case RAFT_FOLLOWER:
        return "Follower";
    case RAFT_CANDIDATE:
        return "Candidate";
    case RAFT_LEADER:
        return "Leader";
    default:
        return "Unknown";
    }
}
bool raft_safety_check(const RaftCluster *rc) {
    int i, cc[RAFT_MAX_NODES];
    for (i = 0; i < rc->node_count; i++) {
        const RaftNode *nd = &rc->nodes[i];
        cc[i] = (nd->commit_index >= 0 && nd->commit_index < nd->log_count)
                    ? nd->log[nd->commit_index].command
                    : -1;
    }
    for (i = 1; i < rc->node_count; i++) {
        if (cc[i] >= 0 && cc[0] >= 0 && cc[i] != cc[0])
            return false;
    }
    return true;
}
void raft_print_state(const RaftCluster *rc) {
    int i;
    printf("=== Raft Cluster (n=%d, time=%llu ms) ===\n", rc->node_count,
           (unsigned long long)rc->sim_time_ms);
    printf("Leader: %d\n", rc->leader_id);
    for (i = 0; i < rc->node_count; i++) {
        const RaftNode *nd = &rc->nodes[i];
        printf("  Node %d: role=%-9s term=%llu log_len=%d commit=%d voted_for=%d\n", i,
               raft_role_str(nd->role), (unsigned long long)nd->current_term, nd->log_count,
               nd->commit_index, nd->voted_for);
    }
}
void raft_print_log(const RaftNode *nd) {
    int i;
    printf("Log for Node %d (len=%d):\n", nd->id, nd->log_count);
    for (i = 0; i < nd->log_count; i++)
        printf("  [%d] term=%llu cmd=%d\n", i, (unsigned long long)nd->log[i].term,
               nd->log[i].command);
}
int raft_agreement_percent(const RaftCluster *rc) {
    int i, ag = 0, rcmd = -1;
    for (i = 0; i < rc->node_count; i++) {
        int cmd = raft_get_committed(rc, i);
        if (cmd < 0)
            continue;
        if (rcmd < 0)
            rcmd = cmd;
        if (cmd == rcmd)
            ag++;
    }
    return (rc->node_count > 0) ? (ag * 100) / rc->node_count : 0;
}
