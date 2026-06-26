#include "gossip.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t g_gossip_seed;

static uint64_t gossip_rand(void) {
    g_gossip_seed = g_gossip_seed * 1103515245 + 12345;
    return g_gossip_seed / 65536 % 32768;
}

const char *gossip_msg_type_name(GossipMessageType type) {
    switch (type) {
        case GOSSIP_PUSH:      return "PUSH";
        case GOSSIP_PULL:      return "PULL";
        case GOSSIP_PUSH_PULL: return "PUSH_PULL";
        default:               return "UNKNOWN";
    }
}

void gossip_init(GossipNode *nodes, int n, int topology_type) {
    g_gossip_seed = (uint64_t)time(NULL);
    switch (topology_type) {
        case 0: gossip_init_ring(nodes, n);   break;
        case 1: gossip_init_full(nodes, n);   break;
        case 2: gossip_init_random(nodes, n, 3); break;
        default: gossip_init_full(nodes, n);  break;
    }
}

void gossip_init_ring(GossipNode *nodes, int n) {
    for (int i = 0; i < n; i++) {
        nodes[i].id            = i;
        nodes[i].data_count    = 0;
        nodes[i].version_clock = 0;
        nodes[i].active        = true;
        nodes[i].neighbor_count = 2;
        nodes[i].neighbors[0]  = (i + 1) % n;
        nodes[i].neighbors[1]  = (i - 1 + n) % n;
    }
}

void gossip_init_full(GossipNode *nodes, int n) {
    for (int i = 0; i < n; i++) {
        nodes[i].id            = i;
        nodes[i].data_count    = 0;
        nodes[i].version_clock = 0;
        nodes[i].active        = true;
        nodes[i].neighbor_count = n - 1;
        int idx = 0;
        for (int j = 0; j < n; j++) {
            if (j != i) {
                nodes[i].neighbors[idx++] = j;
            }
        }
    }
}

void gossip_init_random(GossipNode *nodes, int n, int degree) {
    g_gossip_seed = (uint64_t)time(NULL);
    for (int i = 0; i < n; i++) {
        nodes[i].id            = i;
        nodes[i].data_count    = 0;
        nodes[i].version_clock = 0;
        nodes[i].active        = true;
        nodes[i].neighbor_count = 0;

        for (int d = 0; d < degree && nodes[i].neighbor_count < n - 1; d++) {
            int peer = (int)(gossip_rand() % n);
            if (peer != i) {
                bool already = false;
                for (int k = 0; k < nodes[i].neighbor_count; k++) {
                    if (nodes[i].neighbors[k] == peer) { already = true; break; }
                }
                if (!already && nodes[i].neighbor_count < GOSSIP_MAX_NEIGHBORS) {
                    nodes[i].neighbors[nodes[i].neighbor_count++] = peer;
                }
            }
        }
    }
}

void gossip_set_data(GossipNode *node, int key, int value) {
    for (int i = 0; i < node->data_count; i++) {
        if (node->data[i].key == key) {
            node->data[i].value   = value;
            node->data[i].version = ++node->version_clock;
            return;
        }
    }
    if (node->data_count < GOSSIP_MAX_DATA_KEYS) {
        node->data[node->data_count].key     = key;
        node->data[node->data_count].value   = value;
        node->data[node->data_count].version = ++node->version_clock;
        node->data_count++;
    }
}

bool gossip_get_data(const GossipNode *node, int key, int *value,
                     uint64_t *version) {
    for (int i = 0; i < node->data_count; i++) {
        if (node->data[i].key == key) {
            *value   = node->data[i].value;
            *version = node->data[i].version;
            return true;
        }
    }
    return false;
}

GossipMessage gossip_create_push(const GossipNode *node) {
    GossipMessage msg;
    msg.type       = GOSSIP_PUSH;
    msg.sender_id  = node->id;
    msg.entry_count = node->data_count;
    for (int i = 0; i < node->data_count; i++) {
        msg.data_entries[i] = node->data[i];
    }
    return msg;
}

GossipMessage gossip_create_pull(const GossipNode *node, int keys[],
                                  int key_count) {
    GossipMessage msg;
    msg.type       = GOSSIP_PULL;
    msg.sender_id  = node->id;
    msg.entry_count = 0;
    for (int i = 0; i < key_count && i < GOSSIP_MAX_DATA_KEYS; i++) {
        int k = keys != NULL ? keys[i] : i;
        for (int j = 0; j < node->data_count; j++) {
            if (node->data[j].key == k) {
                msg.data_entries[msg.entry_count++] = node->data[j];
                break;
            }
        }
    }
    return msg;
}

int gossip_select_peer(const GossipNode *node) {
    if (node->neighbor_count == 0) return -1;
    return node->neighbors[(gossip_rand() % node->neighbor_count)];
}

