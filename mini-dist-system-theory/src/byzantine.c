#include "byzantine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void byz_init_network(ByzantineAgreement *ba, int n, int values[],
                      bool faulty_mask[], ByzantineBehavior behaviors[])
{
    int i;
    if (n > BYZ_MAX_GENERALS) n = BYZ_MAX_GENERALS;
    ba->general_count = n;
    ba->msg_log_count = 0;
    ba->current_round = 0;
    ba->decision_reached = false;
    ba->final_decision = -1;

    for (i = 0; i < n; i++) {
        ba->generals[i].id = i;
        ba->generals[i].value = values[i];
        ba->generals[i].is_faulty = faulty_mask[i];
        ba->generals[i].behavior = behaviors[i];
    }
}

int byz_send_value(const ByzantineNode *node, int round)
{
    int v;
    (void)round;

    if (!node->is_faulty) {
        return node->value;
    }

    switch (node->behavior) {
        case BYZ_HONEST:
            return node->value;
        case BYZ_SILENT:
            return -1;
        case BYZ_RANDOM:
            return rand() % 2;
        case BYZ_TRAITOR:
            v = 1 - node->value;
            printf("  [Traitor!] General %d sends %d (real value: %d)\n",
                   node->id, v, node->value);
            return v;
        default:
            return node->value;
    }
}

void byz_collect_values(ByzantineAgreement *ba, int receiver, int round,
                        int values_out[], int *count)
{
    int i;
    *count = 0;

    for (i = 0; i < ba->general_count; i++) {
        if (i == receiver) continue;
        int v = byz_send_value(&ba->generals[i], round);
        if (v >= 0) {
            values_out[*count] = v;
            (*count)++;

            if (ba->msg_log_count < BYZ_MAX_GENERALS * BYZ_MAX_ROUNDS * 8) {
                ba->message_log[ba->msg_log_count].round = round;
                ba->message_log[ba->msg_log_count].sender = i;
                ba->message_log[ba->msg_log_count].values[0] = v;
                ba->message_log[ba->msg_log_count].value_count = 1;
                ba->msg_log_count++;
            }
        }
    }
}

int byz_majority(const int values[], int count)
{
    int zeros = 0, ones = 0;
    int i;

    if (count == 0) return rand() % 2;

    for (i = 0; i < count; i++) {
        if (values[i] == 0) zeros++;
        else if (values[i] == 1) ones++;
    }

    if (zeros > ones) return 0;
    if (ones > zeros) return 1;

    return rand() % 2;
}

bool byz_om_algorithm(ByzantineAgreement *ba, int commander, int m)
{
    int i, j, round;
    int received[BYZ_MAX_GENERALS];
    int received_count;
    int majority_val;

    printf("\n=== OM(%d) Algorithm with %d generals ===\n", m, ba->general_count);
    printf("Commander: General %d (value=%d)\n", commander, ba->generals[commander].value);

    for (round = 0; round <= m; round++) {
        ba->current_round = round;
        printf("--- Round %d ---\n", round);

        for (i = 0; i < ba->general_count; i++) {
            if (i == commander) continue;

            if (round == 0) {
                int v = byz_send_value(&ba->generals[commander], round);
                printf("  General %d received %d from commander\n", i, v >= 0 ? v : -1);
                if (v >= 0) {
                    ba->generals[i].value = v;
                }
            } else {
                int relay[BYZ_MAX_GENERALS];
                int relay_count;
                for (j = 0; j < ba->general_count; j++) {
                    if (j == i || j == commander) continue;

                    if (round == 1) {
                        int v = byz_send_value(&ba->generals[j], round);
                        printf("  General %d heard from G%d: %d\n", i, j, v >= 0 ? v : -1);
                    }
                }

                byz_collect_values(ba, i, round, relay, &relay_count);
                if (relay_count > 0) {
                    int mv = byz_majority(relay, relay_count);
                    printf("  General %d computed majority: %d\n", i, mv);
                    ba->generals[i].value = mv;
                }
            }
        }
    }

    byz_collect_values(ba, 0, m + 1, received, &received_count);
    if (received_count == 0) {
        majority_val = ba->generals[commander].value;
    } else {
        majority_val = byz_majority(received, received_count);
    }

    for (i = 0; i < ba->general_count; i++) {
        if (i != commander && ba->generals[i].value != majority_val) {
            printf("INCONSISTENCY: General %d has %d, majority is %d\n",
                   i, ba->generals[i].value, majority_val);
        }
    }

    ba->decision_reached = true;
    ba->final_decision = majority_val;

    printf("=== Final decision: %d ===\n", majority_val);

    return ba->decision_reached;
}

void byz_print_state(const ByzantineAgreement *ba)
{
    int i;
    printf("=== Byzantine Generals Network ===\n");
    printf("Generals: %d | Round: %d | Decided: %s | Decision: %d\n",
           ba->general_count, ba->current_round,
           ba->decision_reached ? "YES" : "NO",
           ba->final_decision);
    for (i = 0; i < ba->general_count; i++) {
        const char *bhv;
        switch (ba->generals[i].behavior) {
            case BYZ_HONEST:  bhv = "HONEST"; break;
            case BYZ_SILENT:  bhv = "SILENT"; break;
            case BYZ_RANDOM:  bhv = "RANDOM"; break;
            case BYZ_TRAITOR: bhv = "TRAITOR"; break;
            default:          bhv = "?"; break;
        }
        printf("  G%d: value=%d faulty=%s behavior=%s\n",
               i, ba->generals[i].value,
               ba->generals[i].is_faulty ? "YES" : "NO",
               bhv);
    }
    printf("====================================\n");
}

bool byz_check_conditions(int n, int m)
{
    if (n > 3 * m) {
        printf("Condition SATISFIED: n=%d > 3*m=%d -> consensus possible\n", n, 3*m);
        return true;
    } else {
        printf("Condition VIOLATED: n=%d <= 3*m=%d -> consensus IMPOSSIBLE\n", n, 3*m);
        return false;
    }
}
