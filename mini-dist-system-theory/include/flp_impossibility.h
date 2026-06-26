#ifndef FLP_IMPOSSIBILITY_H
#define FLP_IMPOSSIBILITY_H

#include <stdbool.h>
#include <stdint.h>

#define FLP_MAX_PROCESSES 5
#define FLP_MSG_BUF_SIZE 32

typedef enum {
    FLP_UNDECIDED,
    FLP_DECIDED_0,
    FLP_DECIDED_1
} FLPProcessState;

typedef enum {
    FLP_MSG_PROPOSE,
    FLP_MSG_ACK
} FLPMessageType;

typedef struct {
    FLPMessageType type;
    int from;
    int to;
    int value;
} FLPMessage;

typedef struct {
    int id;
    int initial_value;
    FLPProcessState state;
    FLPMessage message_buf[FLP_MSG_BUF_SIZE];
    int buf_head;
    int buf_tail;
    int round;
} FLPProcess;

typedef struct {
    FLPProcess processes[FLP_MAX_PROCESSES];
    int process_count;
    FLPMessage messages_in_flight[256];
    int msg_count;
    int scheduler_index;
    int step_count;
    bool terminated;
} FLPSystem;

void flp_init(FLPSystem *sys, int n, int initial_values[]);
void flp_deliver_message(FLPSystem *sys, int msg_index);
bool flp_schedule_step(FLPSystem *sys);
int flp_run_until_decided(FLPSystem *sys, int max_steps);
void flp_broadcast(FLPSystem *sys, int from, FLPMessageType type, int value);
const char *flp_state_name(FLPProcessState state);

#endif
