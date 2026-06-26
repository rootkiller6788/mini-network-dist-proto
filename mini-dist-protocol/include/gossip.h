#ifndef GOSSIP_H
#define GOSSIP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define GOSSIP_MAX_NODES       16
#define GOSSIP_MAX_NEIGHBORS   8
#define GOSSIP_MAX_DATA_KEYS   32
#define GOSSIP_MAX_ROUNDS      100
#define GOSSIP_FANOUT          3

typedef enum {
    GOSSIP_PUSH,
    GOSSIP_PULL,
    GOSSIP_PUSH_PULL
} GossipMessageType;

typedef struct {
    int      key;
    int      value;
    uint64_t version;
} GossipDataEntry;

typedef struct {
    GossipMessageType type;
    GossipDataEntry   data_entries[GOSSIP_MAX_DATA_KEYS];
    int               entry_count;
    int               sender_id;
} GossipMessage;

typedef struct {
    int             id;
    GossipDataEntry data[GOSSIP_MAX_DATA_KEYS];
    int             data_count;
    int             neighbors[GOSSIP_MAX_NEIGHBORS];
    int             neighbor_count;
    uint64_t        version_clock;
    bool            active;
} GossipNode;

void gossip_init(GossipNode *nodes, int n, int topology_type);
void gossip_init_ring(GossipNode *nodes, int n);
void gossip_init_full(GossipNode *nodes, int n);
void gossip_init_random(GossipNode *nodes, int n, int degree);
void gossip_set_data(GossipNode *node, int key, int value);
bool gossip_get_data(const GossipNode *node, int key, int *value,
                     uint64_t *version);

GossipMessage gossip_create_push(const GossipNode *node);
GossipMessage gossip_create_pull(const GossipNode *node, int keys[],
                                  int key_count);
int gossip_select_peer(const GossipNode *node);

void gossip_spread(GossipNode *nodes, int n, GossipMessageType strategy);
void gossip_on_receive(GossipNode *node, const GossipMessage *msg);
bool gossip_converge(GossipNode *nodes, int n, int max_rounds,
                     int *rounds_needed);

bool gossip_all_synced(const GossipNode *nodes, int n);
int  gossip_missing_count(const GossipNode *node, const GossipNode *other);
void gossip_print_node(const GossipNode *node);
void gossip_print_all(const GossipNode *nodes, int n);
const char *gossip_msg_type_name(GossipMessageType type);

#endif
