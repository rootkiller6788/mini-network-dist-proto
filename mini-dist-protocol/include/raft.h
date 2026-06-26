#ifndef RAFT_H
#define RAFT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define RAFT_MAX_NODES      8
#define RAFT_MAX_LOG_ENTRIES 1024
#define RAFT_ELECTION_TIMEOUT_MIN_MS 150
#define RAFT_ELECTION_TIMEOUT_MAX_MS 300
#define RAFT_HEARTBEAT_INTERVAL_MS   50
#define RAFT_RPC_TIMEOUT_MS          100

typedef enum {
    RAFT_FOLLOWER,
    RAFT_CANDIDATE,
    RAFT_LEADER
} RaftState;

typedef struct {
    uint64_t term;
    int      command;
} LogEntry;

typedef struct {
    uint64_t term;
    int      leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    LogEntry entries[128];
    int      entries_count;
    uint64_t leader_commit;
} AppendEntriesRPC;

typedef struct {
    uint64_t term;
    int      candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
} RequestVoteRPC;

typedef struct {
    int         id;
    RaftState   state;
    uint64_t    current_term;
    int         voted_for;
    LogEntry    log[RAFT_MAX_LOG_ENTRIES];
    int         log_count;
    uint64_t    commit_index;
    uint64_t    last_applied;
    uint64_t    next_index[RAFT_MAX_NODES];
    uint64_t    match_index[RAFT_MAX_NODES];
    uint64_t    election_timeout_ms;
    uint64_t    election_elapsed_ms;
    uint64_t    heartbeat_elapsed_ms;
    int         votes_received;
    int         cluster_ids[RAFT_MAX_NODES];
    int         cluster_size;
    bool        active;
} RaftNode;

void raft_init_cluster(RaftNode *nodes, int n);
void raft_become_follower(RaftNode *node, uint64_t term);
void raft_become_candidate(RaftNode *node);
void raft_become_leader(RaftNode *node);
bool raft_handle_append_entries(RaftNode *node, const AppendEntriesRPC *rpc,
                                AppendEntriesRPC *reply);
bool raft_handle_request_vote(RaftNode *node, const RequestVoteRPC *rpc,
                               bool *vote_granted);
void raft_tick(RaftNode *nodes, int n, uint64_t delta_ms);
uint64_t raft_random_timeout(void);
int  raft_quorum_size(int cluster_size);
void raft_print_state(const RaftNode *node);
const char *raft_state_name(RaftState state);

#endif
