#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#define SNAP_MAX_NODES 8
#define SNAP_MAX_CHANNELS 32
#define SNAP_MAX_MSG_HISTORY 64

typedef enum {
    SNAP_MSG_APP,
    SNAP_MSG_MARKER
} SnapMsgType;

typedef struct {
    SnapMsgType type;
    int from;
    int to;
    int data;
} SnapMessage;

typedef struct {
    int state_value;
    int balance;
    bool recorded;
} SnapProcessState;

typedef struct {
    SnapMessage messages[SNAP_MAX_MSG_HISTORY];
    int msg_count;
    bool recording;
} SnapChannelState;

typedef struct {
    int id;
    SnapProcessState local_state;
    SnapChannelState channels[SNAP_MAX_NODES];
    bool marker_received[SNAP_MAX_NODES];
    bool marker_sent[SNAP_MAX_NODES];
    int neighbor_count;
    int neighbors[SNAP_MAX_NODES];
} SnapProcess;

typedef struct {
    SnapProcess processes[SNAP_MAX_NODES];
    int process_count;
    SnapMessage inflight_msgs[256];
    int msg_inflight_count;
} SnapSystem;

/* Chandy-Lamport snapshot algorithm (1985) */
void snap_init_system(SnapSystem *sys, int n, int state_values[]);
void snap_add_channel(SnapSystem *sys, int from, int to);
void snap_send_message(SnapSystem *sys, int from, int to, int data);
void snap_initiate_snapshot(SnapSystem *sys, int initiator);
void snap_process_marker(SnapSystem *sys, int proc_id, int from);
bool snap_is_complete(const SnapSystem *sys);
int  snap_collect_total(const SnapSystem *sys);
void snap_print_snapshot(const SnapSystem *sys);
void snap_reset(SnapSystem *sys);
int  snap_channel_messages_count(const SnapSystem *sys, int from, int to);
bool snap_consistency_check(const SnapSystem *sys);
const char *snap_event_name(SnapMsgType type);

#endif
