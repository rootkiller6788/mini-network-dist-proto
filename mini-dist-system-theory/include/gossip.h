#ifndef GOSSIP_H
#define GOSSIP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define GOSSIP_MAX_NODES 16
#define GOSSIP_MAX_STATE_ITEMS 64
#define GOSSIP_MAX_FANOUT 4

typedef enum {
    GOSSIP_PUSH,
    GOSSIP_PULL,
    GOSSIP_PUSH_PULL
} GossipMode;

typedef struct {
    int key;
    int value;
    uint64_t version;
} GossipStateItem;

typedef struct {
    int node_id;
    GossipStateItem items[GOSSIP_MAX_STATE_ITEMS];
    int item_count;
    uint64_t round;
    int infection_count;
    int susceptible_count;
    bool infected;
} GossipNode;

typedef struct {
    int type;
    int from;
    int to;
    GossipStateItem items[GOSSIP_MAX_STATE_ITEMS];
    int item_count;
    uint64_t round;
} GossipMessage;

typedef struct {
    GossipNode nodes[GOSSIP_MAX_NODES];
    int node_count;
    GossipMode mode;
    int fanout;
    uint64_t current_round;
    GossipMessage message_queue[256];
    int msg_head;
    int msg_tail;
} GossipNetwork;

typedef enum {
    GOSSIP_STRATEGY_RANDOM,
    GOSSIP_STRATEGY_YOUNGEST,
    GOSSIP_STRATEGY_OLDEST
} GossipPeerStrategy;

void gossip_init_network(GossipNetwork *gn, int n, GossipMode mode, int fanout);
void gossip_init_node(GossipNode *node, int node_id);
void gossip_update_state(GossipNode *node, int key, int value);
bool gossip_get_state(const GossipNode *node, int key, int *value);
void gossip_merge_state(GossipNode *node, const GossipNode *other);
int  gossip_select_peer(const GossipNetwork *gn, int node_id, GossipPeerStrategy strategy);
void gossip_send_round(GossipNetwork *gn, int node_id);
void gossip_process_round(GossipNetwork *gn);
bool gossip_converged(const GossipNetwork *gn);
double gossip_infection_rate(const GossipNetwork *gn);
int  gossip_rounds_to_converge(GossipNetwork *gn, int max_rounds);
void gossip_print_network(const GossipNetwork *gn);
const char *gossip_mode_name(GossipMode mode);
const char *gossip_strategy_name(GossipPeerStrategy s);

#endif