void gossip_spread(GossipNode *nodes, int n, GossipMessageType strategy) {
    for (int i = 0; i < n; i++) {
        if (!nodes[i].active || nodes[i].data_count == 0) continue;

        int fanout = GOSSIP_FANOUT;
        for (int f = 0; f < fanout; f++) {
            int target = gossip_select_peer(&nodes[i]);
            if (target < 0) continue;

            GossipMessage msg;
            msg.sender_id = i;

            switch (strategy) {
                case GOSSIP_PUSH: {
                    msg = gossip_create_push(&nodes[i]);
                    msg.type = GOSSIP_PUSH;
                    break;
                }
                case GOSSIP_PULL: {
                    msg.type = GOSSIP_PULL;
                    msg.entry_count = 0;
                    break;
                }
                case GOSSIP_PUSH_PULL: {
                    msg = gossip_create_push(&nodes[i]);
                    msg.type = GOSSIP_PUSH;
                    gossip_on_receive(&nodes[target], &msg);

                    GossipMessage pull_msg;
                    pull_msg.type      = GOSSIP_PUSH;
                    pull_msg.sender_id = target;
                    pull_msg = gossip_create_push(&nodes[target]);
                    pull_msg.sender_id = target;
                    gossip_on_receive(&nodes[i], &pull_msg);
                    continue;
                }
            }

            gossip_on_receive(&nodes[target], &msg);

            if (strategy == GOSSIP_PULL) {
                GossipMessage response = gossip_create_push(&nodes[target]);
                response.sender_id = target;
                gossip_on_receive(&nodes[i], &response);
            }
        }
    }
}

void gossip_on_receive(GossipNode *node, const GossipMessage *msg) {
    for (int i = 0; i < msg->entry_count; i++) {
        const GossipDataEntry *incoming = &msg->data_entries[i];
        bool found = false;

        for (int j = 0; j < node->data_count; j++) {
            if (node->data[j].key == incoming->key) {
                found = true;
                if (incoming->version > node->data[j].version) {
                    node->data[j].value   = incoming->value;
                    node->data[j].version = incoming->version;
                }
                break;
            }
        }

        if (!found && node->data_count < GOSSIP_MAX_DATA_KEYS) {
            node->data[node->data_count] = *incoming;
            node->data_count++;
        }
    }

    if (msg->entry_count > 0 && msg->data_entries[0].version > node->version_clock) {
        node->version_clock = msg->data_entries[0].version;
    }
}

bool gossip_converge(GossipNode *nodes, int n, int max_rounds,
                     int *rounds_needed) {
    if (max_rounds <= 0) max_rounds = GOSSIP_MAX_ROUNDS;

    for (int r = 0; r < max_rounds; r++) {
        gossip_spread(nodes, n, GOSSIP_PUSH_PULL);

        if (gossip_all_synced(nodes, n)) {
            *rounds_needed = r + 1;
            return true;
        }
    }

    *rounds_needed = max_rounds;
    return false;
}

bool gossip_all_synced(const GossipNode *nodes, int n) {
    int active_nodes[GOSSIP_MAX_NODES];
    int active_count = 0;
    for (int i = 0; i < n; i++) {
        if (nodes[i].active) {
            active_nodes[active_count++] = i;
        }
    }
    if (active_count < 2) return true;

    int ref = active_nodes[0];

    for (int a = 1; a < active_count; a++) {
        int idx = active_nodes[a];
        if (nodes[idx].data_count != nodes[ref].data_count) return false;

        for (int i = 0; i < nodes[ref].data_count; i++) {
            int      val;
            uint64_t ver;
            if (!gossip_get_data(&nodes[idx], nodes[ref].data[i].key,
                                 &val, &ver)) {
                return false;
            }
            if (ver != nodes[ref].data[i].version) return false;
        }
    }
    return true;
}

int gossip_missing_count(const GossipNode *node, const GossipNode *other) {
    int missing = 0;
    for (int i = 0; i < node->data_count; i++) {
        int      val;
        uint64_t ver;
        if (!gossip_get_data(other, node->data[i].key, &val, &ver)) {
            missing++;
        }
    }
    return missing;
}

void gossip_print_node(const GossipNode *node) {
    printf("  Node %d (v=%llu) data=[",
           node->id, (unsigned long long)node->version_clock);
    for (int i = 0; i < node->data_count; i++) {
        printf("%s%d:%d@v%llu",
               i > 0 ? ", " : "",
               node->data[i].key,
               node->data[i].value,
               (unsigned long long)node->data[i].version);
    }
    printf("]\n");
}

void gossip_print_all(const GossipNode *nodes, int n) {
    printf("=== Gossip Cluster (%d nodes) ===\n", n);
    for (int i = 0; i < n; i++) {
        gossip_print_node(&nodes[i]);
    }
}
