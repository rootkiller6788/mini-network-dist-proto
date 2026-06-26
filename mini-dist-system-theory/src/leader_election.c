/* leader_election.c - Bully, Ring, Raft-style leader election. Ref: MIT 6.824, DDIA Ch9. */
#include "leader_election.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void le_enqueue_msg(LENetwork *net, const LEMessage *msg) {
    int nxt = (net->msg_tail + 1) % LE_MAX_MSG;
    if (nxt == net->msg_head)
        return;
    net->msg_queue[net->msg_tail] = *msg;
    net->msg_tail = nxt;
}
static bool le_dequeue_msg(LENetwork *net, LEMessage *out) {
    if (net->msg_head == net->msg_tail)
        return false;
    *out = net->msg_queue[net->msg_head];
    net->msg_head = (net->msg_head + 1) % LE_MAX_MSG;
    return true;
}
static int le_next_ring_node(const LENetwork *net, int node_id) {
    int i;
    for (i = 0; i < net->node_count; i++) {
        if (net->ring_order[i] == node_id) {
            int ni = (i + 1) % net->node_count;
            return net->ring_order[ni];
        }
    }
    return -1;
}
void le_init_network(LENetwork *net, int n, int priorities[], LEMode mode) {
    int i;
    if (n > LE_MAX_NODES)
        n = LE_MAX_NODES;
    net->node_count = n;
    net->mode = mode;
    net->sim_time_ms = 0;
    net->elected_leader = -1;
    net->election_in_progress = false;
    net->msg_head = 0;
    net->msg_tail = 0;
    for (i = 0; i < n; i++) {
        LENode *nd = &net->nodes[i];
        nd->id = i;
        nd->priority = priorities ? priorities[i] : i;
        nd->is_leader = false;
        nd->is_active = true;
        nd->leader_id = -1;
        nd->last_heartbeat_ms = 0;
        nd->election_timeout_ms = 150 + (uint64_t)(rand() % 151);
        net->ring_order[i] = i;
    }
}
void le_tick(LENetwork *net) {
    net->sim_time_ms++;
    int i;
    if (net->mode == LE_RAFT_STYLE) {
        for (i = 0; i < net->node_count; i++) {
            LENode *nd = &net->nodes[i];
            if (!nd->is_active || nd->is_leader)
                continue;
            if (net->sim_time_ms - nd->last_heartbeat_ms > nd->election_timeout_ms)
                le_raft_election_timeout(net, i);
        }
    }
    if (net->elected_leader >= 0 && !net->election_in_progress) {
        LENode *l = &net->nodes[net->elected_leader];
        if (!l->is_active) {
            net->elected_leader = -1;
            l->is_leader = false;
        }
    }
    le_process_messages(net);
}
void le_process_messages(LENetwork *net) {
    LEMessage msg;
    while (le_dequeue_msg(net, &msg)) {
        if (net->mode == LE_BULLY) {
            switch (msg.type) {
            case LE_MSG_ELECTION:
                le_bully_handle_election(net, &msg);
                break;
            case LE_MSG_ANSWER:
                le_bully_handle_answer(net, &msg);
                break;
            case LE_MSG_COORDINATOR:
                le_bully_handle_coordinator(net, &msg);
                break;
            default:
                break;
            }
        } else if (net->mode == LE_RING) {
            switch (msg.type) {
            case LE_MSG_ELECTION:
                le_ring_handle_election(net, &msg);
                break;
            case LE_MSG_COORDINATOR:
                le_ring_handle_coordinator(net, &msg);
                break;
            default:
                break;
            }
        } else if (net->mode == LE_RAFT_STYLE) {
            if (msg.type == LE_MSG_HEARTBEAT)
                le_raft_handle_heartbeat(net, &msg);
        }
    }
}
void le_bully_start_election(LENetwork *net, int node_id) {
    net->election_in_progress = true;
    LENode *nd = &net->nodes[node_id];
    int i;
    bool found_higher = false;
    for (i = 0; i < net->node_count; i++) {
        if (i == node_id || !net->nodes[i].is_active)
            continue;
        if (net->nodes[i].priority > nd->priority) {
            LEMessage msg;
            msg.type = LE_MSG_ELECTION;
            msg.from = node_id;
            msg.to = i;
            msg.priority = nd->priority;
            msg.candidate_id = node_id;
            le_enqueue_msg(net, &msg);
            found_higher = true;
        }
    }
    if (!found_higher) {
        nd->is_leader = true;
        nd->leader_id = node_id;
        net->elected_leader = node_id;
        net->election_in_progress = false;
        for (i = 0; i < net->node_count; i++) {
            if (i == node_id)
                continue;
            LEMessage coord;
            coord.type = LE_MSG_COORDINATOR;
            coord.from = node_id;
            coord.to = i;
            coord.priority = nd->priority;
            coord.candidate_id = node_id;
            le_enqueue_msg(net, &coord);
        }
    }
}
void le_bully_handle_election(LENetwork *net, const LEMessage *msg) {
    LENode *nd = &net->nodes[msg->to];
    if (!nd->is_active)
        return;
    if (nd->priority > msg->priority) {
        LEMessage ans;
        ans.type = LE_MSG_ANSWER;
        ans.from = msg->to;
        ans.to = msg->from;
        ans.priority = nd->priority;
        ans.candidate_id = msg->to;
        le_enqueue_msg(net, &ans);
        le_bully_start_election(net, msg->to);
    }
}
void le_bully_handle_answer(LENetwork *net, const LEMessage *msg) {
    (void)net;
    (void)msg;
}
void le_bully_handle_coordinator(LENetwork *net, const LEMessage *msg) {
    int i;
    for (i = 0; i < net->node_count; i++) {
        net->nodes[i].leader_id = msg->candidate_id;
        net->nodes[i].is_leader = (i == msg->candidate_id);
    }
    net->elected_leader = msg->candidate_id;
    net->election_in_progress = false;
}
void le_ring_start_election(LENetwork *net, int node_id) {
    net->election_in_progress = true;
    int next = le_next_ring_node(net, node_id);
    if (next < 0)
        return;
    LENode *nd = &net->nodes[node_id];
    LEMessage msg;
    msg.type = LE_MSG_ELECTION;
    msg.from = node_id;
    msg.to = next;
    msg.priority = nd->priority;
    msg.candidate_id = node_id;
    le_enqueue_msg(net, &msg);
}
void le_ring_handle_election(LENetwork *net, const LEMessage *msg) {
    LENode *nd = &net->nodes[msg->to];
    if (!nd->is_active) {
        int next = le_next_ring_node(net, msg->to);
        if (next >= 0) {
            LEMessage fwd = *msg;
            fwd.to = next;
            fwd.from = msg->to;
            le_enqueue_msg(net, &fwd);
        }
        return;
    }
    if (msg->priority > nd->priority) {
        int next = le_next_ring_node(net, msg->to);
        if (next >= 0) {
            LEMessage fwd = *msg;
            fwd.to = next;
            fwd.from = msg->to;
            le_enqueue_msg(net, &fwd);
        }
    } else if (msg->priority < nd->priority) {
        LEMessage fwd;
        fwd.type = LE_MSG_ELECTION;
        fwd.from = msg->to;
        fwd.to = le_next_ring_node(net, msg->to);
        fwd.priority = nd->priority;
        fwd.candidate_id = msg->to;
        le_enqueue_msg(net, &fwd);
    } else {
        nd->is_leader = true;
        nd->leader_id = msg->to;
        net->elected_leader = msg->to;
        net->election_in_progress = false;
        LEMessage coord;
        coord.type = LE_MSG_COORDINATOR;
        coord.from = msg->to;
        coord.to = le_next_ring_node(net, msg->to);
        coord.priority = nd->priority;
        coord.candidate_id = msg->to;
        le_enqueue_msg(net, &coord);
    }
}
void le_ring_handle_coordinator(LENetwork *net, const LEMessage *msg) {
    if (msg->to == msg->candidate_id)
        return;
    net->nodes[msg->to].leader_id = msg->candidate_id;
    net->nodes[msg->to].is_leader = false;
    net->elected_leader = msg->candidate_id;
    int next = le_next_ring_node(net, msg->to);
    if (next >= 0) {
        LEMessage fwd = *msg;
        fwd.to = next;
        fwd.from = msg->to;
        le_enqueue_msg(net, &fwd);
    }
}
void le_raft_election_timeout(LENetwork *net, int node_id) {
    LENode *nd = &net->nodes[node_id];
    net->election_in_progress = true;
    int i, votes = 1;
    for (i = 0; i < net->node_count; i++) {
        if (i == node_id)
            continue;
        if (!net->nodes[i].is_active) {
            votes++;
            continue;
        }
        LEMessage msg;
        msg.type = LE_MSG_HEARTBEAT;
        msg.from = node_id;
        msg.to = i;
        msg.priority = nd->priority;
        msg.candidate_id = node_id;
        le_enqueue_msg(net, &msg);
    }
    int quorum = (net->node_count / 2) + 1;
    if (votes >= quorum) {
        nd->is_leader = true;
        nd->leader_id = node_id;
        nd->election_timeout_ms = 150 + (uint64_t)(rand() % 151);
        net->elected_leader = node_id;
        net->election_in_progress = false;
    }
}
void le_raft_handle_heartbeat(LENetwork *net, const LEMessage *msg) {
    LENode *nd = &net->nodes[msg->to];
    nd->last_heartbeat_ms = net->sim_time_ms;
    nd->leader_id = msg->from;
    nd->election_timeout_ms = 150 + (uint64_t)(rand() % 151);
    net->elected_leader = msg->from;
    net->election_in_progress = false;
}
int le_get_leader(const LENetwork *net) {
    return net->elected_leader;
}
bool le_is_leader(const LENetwork *net, int node_id) {
    if (node_id < 0 || node_id >= net->node_count)
        return false;
    return net->nodes[node_id].is_leader;
}
int le_active_count(const LENetwork *net) {
    int i, count = 0;
    for (i = 0; i < net->node_count; i++) {
        if (net->nodes[i].is_active)
            count++;
    }
    return count;
}
bool le_all_agree_leader(const LENetwork *net) {
    int l = net->elected_leader;
    if (l < 0)
        return false;
    int i;
    for (i = 0; i < net->node_count; i++) {
        if (!net->nodes[i].is_active)
            continue;
        if (net->nodes[i].leader_id != l)
            return false;
    }
    return true;
}
const char *le_mode_name(LEMode m) {
    switch (m) {
    case LE_BULLY:
        return "Bully";
    case LE_RING:
        return "Ring";
    case LE_RAFT_STYLE:
        return "Raft-style";
    default:
        return "Unknown";
    }
}
const char *le_msg_name(LEMsgType t) {
    switch (t) {
    case LE_MSG_ELECTION:
        return "ELECTION";
    case LE_MSG_ANSWER:
        return "ANSWER";
    case LE_MSG_COORDINATOR:
        return "COORDINATOR";
    case LE_MSG_HEARTBEAT:
        return "HEARTBEAT";
    default:
        return "UNKNOWN";
    }
}
void le_print_network(const LENetwork *net) {
    int i;
    printf("=== Leader Election Network (n=%d, mode=%s, leader=%d) ===\n", net->node_count,
           le_mode_name(net->mode), net->elected_leader);
    printf("Election in progress: %s\n", net->election_in_progress ? "YES" : "NO");
    for (i = 0; i < net->node_count; i++) {
        const LENode *nd = &net->nodes[i];
        printf("  Node %d: priority=%d active=%s leader=%s leader_id=%d hb_ms=%llu\n", i,
               nd->priority, nd->is_active ? "YES" : "NO", nd->is_leader ? "YES" : "NO",
               nd->leader_id, (unsigned long long)nd->last_heartbeat_ms);
    }
}
