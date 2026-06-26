#include "raft.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static uint64_t g_seed;

static uint64_t raft_rand(void) {
    g_seed = g_seed * 1103515245 + 12345;
    return g_seed / 65536 % 32768;
}

uint64_t raft_random_timeout(void) {
    return RAFT_ELECTION_TIMEOUT_MIN_MS +
           (raft_rand() % (RAFT_ELECTION_TIMEOUT_MAX_MS -
            RAFT_ELECTION_TIMEOUT_MIN_MS + 1));
}

int raft_quorum_size(int cluster_size) {
    return (cluster_size / 2) + 1;
}

const char *raft_state_name(RaftState state) {
    switch (state) {
        case RAFT_FOLLOWER:   return "FOLLOWER";
        case RAFT_CANDIDATE:  return "CANDIDATE";
        case RAFT_LEADER:     return "LEADER";
        default:              return "UNKNOWN";
    }
}

void raft_init_cluster(RaftNode *nodes, int n) {
    g_seed = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)nodes;
    for (int i = 0; i < n; i++) {
        nodes[i].id                  = i;
        nodes[i].state               = RAFT_FOLLOWER;
        nodes[i].current_term        = 0;
        nodes[i].voted_for           = -1;
        nodes[i].log_count           = 0;
        nodes[i].commit_index        = 0;
        nodes[i].last_applied        = 0;
        nodes[i].election_timeout_ms = raft_random_timeout();
        nodes[i].election_elapsed_ms = 0;
        nodes[i].heartbeat_elapsed_ms = 0;
        nodes[i].votes_received      = 0;
        nodes[i].cluster_size        = n;
        nodes[i].active              = true;

        for (int j = 0; j < n; j++) {
            nodes[i].cluster_ids[j] = j;
            nodes[i].next_index[j]  = 1;
            nodes[i].match_index[j] = 0;
        }
    }
}

void raft_become_follower(RaftNode *node, uint64_t term) {
    if (term > node->current_term) {
        node->current_term = term;
    }
    node->state               = RAFT_FOLLOWER;
    node->voted_for           = -1;
    node->votes_received      = 0;
    node->election_timeout_ms = raft_random_timeout();
    node->election_elapsed_ms = 0;
}

void raft_become_candidate(RaftNode *node) {
    node->state               = RAFT_CANDIDATE;
    node->current_term++;
    node->voted_for           = node->id;
    node->votes_received      = 1;
    node->election_timeout_ms = raft_random_timeout();
    node->election_elapsed_ms = 0;
}

void raft_become_leader(RaftNode *node) {
    node->state          = RAFT_LEADER;
    node->voted_for      = -1;
    node->votes_received = 0;
    for (int i = 0; i < node->cluster_size; i++) {
        node->next_index[i]  = (uint64_t)(node->log_count + 1);
        node->match_index[i] = 0;
    }
    node->heartbeat_elapsed_ms = 0;
}

static uint64_t raft_last_log_index(const RaftNode *node) {
    if (node->log_count == 0) return 0;
    return (uint64_t)node->log_count;
}

static uint64_t raft_last_log_term(const RaftNode *node) {
    if (node->log_count == 0) return 0;
    return node->log[node->log_count - 1].term;
}

static bool raft_log_ok(const RaftNode *node, uint64_t prev_log_index,
                         uint64_t prev_log_term) {
    if (prev_log_index == 0) return true;
    if ((int)prev_log_index > node->log_count) return false;
    uint64_t existing_term = node->log[prev_log_index - 1].term;
    return existing_term == prev_log_term;
}

