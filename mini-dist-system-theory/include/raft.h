#ifndef RAFT_H
#define RAFT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RAFT_MAX_NODES 7
#define RAFT_MAX_LOG 256
#define RAFT_MAX_MSG_QUEUE 128

typedef enum {
    RAFT_FOLLOWER,
    RAFT_CANDIDATE,
    RAFT_LEADER
} RaftRole;

typedef struct {
    uint64_t term;
    int command;
} RaftLogEntry;

typedef struct {
    int id;
    RaftRole role;
    uint64_t current_term;
    int voted_for;
    RaftLogEntry log[RAFT_MAX_LOG];
    int log_count;
    int commit_index;
    int last_applied;
    int next_index[RAFT_MAX_NODES];
    int match_index[RAFT_MAX_NODES];
    int votes_received;
    uint64_t election_deadline_ms;
    uint64_t last_heartbeat_ms;
} RaftNode;

typedef enum {
    RAFT_MSG_REQUEST_VOTE,
    RAFT_MSG_REQUEST_VOTE_REPLY,
    RAFT_MSG_APPEND_ENTRIES,
    RAFT_MSG_APPEND_ENTRIES_REPLY,
    RAFT_MSG_CLIENT_COMMAND
} RaftMsgType;

typedef struct {
    RaftMsgType type;
    int from;
    int to;
    uint64_t term;
    int last_log_index;
    uint64_t last_log_term;
    bool vote_granted;
    int prev_log_index;
    uint64_t prev_log_term;
    RaftLogEntry entries[8];
    int entry_count;
    int leader_commit;
    bool success;
    int match_index_reply;
    int command;
} RaftMessage;

typedef struct {
    RaftNode nodes[RAFT_MAX_NODES];
    int node_count;
    RaftMessage message_queue[RAFT_MAX_MSG_QUEUE];
    int msg_head;
    int msg_tail;
    int leader_id;
    uint64_t sim_time_ms;
} RaftCluster;

void raft_init_cluster(RaftCluster *rc, int n);
void raft_tick(RaftCluster *rc);
bool raft_is_running(const RaftCluster *rc);
void raft_start_election(RaftCluster *rc, int node_id);
void raft_become_follower(RaftCluster *rc, int node_id, uint64_t term);
void raft_become_leader(RaftCluster *rc, int node_id);
RaftMessage raft_build_request_vote(const RaftCluster *rc, int from, int to);
RaftMessage raft_build_request_vote_reply(const RaftCluster *rc, int from, int to,
                                           uint64_t term, int last_log_idx,
                                           uint64_t last_log_term, bool granted);
RaftMessage raft_build_append_entries(const RaftCluster *rc, int from, int to);
RaftMessage raft_build_append_entries_reply(const RaftCluster *rc, int from, int to,
                                              uint64_t term, bool success,
                                              int match_idx);
void raft_process_message(RaftCluster *rc, const RaftMessage *msg);
void raft_handle_request_vote(RaftCluster *rc, const RaftMessage *msg);
void raft_handle_request_vote_reply(RaftCluster *rc, const RaftMessage *msg);
void raft_handle_append_entries(RaftCluster *rc, const RaftMessage *msg);
void raft_handle_append_entries_reply(RaftCluster *rc, const RaftMessage *msg);
void raft_leader_send_heartbeats(RaftCluster *rc);
void raft_leader_append_entry(RaftCluster *rc, int command);
bool raft_leader_commit_entries(RaftCluster *rc);
bool raft_client_submit(RaftCluster *rc, int command, int *out_log_index);
int  raft_get_committed(const RaftCluster *rc, int node_id);
bool raft_is_leader(const RaftCluster *rc, int node_id);
int  raft_get_leader(const RaftCluster *rc);
int  raft_quorum_size(int n);
const char *raft_role_str(RaftRole role);
bool raft_safety_check(const RaftCluster *rc);
void raft_print_state(const RaftCluster *rc);
void raft_print_log(const RaftNode *node);
int  raft_agreement_percent(const RaftCluster *rc);

#endif
