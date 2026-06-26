#include "flp_impossibility.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void msg_enqueue(FLPProcess *proc, FLPMessage msg)
{
    int nxt = (proc->buf_tail + 1) % FLP_MSG_BUF_SIZE;
    if (nxt == proc->buf_head) {
        return;
    }
    proc->message_buf[proc->buf_tail] = msg;
    proc->buf_tail = nxt;
}

static bool msg_dequeue(FLPProcess *proc, FLPMessage *out)
{
    if (proc->buf_head == proc->buf_tail) return false;
    *out = proc->message_buf[proc->buf_head];
    proc->buf_head = (proc->buf_head + 1) % FLP_MSG_BUF_SIZE;
    return true;
}

static bool msg_pending(const FLPProcess *proc)
{
    return proc->buf_head != proc->buf_tail;
}

void flp_init(FLPSystem *sys, int n, int initial_values[])
{
    int i;

    if (n > FLP_MAX_PROCESSES) n = FLP_MAX_PROCESSES;
    sys->process_count = n;
    sys->msg_count = 0;
    sys->scheduler_index = 0;
    sys->step_count = 0;
    sys->terminated = false;

    for (i = 0; i < n; i++) {
        sys->processes[i].id = i;
        sys->processes[i].initial_value = initial_values[i];
        sys->processes[i].state = FLP_UNDECIDED;
        sys->processes[i].buf_head = 0;
        sys->processes[i].buf_tail = 0;
        sys->processes[i].round = 0;
    }
}

void flp_deliver_message(FLPSystem *sys, int msg_index)
{
    FLPProcess *proc;
    FLPMessage msg;

    if (msg_index < 0 || msg_index >= sys->msg_count) return;
    msg = sys->messages_in_flight[msg_index];

    if (msg.to < 0 || msg.to >= sys->process_count) return;
    proc = &sys->processes[msg.to];
    msg_enqueue(proc, msg);

    sys->messages_in_flight[msg_index] =
        sys->messages_in_flight[sys->msg_count - 1];
    sys->msg_count--;

    printf("  [Step %d] Message delivered: %s from P%d to P%d (value=%d)\n",
           sys->step_count,
           msg.type == FLP_MSG_PROPOSE ? "PROPOSE" : "ACK",
           msg.from, msg.to, msg.value);
}

void flp_broadcast(FLPSystem *sys, int from, FLPMessageType type, int value)
{
    int i;
    for (i = 0; i < sys->process_count; i++) {
        if (i == from) continue;
        if (sys->msg_count >= 256) continue;
        sys->messages_in_flight[sys->msg_count].type = type;
        sys->messages_in_flight[sys->msg_count].from = from;
        sys->messages_in_flight[sys->msg_count].to = i;
        sys->messages_in_flight[sys->msg_count].value = value;
        sys->msg_count++;
    }
}

static void process_event(FLPSystem *sys, FLPProcess *proc)
{
    FLPMessage msg;
    int propose_count[2] = {0, 0};
    int i;

    if (!msg_dequeue(proc, &msg)) return;

    if (msg.type == FLP_MSG_PROPOSE) {
        proc->round++;
        if (msg.value == 0) propose_count[0]++;
        else propose_count[1]++;

        while (msg_dequeue(proc, &msg)) {
            if (msg.type == FLP_MSG_PROPOSE) {
                if (msg.value == 0) propose_count[0]++;
                else propose_count[1]++;
            }
        }

        if (propose_count[0] > propose_count[1]) {
            if (proc->state == FLP_UNDECIDED) {
                proc->state = FLP_DECIDED_0;
                printf("  P%d decided 0 (round %d)\n", proc->id, proc->round);
            }
            flp_broadcast(sys, proc->id, FLP_MSG_PROPOSE, 0);
        } else if (propose_count[1] > propose_count[0]) {
            if (proc->state == FLP_UNDECIDED) {
                proc->state = FLP_DECIDED_1;
                printf("  P%d decided 1 (round %d)\n", proc->id, proc->round);
            }
            flp_broadcast(sys, proc->id, FLP_MSG_PROPOSE, 1);
        } else {
            if (proc->state == FLP_UNDECIDED) {
                int v = proc->initial_value;
                printf("  P%d tie (bivalent) - broadcasting initial value %d\n",
                       proc->id, v);
                flp_broadcast(sys, proc->id, FLP_MSG_PROPOSE, v);
            }
        }
    }
}

