#include "swim.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t g_swim_seed;

static uint64_t swim_rand(void) {
    g_swim_seed = g_swim_seed * 1103515245 + 12345;
    return g_swim_seed / 65536 % 32768;
}

const char *swim_state_name(SWIMMemberState state) {
    switch (state) {
        case SWIM_ALIVE:     return "ALIVE";
        case SWIM_SUSPECTED: return "SUSPECTED";
        case SWIM_DEAD:      return "DEAD";
        default:             return "UNKNOWN";
    }
}

const char *swim_msg_name(SWIMMessageType type) {
    switch (type) {
        case SWIM_MSG_PING:          return "PING";
        case SWIM_MSG_ACK:           return "ACK";
        case SWIM_MSG_INDIRECT_PING: return "INDIRECT_PING";
        case SWIM_MSG_INDIRECT_ACK:  return "INDIRECT_ACK";
        case SWIM_MSG_JOIN:          return "JOIN";
        case SWIM_MSG_LEAVE:         return "LEAVE";
        default:                     return "UNKNOWN";
    }
}

void swim_init(SWIMCluster *cluster, int n) {
    g_swim_seed = (uint64_t)time(NULL);
    cluster->member_count       = n;
    cluster->protocol_time_ms   = 0;
    cluster->ping_sequence_index = 0;

    for (int i = 0; i < n; i++) {
        cluster->members[i].id              = i;
        cluster->members[i].address         = (uint32_t)(0xC0A80000 + i);
        cluster->members[i].state           = SWIM_ALIVE;
        cluster->members[i].incarnation     = 0;
        cluster->members[i].ping_target     = -1;
        cluster->members[i].suspect_since_ms = 0;
        cluster->members[i].last_heard_ms   = 0;
        cluster->members[i].active          = true;
    }
}

void swim_join(SWIMCluster *cluster, int new_id, uint32_t address,
               int contact_id) {
    if (cluster->member_count >= SWIM_MAX_MEMBERS) return;

    int idx = cluster->member_count;
    cluster->members[idx].id              = new_id;
    cluster->members[idx].address         = address;
    cluster->members[idx].state           = SWIM_ALIVE;
    cluster->members[idx].incarnation     = 0;
    cluster->members[idx].ping_target     = -1;
    cluster->members[idx].suspect_since_ms = 0;
    cluster->members[idx].last_heard_ms   = cluster->protocol_time_ms;
    cluster->members[idx].active          = true;
    cluster->member_count++;

    if (contact_id >= 0) {
        SWIMMessage join_msg;
        join_msg.type      = SWIM_MSG_JOIN;
        join_msg.sender_id = new_id;
        join_msg.target_id = contact_id;
        join_msg.origin_id = new_id;
        join_msg.incarnation = 0;
        join_msg.suspected  = false;
        join_msg.changes_count = 1;
        join_msg.membership_changes[0] = cluster->members[idx];
        swim_on_receive(cluster, &join_msg);
    }
}

void swim_leave(SWIMCluster *cluster, int member_id) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id == member_id) {
            cluster->members[i].state = SWIM_DEAD;
            cluster->members[i].incarnation++;
            return;
        }
    }
}

bool swim_ping(SWIMCluster *cluster, int from_id, int target_id) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id == target_id &&
            cluster->members[i].active &&
            cluster->members[i].state != SWIM_DEAD) {
            cluster->members[from_id].ping_target = target_id;
            return true;
        }
    }
    return false;
}

bool swim_ping_success(SWIMCluster *cluster, int from_id, int target_id,
                       uint64_t incarnation) {
    (void)from_id;
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id == target_id) {
            cluster->members[i].last_heard_ms = cluster->protocol_time_ms;
            cluster->members[i].ping_target   = -1;
            if (incarnation > cluster->members[i].incarnation) {
                cluster->members[i].incarnation = incarnation;
            }
            if (cluster->members[i].state == SWIM_SUSPECTED) {
                cluster->members[i].state = SWIM_ALIVE;
                cluster->members[i].incarnation++;
            }
            return true;
        }
    }
    return false;
}

