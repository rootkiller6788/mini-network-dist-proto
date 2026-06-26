/* gossip.c - Epidemic/Gossip Protocols. Reference: Demers et al. 1987, MIT 6.824. */
#include "gossip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void gossip_enqueue(GossipNetwork *gn, const GossipMessage *msg) {
    int nxt = (gn->msg_tail + 1) % 256;
    if (nxt == gn->msg_head)
        return;
    gn->message_queue[gn->msg_tail] = *msg;
    gn->msg_tail = nxt;
}
static bool gossip_dequeue(GossipNetwork *gn, GossipMessage *out) {
    if (gn->msg_head == gn->msg_tail)
        return false;
    *out = gn->message_queue[gn->msg_head];
    gn->msg_head = (gn->msg_head + 1) % 256;
    return true;
}
static int find_item_index(const GossipNode *node, int key) {
    int i;
    for (i = 0; i < node->item_count; i++) {
        if (node->items[i].key == key)
            return i;
    }
    return -1;
}
void gossip_init_network(GossipNetwork *gn, int n, GossipMode mode, int fanout) {
    int i;
    if (n > GOSSIP_MAX_NODES)
        n = GOSSIP_MAX_NODES;
    gn->node_count = n;
    gn->mode = mode;
    gn->fanout = fanout > 0 ? fanout : 3;
    gn->current_round = 0;
    gn->msg_head = 0;
    gn->msg_tail = 0;
    for (i = 0; i < n; i++)
        gossip_init_node(&gn->nodes[i], i);
}
void gossip_init_node(GossipNode *node, int node_id) {
    node->node_id = node_id;
    node->item_count = 0;
    node->round = 0;
    node->infection_count = 0;
    node->susceptible_count = 0;
    node->infected = false;
}
void gossip_update_state(GossipNode *node, int key, int value) {
    int idx = find_item_index(node, key);
    if (idx >= 0) {
        node->items[idx].value = value;
        node->items[idx].version++;
    } else if (node->item_count < GOSSIP_MAX_STATE_ITEMS) {
        node->items[node->item_count].key = key;
        node->items[node->item_count].value = value;
        node->items[node->item_count].version = 1;
        node->item_count++;
    }
    if (!node->infected) {
        node->infected = true;
        node->infection_count = 1;
    }
}
bool gossip_get_state(const GossipNode *node, int key, int *value) {
    int idx = find_item_index(node, key);
    if (idx < 0)
        return false;
    if (value)
        *value = node->items[idx].value;
    return true;
}
void gossip_merge_state(GossipNode *node, const GossipNode *other) {
    int i;
    for (i = 0; i < other->item_count; i++) {
        const GossipStateItem *oi = &other->items[i];
        int idx = find_item_index(node, oi->key);
        if (idx < 0) {
            if (node->item_count < GOSSIP_MAX_STATE_ITEMS) {
                node->items[node->item_count] = *oi;
                node->item_count++;
            }
        } else if (oi->version > node->items[idx].version)
            node->items[idx] = *oi;
    }
    if (other->infected && !node->infected) {
        node->infected = true;
        node->infection_count = 1;
    }
}
int gossip_select_peer(const GossipNetwork *gn, int node_id, GossipPeerStrategy strategy) {
    int i, candidate, count;
    switch (strategy) {
    case GOSSIP_STRATEGY_RANDOM:
        count = 0;
        for (i = 0; i < gn->node_count; i++) {
            if (i != node_id)
                count++;
        }
        if (count == 0)
            return -1;
        candidate = rand() % count;
        for (i = 0; i < gn->node_count; i++) {
            if (i != node_id && candidate-- == 0)
                return i;
        }
        break;
    case GOSSIP_STRATEGY_YOUNGEST: {
        int min_round = (int)gn->current_round + 1;
        candidate = -1;
        for (i = 0; i < gn->node_count; i++) {
            if (i == node_id)
                continue;
            if ((int)gn->nodes[i].round < min_round) {
                min_round = (int)gn->nodes[i].round;
                candidate = i;
            }
        }
        return candidate;
    }
    case GOSSIP_STRATEGY_OLDEST: {
        int max_round = -1;
        candidate = -1;
        for (i = 0; i < gn->node_count; i++) {
            if (i == node_id)
                continue;
            if ((int)gn->nodes[i].round > max_round) {
                max_round = (int)gn->nodes[i].round;
                candidate = i;
            }
        }
        return candidate;
    }
    default:
        break;
    }
    return -1;
}
void gossip_send_round(GossipNetwork *gn, int node_id) {
    GossipNode *node = &gn->nodes[node_id];
    int f;
    for (f = 0; f < gn->fanout; f++) {
        int peer = gossip_select_peer(gn, node_id, GOSSIP_STRATEGY_RANDOM);
        if (peer < 0)
            continue;
        GossipMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.from = node_id;
        msg.to = peer;
        msg.round = gn->current_round;
        if (gn->mode == GOSSIP_PUSH || gn->mode == GOSSIP_PUSH_PULL) {
            msg.type = 0;
            msg.item_count = node->item_count;
            memcpy(msg.items, node->items, node->item_count * sizeof(GossipStateItem));
            gossip_enqueue(gn, &msg);
        }
        if (gn->mode == GOSSIP_PULL || gn->mode == GOSSIP_PUSH_PULL) {
            msg.type = 1;
            msg.item_count = 0;
            memset(msg.items, 0, sizeof(msg.items));
            gossip_enqueue(gn, &msg);
        }
    }
}
void gossip_process_round(GossipNetwork *gn) {
    GossipMessage msg;
    while (gossip_dequeue(gn, &msg)) {
        GossipNode *sender = &gn->nodes[msg.from];
        GossipNode *receiver = &gn->nodes[msg.to];
        if (msg.type == 0) {
            gossip_merge_state(receiver, sender);
            receiver->round = gn->current_round;
        } else {
            GossipNode temp;
            gossip_init_node(&temp, msg.from);
            gossip_merge_state(&temp, receiver);
            gossip_merge_state(sender, &temp);
            sender->round = gn->current_round;
        }
    }
    int i;
    for (i = 0; i < gn->node_count; i++)
        gn->nodes[i].susceptible_count = gn->node_count - gn->nodes[i].infection_count;
    gn->current_round++;
}
bool gossip_converged(const GossipNetwork *gn) {
    int i, j;
    if (gn->node_count < 2)
        return true;
    const GossipNode *ref = &gn->nodes[0];
    for (i = 1; i < gn->node_count; i++) {
        const GossipNode *cur = &gn->nodes[i];
        if (cur->item_count != ref->item_count)
            return false;
        for (j = 0; j < ref->item_count; j++) {
            int val;
            if (!gossip_get_state(cur, ref->items[j].key, &val))
                return false;
            if (val != ref->items[j].value)
                return false;
        }
    }
    return true;
}
double gossip_infection_rate(const GossipNetwork *gn) {
    int inf = 0, i;
    for (i = 0; i < gn->node_count; i++) {
        if (gn->nodes[i].infected)
            inf++;
    }
    return gn->node_count > 0 ? (double)inf / gn->node_count : 0.0;
}
int gossip_rounds_to_converge(GossipNetwork *gn, int max_rounds) {
    int r;
    for (r = 0; r < max_rounds; r++) {
        if (gossip_converged(gn))
            return r;
        int i;
        for (i = 0; i < gn->node_count; i++)
            gossip_send_round(gn, i);
        gossip_process_round(gn);
    }
    return -1;
}
void gossip_print_network(const GossipNetwork *gn) {
    int i, j;
    printf("=== Gossip Network (n=%d, mode=%s, round=%llu) ===\n", gn->node_count,
           gossip_mode_name(gn->mode), (unsigned long long)gn->current_round);
    printf("Infection rate: %.1f%%\n", gossip_infection_rate(gn) * 100.0);
    for (i = 0; i < gn->node_count; i++) {
        const GossipNode *n = &gn->nodes[i];
        printf("  Node %d: items=%d infected=%s round=%llu\n", i, n->item_count,
               n->infected ? "YES" : "NO", (unsigned long long)n->round);
        for (j = 0; j < n->item_count; j++)
            printf("    key=%d val=%d ver=%llu\n", n->items[j].key, n->items[j].value,
                   (unsigned long long)n->items[j].version);
    }
}
const char *gossip_mode_name(GossipMode m) {
    switch (m) {
    case GOSSIP_PUSH:
        return "Push";
    case GOSSIP_PULL:
        return "Pull";
    case GOSSIP_PUSH_PULL:
        return "Push-Pull";
    default:
        return "Unknown";
    }
}
const char *gossip_strategy_name(GossipPeerStrategy s) {
    switch (s) {
    case GOSSIP_STRATEGY_RANDOM:
        return "Random";
    case GOSSIP_STRATEGY_YOUNGEST:
        return "Youngest";
    case GOSSIP_STRATEGY_OLDEST:
        return "Oldest";
    default:
        return "Unknown";
    }
}
