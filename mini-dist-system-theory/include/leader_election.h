#ifndef LEADER_ELECTION_H
#define LEADER_ELECTION_H

#include <stdbool.h>
#include <stdint.h>

#define LE_MAX_NODES 16
#define LE_MAX_MSG 64

typedef enum {
    LE_BULLY,
    LE_RING,
    LE_RAFT_STYLE
} LEMode;

typedef enum {
    LE_MSG_ELECTION,
    LE_MSG_ANSWER,
    LE_MSG_COORDINATOR,
    LE_MSG_HEARTBEAT
} LEMsgType;

typedef struct {
    int id;
    int priority;
    bool is_leader;
    bool is_active;
    int leader_id;
    uint64_t last_heartbeat_ms;
    uint64_t election_timeout_ms;
} LENode;

typedef struct {
    LEMsgType type;
    int from;
    int to;
    int priority;
    int candidate_id;
} LEMessage;

typedef struct {
    LENode nodes[LE_MAX_NODES];
    int node_count;
    LEMode mode;
    uint64_t sim_time_ms;
    int elected_leader;
    bool election_in_progress;
    LEMessage msg_queue[LE_MAX_MSG];
    int msg_head;
    int msg_tail;
    int ring_order[LE_MAX_NODES];
} LENetwork;

/* Network lifecycle */
void le_init_network(LENetwork *net, int n, int priorities[], LEMode mode);
void le_tick(LENetwork *net);
void le_process_messages(LENetwork *net);

/* Bully algorithm (Garcia-Molina, 1982) */
void le_bully_start_election(LENetwork *net, int node_id);
void le_bully_handle_election(LENetwork *net, const LEMessage *msg);
void le_bully_handle_answer(LENetwork *net, const LEMessage *msg);
void le_bully_handle_coordinator(LENetwork *net, const LEMessage *msg);

/* Ring algorithm (Chang and Roberts, 1979) */
void le_ring_start_election(LENetwork *net, int node_id);
void le_ring_handle_election(LENetwork *net, const LEMessage *msg);
void le_ring_handle_coordinator(LENetwork *net, const LEMessage *msg);

/* Raft-style leader election */
void le_raft_election_timeout(LENetwork *net, int node_id);
void le_raft_handle_heartbeat(LENetwork *net, const LEMessage *msg);

/* Queries */
int  le_get_leader(const LENetwork *net);
bool le_is_leader(const LENetwork *net, int node_id);
int  le_active_count(const LENetwork *net);
bool le_all_agree_leader(const LENetwork *net);

/* Utilities */
const char *le_mode_name(LEMode mode);
const char *le_msg_name(LEMsgType type);
void le_print_network(const LENetwork *net);

#endif