bool swim_indirect_ping(SWIMCluster *cluster, int from_id, int suspect_id,
                        int through_ids[], int through_count) {
    int responses = 0;
    for (int i = 0; i < through_count; i++) {
        int via = through_ids[i];
        SWIMMessage indirect;
        indirect.type         = SWIM_MSG_INDIRECT_PING;
        indirect.sender_id    = from_id;
        indirect.target_id    = suspect_id;
        indirect.origin_id    = suspect_id;
        indirect.incarnation  = 0;
        indirect.suspected    = false;
        indirect.changes_count = 0;
        (void)indirect;  /* implicitly delivered in simulation */

        for (int j = 0; j < cluster->member_count; j++) {
            if (cluster->members[j].id == via && cluster->members[j].active) {
                SWIMMessage ack;
                ack.type         = SWIM_MSG_INDIRECT_ACK;
                ack.sender_id    = via;
                ack.target_id    = from_id;
                ack.origin_id    = suspect_id;
                ack.incarnation  = 0;
                ack.changes_count = 0;
                if (cluster->members[suspect_id].active) {
                    responses++;
                }
                (void)ack;
                break;
            }
        }
    }
    return responses > 0;
}

void swim_suspect(SWIMCluster *cluster, int suspect_id) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id == suspect_id) {
            if (cluster->members[i].state == SWIM_ALIVE) {
                cluster->members[i].state           = SWIM_SUSPECTED;
                cluster->members[i].suspect_since_ms = cluster->protocol_time_ms;
                cluster->members[i].incarnation++;
            }
            return;
        }
    }
}

void swim_confirm_dead(SWIMCluster *cluster, int member_id) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id == member_id) {
            cluster->members[i].state = SWIM_DEAD;
            cluster->members[i].incarnation++;
            return;
        }
    }
}

void swim_disseminate(SWIMCluster *cluster) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (!cluster->members[i].active) continue;

        int target = swim_random_member(cluster, i);
        if (target < 0) continue;

        SWIMMessage msg;
        msg.type         = SWIM_MSG_PING;
        msg.sender_id    = i;
        msg.target_id    = target;
        msg.origin_id    = i;
        msg.incarnation  = cluster->members[i].incarnation;
        msg.suspected    = false;
        msg.changes_count = 0;

        for (int j = 0; j < cluster->member_count; j++) {
            if (cluster->members[j].state == SWIM_SUSPECTED ||
                cluster->members[j].state == SWIM_DEAD) {
                if (msg.changes_count < SWIM_MAX_MEMBERS) {
                    msg.membership_changes[msg.changes_count] =
                        cluster->members[j];
                    msg.changes_count++;
                }
            }
        }

        swim_on_receive(cluster, &msg);
    }
}

SWIMMessage swim_create_ping(int sender, int target) {
    SWIMMessage msg;
    msg.type         = SWIM_MSG_PING;
    msg.sender_id    = sender;
    msg.target_id    = target;
    msg.origin_id    = sender;
    msg.incarnation  = 0;
    msg.suspected    = false;
    msg.changes_count = 0;
    return msg;
}

SWIMMessage swim_create_ack(int sender, int target, uint64_t incarnation) {
    SWIMMessage msg;
    msg.type         = SWIM_MSG_ACK;
    msg.sender_id    = sender;
    msg.target_id    = target;
    msg.origin_id    = sender;
    msg.incarnation  = incarnation;
    msg.suspected    = false;
    msg.changes_count = 0;
    return msg;
}

void swim_on_receive(SWIMCluster *cluster, const SWIMMessage *msg) {
    switch (msg->type) {
        case SWIM_MSG_PING: {
            for (int i = 0; i < cluster->member_count; i++) {
                if (cluster->members[i].id == msg->target_id &&
                    cluster->members[i].active) {
                    cluster->members[i].last_heard_ms = cluster->protocol_time_ms;
                }
            }
            break;
        }
        case SWIM_MSG_ACK: {
            for (int i = 0; i < cluster->member_count; i++) {
                if (cluster->members[i].id == msg->sender_id) {
                    cluster->members[i].last_heard_ms = cluster->protocol_time_ms;
                }
            }
            break;
        }
        case SWIM_MSG_INDIRECT_PING: {
            for (int i = 0; i < cluster->member_count; i++) {
                if (cluster->members[i].id == msg->origin_id &&
                    cluster->members[i].active) {
                    cluster->members[i].last_heard_ms = cluster->protocol_time_ms;
                }
            }
            break;
        }
        case SWIM_MSG_JOIN: {
            bool exists = false;
            for (int i = 0; i < cluster->member_count; i++) {
                if (cluster->members[i].id == msg->origin_id) {
                    exists = true;
                    break;
                }
            }
            if (!exists && cluster->member_count < SWIM_MAX_MEMBERS) {
                int idx = cluster->member_count;
                cluster->members[idx] = msg->membership_changes[0];
                cluster->members[idx].last_heard_ms = cluster->protocol_time_ms;
                cluster->member_count++;
            }
            break;
        }
        default:
            break;
    }

    for (int c = 0; c < msg->changes_count; c++) {
        const SWIMMember *change = &msg->membership_changes[c];
        for (int i = 0; i < cluster->member_count; i++) {
            if (cluster->members[i].id == change->id) {
                if (change->incarnation > cluster->members[i].incarnation) {
                    cluster->members[i].incarnation = change->incarnation;
                    cluster->members[i].state       = change->state;
                    if (change->state == SWIM_SUSPECTED) {
                        cluster->members[i].suspect_since_ms =
                            cluster->protocol_time_ms;
                    }
                }
                break;
            }
        }
    }
}

