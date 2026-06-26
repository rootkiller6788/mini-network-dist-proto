#ifndef LEADER_ELECTION_H
#define LEADER_ELECTION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define BULLY_MAX_NODES         10
#define RING_MAX_NODES          10
#define ZK_MAX_NODES            10
#define ELECTION_MESSAGE_LIMIT  200

typedef enum {
    BULLY_IDLE,
    BULLY_ELECTION,
    BULLY_LEADER
} BullyState;

typedef struct {
    int        id;
    int        leader_id;
    BullyState state;
    bool       active;
    int        message_count;
} BullyNode;

typedef struct {
    BullyNode nodes[BULLY_MAX_NODES];
    int       node_count;
} BullyCluster;

typedef struct {
    int  candidate_id;
    int  highest_id;
    int  hop_count;
    bool complete;
} RingElectionToken;

typedef struct {
    int         id;
    int         leader_id;
    int         next_id;
    bool        active;
    RingElectionToken token;
    bool        token_owner;
    int         message_count;
} RingNode;

typedef struct {
    RingNode nodes[RING_MAX_NODES];
    int      node_count;
} RingCluster;

typedef struct {
    int  id;
    int  sequence;
    int  leader_id;
    bool active;
    int  message_count;
} ZKNode;

typedef struct {
    ZKNode nodes[ZK_MAX_NODES];
    int    node_count;
    int    next_sequence;
} ZKCluster;

void bully_init(BullyCluster *cluster, int n);
int  bully_election(BullyCluster *cluster, int initiator_id);
void bully_declare_leader(BullyCluster *cluster, int leader_id);
void bully_node_crash(BullyCluster *cluster, int node_id);
void bully_node_recover(BullyCluster *cluster, int node_id);

void ring_init(RingCluster *cluster, int n);
int  ring_election(RingCluster *cluster, int initiator_id);
bool ring_pass_token(RingCluster *cluster, int from_id);
void ring_node_crash(RingCluster *cluster, int node_id);

void zk_init(ZKCluster *cluster, int n);
int  zk_leader_election(ZKCluster *cluster);
int  zk_create_ephemeral_sequential(ZKCluster *cluster, int node_id);
void zk_watch_leader(ZKCluster *cluster, int node_id);
void zk_node_crash(ZKCluster *cluster, int node_id);

void bully_print(const BullyCluster *cluster);
void ring_print(const RingCluster *cluster);
void zk_print(const ZKCluster *cluster);

int  bully_message_complexity(int n);
int  ring_message_complexity(int n);
int  zk_message_complexity(int n);

const char *bully_state_name(BullyState state);

#endif