bool raft_handle_append_entries(RaftNode *node, const AppendEntriesRPC *rpc,
                                 AppendEntriesRPC *reply) {
    reply->term         = node->current_term;
    reply->entries_count = 0;
    reply->leader_id    = node->id;

    if (rpc->term < node->current_term) {
        return false;
    }

    if (rpc->term > node->current_term) {
        raft_become_follower(node, rpc->term);
    }

    node->election_elapsed_ms = 0;
    node->heartbeat_elapsed_ms = 0;

    if (!raft_log_ok(node, rpc->prev_log_index, rpc->prev_log_term)) {
        return false;
    }

    int conflict_index = (int)rpc->prev_log_index + 1;

    for (int i = 0; i < rpc->entries_count; i++) {
        int pos = conflict_index + i - 1;
        if (pos >= RAFT_MAX_LOG_ENTRIES) break;

        if (pos < node->log_count) {
            if (node->log[pos].term != rpc->entries[i].term) {
                node->log_count = pos;
                node->log[pos] = rpc->entries[i];
                node->log_count = pos + 1;
            }
        } else {
            node->log[node->log_count] = rpc->entries[i];
            node->log_count++;
        }
    }

    if (rpc->leader_commit > node->commit_index) {
        uint64_t last_new = (uint64_t)(conflict_index + rpc->entries_count - 1);
        uint64_t new_commit = rpc->leader_commit;
        if (new_commit > last_new) new_commit = last_new;
        if (new_commit > node->commit_index) {
            node->commit_index = new_commit;
        }
    }

    return true;
}

bool raft_handle_request_vote(RaftNode *node, const RequestVoteRPC *rpc,
                               bool *vote_granted) {
    *vote_granted = false;

    if (rpc->term < node->current_term) {
        return false;
    }

    if (rpc->term > node->current_term) {
        raft_become_follower(node, rpc->term);
    }

    bool can_vote = (node->voted_for == -1 || node->voted_for == rpc->candidate_id);

    if (!can_vote) {
        return false;
    }

    uint64_t my_last_term = raft_last_log_term(node);
    uint64_t my_last_index = raft_last_log_index(node);

    bool log_ok =
        (rpc->last_log_term > my_last_term) ||
        (rpc->last_log_term == my_last_term &&
         rpc->last_log_index >= my_last_index);

    if (log_ok) {
        node->voted_for = rpc->candidate_id;
        node->election_elapsed_ms = 0;
        *vote_granted = true;
    }

    return true;
}

static void raft_send_request_vote(RaftNode *node, RaftNode *nodes) {
    RequestVoteRPC rpc;
    rpc.term           = node->current_term;
    rpc.candidate_id   = node->id;
    rpc.last_log_index = raft_last_log_index(node);
    rpc.last_log_term  = raft_last_log_term(node);

    for (int i = 0; i < node->cluster_size; i++) {
        if (i == node->id || !nodes[i].active) continue;

        bool granted = false;
        raft_handle_request_vote(&nodes[i], &rpc, &granted);

        if (granted && nodes[i].current_term >= node->current_term) {
            node->votes_received++;
        }
    }
}

static void raft_send_heartbeat(RaftNode *node, RaftNode *nodes) {
    AppendEntriesRPC rpc;
    rpc.term          = node->current_term;
    rpc.leader_id     = node->id;
    rpc.entries_count = 0;
    rpc.leader_commit = node->commit_index;

    for (int i = 0; i < node->cluster_size; i++) {
        if (i == node->id || !nodes[i].active) continue;

        rpc.prev_log_index = node->next_index[i] - 1;
        rpc.prev_log_term  = 0;
        if (rpc.prev_log_index > 0 && (int)rpc.prev_log_index <= node->log_count) {
            rpc.prev_log_term = node->log[rpc.prev_log_index - 1].term;
        }

        AppendEntriesRPC reply;
        raft_handle_append_entries(&nodes[i], &rpc, &reply);
    }
}

static void raft_send_append_entries(RaftNode *node, RaftNode *nodes) {
    for (int i = 0; i < node->cluster_size; i++) {
        if (i == node->id || !nodes[i].active) continue;

        AppendEntriesRPC rpc;
        rpc.term          = node->current_term;
        rpc.leader_id     = node->id;
        rpc.prev_log_index = node->next_index[i] - 1;
        rpc.prev_log_term  = 0;
        if (rpc.prev_log_index > 0 && (int)rpc.prev_log_index <= node->log_count) {
            rpc.prev_log_term = node->log[rpc.prev_log_index - 1].term;
        }
        rpc.leader_commit = node->commit_index;

        int start = (int)node->next_index[i] - 1;
        rpc.entries_count = 0;
        for (int j = start; j < node->log_count && rpc.entries_count < 128; j++) {
            rpc.entries[rpc.entries_count] = node->log[j];
            rpc.entries_count++;
        }

        AppendEntriesRPC reply;
        bool success = raft_handle_append_entries(&nodes[i], &rpc, &reply);

        if (success) {
            node->next_index[i] = node->next_index[i] + rpc.entries_count;
            node->match_index[i] = node->next_index[i] - 1;
        } else {
            if (node->next_index[i] > 1) {
                node->next_index[i]--;
            }
        }
    }

    for (uint64_t n = raft_last_log_index(node); n > node->commit_index; n--) {
        if (node->log[n - 1].term != node->current_term) continue;
        int matched = 1;
        for (int i = 0; i < node->cluster_size; i++) {
            if (i == node->id) continue;
            if (!nodes[i].active) continue;
            if (node->match_index[i] >= n) matched++;
        }
        if (matched >= raft_quorum_size(node->cluster_size)) {
            node->commit_index = n;
            break;
        }
    }
}