void swim_tick(SWIMCluster *cluster, uint64_t delta_ms) {
    cluster->protocol_time_ms += delta_ms;

    for (int i = 0; i < cluster->member_count; i++) {
        if (!cluster->members[i].active) continue;

        if (cluster->members[i].state == SWIM_SUSPECTED &&
            cluster->protocol_time_ms - cluster->members[i].suspect_since_ms >
            SWIM_DEAD_TIMEOUT_MS) {
            cluster->members[i].state = SWIM_DEAD;
            cluster->members[i].incarnation++;
        }
    }

    if (cluster->protocol_time_ms % SWIM_PROTOCOL_PERIOD_MS < delta_ms ||
        cluster->protocol_time_ms < delta_ms) {
        int target = swim_random_member(cluster, cluster->ping_sequence_index);
        if (target >= 0) {
            swim_ping(cluster, cluster->ping_sequence_index, target);

            if ((swim_rand() % 100) < 20) {
                swim_ping_success(cluster, cluster->ping_sequence_index,
                                  target, 0);
            } else {
                int indirect[SWIM_INDIRECT_PING_COUNT];
                int count = 0;
                for (int j = 0; j < cluster->member_count && count < SWIM_INDIRECT_PING_COUNT; j++) {
                    if (cluster->members[j].id != cluster->ping_sequence_index &&
                        cluster->members[j].id != target &&
                        cluster->members[j].active) {
                        indirect[count++] = cluster->members[j].id;
                    }
                }
                if (count > 0) {
                    bool ok = swim_indirect_ping(cluster,
                                                  cluster->ping_sequence_index,
                                                  target, indirect, count);
                    if (!ok) {
                        swim_suspect(cluster, target);
                    }
                }
            }
        }
        cluster->ping_sequence_index =
            (cluster->ping_sequence_index + 1) % cluster->member_count;

        swim_disseminate(cluster);
    }
}

int swim_random_member(const SWIMCluster *cluster, int exclude_id) {
    int alive[SWIM_MAX_MEMBERS];
    int alive_count = 0;
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].id != exclude_id &&
            cluster->members[i].active &&
            cluster->members[i].state != SWIM_DEAD) {
            alive[alive_count++] = i;
        }
    }
    if (alive_count == 0) return -1;
    return cluster->members[alive[swim_rand() % alive_count]].id;
}

int swim_alive_count(const SWIMCluster *cluster) {
    int count = 0;
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].state == SWIM_ALIVE) count++;
    }
    return count;
}

int swim_suspected_count(const SWIMCluster *cluster) {
    int count = 0;
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].state == SWIM_SUSPECTED) count++;
    }
    return count;
}

int swim_dead_count(const SWIMCluster *cluster) {
    int count = 0;
    for (int i = 0; i < cluster->member_count; i++) {
        if (cluster->members[i].state == SWIM_DEAD) count++;
    }
    return count;
}

void swim_print_member(const SWIMMember *member) {
    printf("  Member %d [%s] addr=0x%08X inc=%llu last_heard=%llu\n",
           member->id,
           swim_state_name(member->state),
           member->address,
           (unsigned long long)member->incarnation,
           (unsigned long long)member->last_heard_ms);
}

void swim_print_cluster(const SWIMCluster *cluster) {
    printf("=== SWIM Cluster (%d members, time=%llu) ===\n",
           cluster->member_count,
           (unsigned long long)cluster->protocol_time_ms);
    printf("  Alive=%d Suspected=%d Dead=%d\n",
           swim_alive_count(cluster),
           swim_suspected_count(cluster),
           swim_dead_count(cluster));
    for (int i = 0; i < cluster->member_count; i++) {
        swim_print_member(&cluster->members[i]);
    }
}
