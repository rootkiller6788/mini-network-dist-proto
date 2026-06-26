#include "raft.h"
#include <stdio.h>

int main(void)
{
    RaftCluster rc;
    raft_init_cluster(&rc, 3);
    printf("=== Raft Consensus Demo ===\n");
    printf("Starting 3-node Raft cluster...\n");

    int i;
    for (i = 0; i < 500; i++) {
        raft_tick(&rc);
        while (rc.msg_head != rc.msg_tail) {
            RaftMessage msg = rc.message_queue[rc.msg_head];
            rc.msg_head = (rc.msg_head + 1) % RAFT_MAX_MSG_QUEUE;
            raft_process_message(&rc, &msg);
        }
        if (rc.leader_id >= 0 && i > 50) break;
    }

    raft_print_state(&rc);
    printf("Leader: %d, Quorum: %d\n",
           raft_get_leader(&rc), raft_quorum_size(rc.node_count));
    printf("Safety check: %s\n", raft_safety_check(&rc) ? "PASS" : "FAIL");
    printf("Agreement: %d%%\n", raft_agreement_percent(&rc));
    return 0;
}
