#ifndef SWIM_H
#define SWIM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SWIM_MAX_MEMBERS        16
#define SWIM_PING_TIMEOUT_MS    200
#define SWIM_INDIRECT_PING_COUNT 3
#define SWIM_PROTOCOL_PERIOD_MS 1000
#define SWIM_SUSPECT_TIMEOUT_MS 500
#define SWIM_DEAD_TIMEOUT_MS    3000

typedef enum {
    SWIM_ALIVE,
    SWIM_SUSPECTED,
    SWIM_DEAD
} SWIMMemberState;

typedef enum {
    SWIM_MSG_PING,
    SWIM_MSG_ACK,
    SWIM_MSG_INDIRECT_PING,
    SWIM_MSG_INDIRECT_ACK,
    SWIM_MSG_JOIN,
    SWIM_MSG_LEAVE
} SWIMMessageType;

typedef struct {
    int              id;
    uint32_t         address;
    SWIMMemberState  state;
    uint64_t         incarnation;
    int              ping_target;
    uint64_t         suspect_since_ms;
    uint64_t         last_heard_ms;
    bool             active;
} SWIMMember;

typedef struct {
    SWIMMessageType type;
    int             sender_id;
    int             target_id;
    int             origin_id;
    uint64_t        incarnation;
    bool            suspected;
    SWIMMember      membership_changes[SWIM_MAX_MEMBERS];
    int             changes_count;
} SWIMMessage;

typedef struct {
    SWIMMember members[SWIM_MAX_MEMBERS];
    int        member_count;
    uint64_t   protocol_time_ms;
    int        ping_sequence_index;
} SWIMCluster;

void swim_init(SWIMCluster *cluster, int n);
void swim_join(SWIMCluster *cluster, int new_id, uint32_t address,
               int contact_id);
void swim_leave(SWIMCluster *cluster, int member_id);

bool swim_ping(SWIMCluster *cluster, int from_id, int target_id);
bool swim_ping_success(SWIMCluster *cluster, int from_id, int target_id,
                       uint64_t incarnation);
bool swim_indirect_ping(SWIMCluster *cluster, int from_id, int suspect_id,
                        int through_ids[], int through_count);
void swim_suspect(SWIMCluster *cluster, int suspect_id);
void swim_confirm_dead(SWIMCluster *cluster, int member_id);

void swim_disseminate(SWIMCluster *cluster);
SWIMMessage swim_create_ping(int sender, int target);
SWIMMessage swim_create_ack(int sender, int target, uint64_t incarnation);

void swim_on_receive(SWIMCluster *cluster, const SWIMMessage *msg);
void swim_tick(SWIMCluster *cluster, uint64_t delta_ms);

int swim_random_member(const SWIMCluster *cluster, int exclude_id);
int swim_alive_count(const SWIMCluster *cluster);
int swim_suspected_count(const SWIMCluster *cluster);
int swim_dead_count(const SWIMCluster *cluster);

void swim_print_member(const SWIMMember *member);
void swim_print_cluster(const SWIMCluster *cluster);
const char *swim_state_name(SWIMMemberState state);
const char *swim_msg_name(SWIMMessageType type);

#endif
