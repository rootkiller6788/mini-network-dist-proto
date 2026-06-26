#ifndef BYZANTINE_H
#define BYZANTINE_H

#include <stdbool.h>
#include <stdint.h>

#define BYZ_MAX_GENERALS 8
#define BYZ_MAX_ROUNDS 8

typedef enum {
    BYZ_HONEST,
    BYZ_SILENT,
    BYZ_RANDOM,
    BYZ_TRAITOR
} ByzantineBehavior;

typedef struct {
    int id;
    int value;
    bool is_faulty;
    ByzantineBehavior behavior;
} ByzantineNode;

typedef struct {
    int round;
    int values[BYZ_MAX_GENERALS];
    int value_count;
    int sender;
} ByzantineMessage;

typedef struct {
    ByzantineNode generals[BYZ_MAX_GENERALS];
    int general_count;
    ByzantineMessage message_log[BYZ_MAX_GENERALS * BYZ_MAX_ROUNDS * 8];
    int msg_log_count;
    int current_round;
    bool decision_reached;
    int final_decision;
} ByzantineAgreement;

void byz_init_network(ByzantineAgreement *ba, int n, int values[], bool faulty_mask[], ByzantineBehavior behaviors[]);
int byz_send_value(const ByzantineNode *node, int round);
void byz_collect_values(ByzantineAgreement *ba, int receiver, int round, int values_out[], int *count);
int byz_majority(const int values[], int count);
bool byz_om_algorithm(ByzantineAgreement *ba, int commander, int m);
void byz_print_state(const ByzantineAgreement *ba);
bool byz_check_conditions(int n, int m);

#endif