bool flp_schedule_step(FLPSystem *sys)
{
    int start_idx;
    int attempted;

    if (sys->terminated) return false;

    sys->step_count++;

    if (sys->msg_count == 0) {
        int i;
        bool all_waiting = true;
        for (i = 0; i < sys->process_count; i++) {
            if (sys->processes[i].state == FLP_UNDECIDED && !msg_pending(&sys->processes[i])) {
                if (sys->processes[i].round == 0) {
                    printf("  P%d has no messages - broadcasting initial value %d\n",
                           i, sys->processes[i].initial_value);
                    flp_broadcast(sys, i, FLP_MSG_PROPOSE, sys->processes[i].initial_value);
                    all_waiting = false;
                    break;
                }
            }
        }

        if (all_waiting) {
            int undecided = 0;
            printf("  No messages in flight. ");
            for (i = 0; i < sys->process_count; i++) {
                if (sys->processes[i].state == FLP_UNDECIDED) undecided++;
            }
            if (undecided == 0) {
                printf("All processes decided.\n");
                sys->terminated = true;
                return false;
            }

            printf("System in bivalent state (%d undecided). Delaying consensus...\n", undecided);

            for (i = 0; i < sys->process_count; i++) {
                if (sys->processes[i].state == FLP_UNDECIDED) {
                    int v = sys->processes[i].initial_value;
                    if (sys->step_count % 3 == 0) v = 1 - v;
                    printf("  P%d re-proposing value %d (adversarial delay)\n", i, v);
                    flp_broadcast(sys, i, FLP_MSG_PROPOSE, v);
                }
            }
        }
    }

    if (sys->msg_count > 0) {
        int pick = sys->scheduler_index % sys->msg_count;
        sys->scheduler_index = (sys->scheduler_index + 1) % sys->msg_count;

        flp_deliver_message(sys, pick);

        process_event(sys, &sys->processes[sys->messages_in_flight[pick > 0 ? pick - 1 : sys->msg_count].to]);

        return true;
    }

    return false;
}

int flp_run_until_decided(FLPSystem *sys, int max_steps)
{
    int i;
    printf("\n=== FLP Consensus Run (max %d steps) ===\n", max_steps);
    printf("Processes: %d\n", sys->process_count);
    for (i = 0; i < sys->process_count; i++) {
        printf("  P%d initial=%d\n", i, sys->processes[i].initial_value);
    }
    printf("Starting...\n\n");

    while (sys->step_count < max_steps && !sys->terminated) {
        if (!flp_schedule_step(sys)) break;

        int decided_0 = 0, decided_1 = 0, undecided = 0;
        for (i = 0; i < sys->process_count; i++) {
            if (sys->processes[i].state == FLP_DECIDED_0) decided_0++;
            if (sys->processes[i].state == FLP_DECIDED_1) decided_1++;
            if (sys->processes[i].state == FLP_UNDECIDED) undecided++;
        }

        if (decided_0 > 0 && decided_1 > 0) {
            printf("!!! SAFETY VIOLATION: both 0 and 1 decided !!!\n");
        }

        if (decided_0 == sys->process_count || decided_1 == sys->process_count) {
            printf("=== Consensus reached in %d steps! ===\n", sys->step_count);
            sys->terminated = true;
            return decided_1 > 0 ? 1 : 0;
        }
    }

    printf("=== Did not reach consensus in %d steps ===\n", sys->step_count);
    printf("(Demonstrates FLP: even with no crashes, consensus can take arbitrarily long)\n");
    return -1;
}

const char *flp_state_name(FLPProcessState state)
{
    switch (state) {
        case FLP_UNDECIDED: return "UNDECIDED";
        case FLP_DECIDED_0: return "DECIDED_0";
        case FLP_DECIDED_1: return "DECIDED_1";
        default: return "UNKNOWN";
    }
}
