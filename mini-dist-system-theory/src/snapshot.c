/* snapshot.c - Chandy-Lamport Distributed Snapshot Algorithm (1985). Ref: MIT 6.824. */
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void snap_enqueue_msg(SnapSystem *sys, const SnapMessage *msg) {
    if (sys->msg_inflight_count >= 256)
        return;
    sys->inflight_msgs[sys->msg_inflight_count++] = *msg;
}
void snap_init_system(SnapSystem *sys, int n, int state_values[]) {
    int i, j;
    if (n > SNAP_MAX_NODES)
        n = SNAP_MAX_NODES;
    sys->process_count = n;
    sys->msg_inflight_count = 0;
    for (i = 0; i < n; i++) {
        SnapProcess *proc = &sys->processes[i];
        proc->id = i;
        proc->local_state.state_value = state_values ? state_values[i] : 0;
        proc->local_state.balance = 0;
        proc->local_state.recorded = false;
        proc->neighbor_count = 0;
        for (j = 0; j < SNAP_MAX_NODES; j++) {
            proc->channels[j].msg_count = 0;
            proc->channels[j].recording = false;
            proc->marker_received[j] = false;
            proc->marker_sent[j] = false;
        }
    }
}
void snap_add_channel(SnapSystem *sys, int from, int to) {
    int i;
    if (from < 0 || from >= sys->process_count || to < 0 || to >= sys->process_count || from == to)
        return;
    SnapProcess *proc = &sys->processes[from];
    if (proc->neighbor_count >= SNAP_MAX_NODES)
        return;
    for (i = 0; i < proc->neighbor_count; i++) {
        if (proc->neighbors[i] == to)
            return;
    }
    proc->neighbors[proc->neighbor_count++] = to;
}
void snap_send_message(SnapSystem *sys, int from, int to, int data) {
    if (from < 0 || from >= sys->process_count || to < 0 || to >= sys->process_count)
        return;
    SnapMessage msg;
    msg.type = SNAP_MSG_APP;
    msg.from = from;
    msg.to = to;
    msg.data = data;
    snap_enqueue_msg(sys, &msg);
}
void snap_initiate_snapshot(SnapSystem *sys, int initiator) {
    SnapProcess *proc = &sys->processes[initiator];
    int i;
    proc->local_state.recorded = true;
    for (i = 0; i < SNAP_MAX_NODES; i++) {
        proc->channels[i].recording = true;
        proc->channels[i].msg_count = 0;
    }
    for (i = 0; i < proc->neighbor_count; i++) {
        int peer = proc->neighbors[i];
        SnapMessage marker;
        marker.type = SNAP_MSG_MARKER;
        marker.from = initiator;
        marker.to = peer;
        marker.data = 0;
        snap_enqueue_msg(sys, &marker);
        proc->marker_sent[peer] = true;
    }
}
void snap_process_marker(SnapSystem *sys, int proc_id, int from) {
    SnapProcess *proc = &sys->processes[proc_id];
    if (proc->marker_received[from]) {
        proc->channels[from].recording = false;
        return;
    }
    proc->marker_received[from] = true;
    if (!proc->local_state.recorded) {
        proc->local_state.recorded = true;
        int i;
        for (i = 0; i < SNAP_MAX_NODES; i++) {
            proc->channels[i].recording = true;
            proc->channels[i].msg_count = 0;
        }
        proc->channels[from].recording = false;
        for (i = 0; i < proc->neighbor_count; i++) {
            int peer = proc->neighbors[i];
            if (!proc->marker_sent[peer]) {
                SnapMessage marker;
                marker.type = SNAP_MSG_MARKER;
                marker.from = proc_id;
                marker.to = peer;
                marker.data = 0;
                snap_enqueue_msg(sys, &marker);
                proc->marker_sent[peer] = true;
            }
        }
    } else {
        proc->channels[from].recording = false;
    }
}
bool snap_is_complete(const SnapSystem *sys) {
    int i, j;
    for (i = 0; i < sys->process_count; i++) {
        const SnapProcess *proc = &sys->processes[i];
        if (!proc->local_state.recorded)
            return false;
        for (j = 0; j < proc->neighbor_count; j++) {
            if (proc->channels[proc->neighbors[j]].recording)
                return false;
        }
    }
    return true;
}
int snap_collect_total(const SnapSystem *sys) {
    int total = 0, i, j;
    for (i = 0; i < sys->process_count; i++) {
        if (sys->processes[i].local_state.recorded)
            total += sys->processes[i].local_state.balance;
    }
    for (i = 0; i < sys->process_count; i++)
        for (j = 0; j < sys->process_count; j++)
            total += sys->processes[i].channels[j].msg_count;
    return total;
}
void snap_print_snapshot(const SnapSystem *sys) {
    int i, j;
    printf("=== Chandy-Lamport Snapshot (%d processes) ===\n", sys->process_count);
    for (i = 0; i < sys->process_count; i++) {
        const SnapProcess *proc = &sys->processes[i];
        printf("P%d: recorded=%s state_val=%d balance=%d\n", i,
               proc->local_state.recorded ? "YES" : "NO", proc->local_state.state_value,
               proc->local_state.balance);
        for (j = 0; j < SNAP_MAX_NODES; j++) {
            if (proc->channels[j].msg_count > 0)
                printf("  channel P%d->P%d: %d msgs\n", j, i, proc->channels[j].msg_count);
        }
    }
    printf("Total captured: %d\n", snap_collect_total(sys));
}
void snap_reset(SnapSystem *sys) {
    int i, j;
    for (i = 0; i < sys->process_count; i++) {
        SnapProcess *proc = &sys->processes[i];
        proc->local_state.recorded = false;
        for (j = 0; j < SNAP_MAX_NODES; j++) {
            proc->channels[j].msg_count = 0;
            proc->channels[j].recording = false;
            proc->marker_received[j] = false;
            proc->marker_sent[j] = false;
        }
    }
}
int snap_channel_messages_count(const SnapSystem *sys, int from, int to) {
    if (from < 0 || from >= sys->process_count || to < 0 || to >= sys->process_count)
        return 0;
    return sys->processes[to].channels[from].msg_count;
}
bool snap_consistency_check(const SnapSystem *sys) {
    return snap_is_complete(sys);
}
const char *snap_event_name(SnapMsgType t) {
    switch (t) {
    case SNAP_MSG_APP:
        return "APP_MSG";
    case SNAP_MSG_MARKER:
        return "MARKER";
    default:
        return "UNKNOWN";
    }
}