void raft_tick(RaftNode *nodes, int n, uint64_t delta_ms) {
    for (int i = 0; i < n; i++) {
        if (!nodes[i].active) continue;

        switch (nodes[i].state) {
            case RAFT_FOLLOWER:
                nodes[i].election_elapsed_ms += delta_ms;
                if (nodes[i].election_elapsed_ms >= nodes[i].election_timeout_ms) {
                    raft_become_candidate(&nodes[i]);
                }
                break;

            case RAFT_CANDIDATE:
                nodes[i].election_elapsed_ms += delta_ms;
                if (nodes[i].election_elapsed_ms >= nodes[i].election_timeout_ms) {
                    raft_become_candidate(&nodes[i]);
                    break;
                }
                raft_send_request_vote(&nodes[i], nodes);
                if (nodes[i].votes_received >= raft_quorum_size(n)) {
                    raft_become_leader(&nodes[i]);
                }
                break;

            case RAFT_LEADER:
                nodes[i].heartbeat_elapsed_ms += delta_ms;
                if (nodes[i].heartbeat_elapsed_ms >= RAFT_HEARTBEAT_INTERVAL_MS) {
                    raft_send_heartbeat(&nodes[i], nodes);
                    raft_send_append_entries(&nodes[i], nodes);
                    nodes[i].heartbeat_elapsed_ms = 0;
                }
                break;
        }
    }
}

void raft_print_state(const RaftNode *node) {
    printf("  Node %d [%s] term=%llu log_count=%d commit=%llu votes=%d active=%s\n",
           node->id,
           raft_state_name(node->state),
           (unsigned long long)node->current_term,
           node->log_count,
           (unsigned long long)node->commit_index,
           node->votes_received,
           node->active ? "yes" : "no");
}

bool raft_submit_command(RaftNode *leader, RaftNode *nodes, int command_value) {
    if (leader->state != RAFT_LEADER) return false;

    LogEntry entry;
    entry.term    = leader->current_term;
    entry.command = command_value;

    if (leader->log_count >= RAFT_MAX_LOG_ENTRIES) return false;

    leader->log[leader->log_count] = entry;
    leader->log_count++;

    raft_send_append_entries(leader, nodes);

    return true;
}

void raft_isolate_node(RaftNode *node) {
    node->active = false;
}

void raft_reconnect_node(RaftNode *nodes, int n, int node_id) {
    if (node_id < 0 || node_id >= n) return;
    nodes[node_id].active = true;
    raft_become_follower(&nodes[node_id], nodes[node_id].current_term);
}

int raft_find_leader(const RaftNode *nodes, int n) {
    for (int i = 0; i < n; i++) {
        if (nodes[i].active && nodes[i].state == RAFT_LEADER) {
            return i;
        }
    }
    return -1;
}

int raft_follower_count(const RaftNode *nodes, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (nodes[i].active && nodes[i].state == RAFT_FOLLOWER) count++;
    }
    return count;
}

int raft_candidate_count(const RaftNode *nodes, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (nodes[i].active && nodes[i].state == RAFT_CANDIDATE) count++;
    }
    return count;
}

bool raft_quorum_active(const RaftNode *nodes, int n) {
    int active = 0;
    for (int i = 0; i < n; i++) {
        if (nodes[i].active) active++;
    }
    return active >= raft_quorum_size(n);
}
