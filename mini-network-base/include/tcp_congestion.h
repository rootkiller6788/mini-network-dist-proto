#ifndef TCP_CONGESTION_H
#define TCP_CONGESTION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ── L1 Definitions: TCP Congestion Control State & Algorithms ── */

/* Congestion window limits (RFC 5681) */
#define TCP_CWND_INIT       10
#define TCP_CWND_MAX         65535
#define TCP_SSTHRESH_INIT    65535
#define TCP_MIN_RTO_MS       200
#define TCP_MAX_RTO_MS       120000
#define TCP_CLOCK_GRANULARITY_MS 1

/* RFC 6298: RTO calculation constants */
#define TCP_RTO_ALPHA_DIV    8
#define TCP_RTO_BETA_DIV     4
#define TCP_RTO_K_FACTOR     4

/* RFC 2581: Fast Retransmit threshold */
#define TCP_DUPACK_THRESHOLD 3

/* Congestion control state machine */
typedef enum {
    TCP_CC_SLOW_START         = 0,
    TCP_CC_CONGESTION_AVOID   = 1,
    TCP_CC_FAST_RECOVERY      = 2,
    TCP_CC_FAST_RETRANSMIT    = 3
} TCPCCState;

/* RTT measurement sample (RFC 6298, Section 2) */
typedef struct {
    double   srtt;
    double   rttvar;
    double   rto;
    int64_t  first_rtt_ts;
    bool     rtt_measured;
    uint32_t rtt_seq;
} TCPRTTEstimator;

/* Congestion window state (RFC 5681, Section 3) */
typedef struct {
    uint32_t   cwnd;
    uint32_t   ssthresh;
    uint32_t   inflight;
    uint32_t   recover_seq;
    uint32_t   dup_ack_count;
    uint32_t   last_ack;
    TCPCCState cc_state;
    bool       in_recovery;
} TCPCongestionControl;

/* Transmission statistics */
typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_acked;
    uint64_t bytes_retransmitted;
    uint64_t segments_sent;
    uint64_t segments_retransmitted;
    uint64_t timeout_count;
    uint64_t fast_retransmit_count;
    uint32_t max_cwnd_reached;
    uint32_t min_ssthresh_reached;
} TCPTransmissionStats;

/* ── L5 Algorithms: TCP Congestion Control API ── */

void    tcp_cc_init(TCPCongestionControl *cc);
void    tcp_rtt_init(TCPRTTEstimator *rtt);
void    tcp_stats_init(TCPTransmissionStats *stats);

/* RTO computation (RFC 6298, Section 2: Jacobson's algorithm)
 * Formula: SRTT = (1-α)·SRTT + α·RTT_sample
 *          RTTVAR = (1-β)·RTTVAR + β·|SRTT - RTT_sample|
 *          RTO = SRTT + max(G, K·RTTVAR)
 * where α = 1/8, β = 1/4, K = 4, G = clock granularity */
void    tcp_rtt_sample(TCPRTTEstimator *rtt, double measured_rtt_ms);
double  tcp_rto_compute(const TCPRTTEstimator *rtt);
void    tcp_rtt_backoff(TCPRTTEstimator *rtt);
void    tcp_rtt_timeout(TCPRTTEstimator *rtt);

/* Congestion window management (RFC 5681) */
void    tcp_cc_slow_start(TCPCongestionControl *cc, uint32_t bytes_acked);
void    tcp_cc_congestion_avoidance(TCPCongestionControl *cc, uint32_t bytes_acked);
void    tcp_cc_on_loss(TCPCongestionControl *cc, uint32_t seq_num);
void    tcp_cc_on_timeout(TCPCongestionControl *cc);
void    tcp_cc_on_dup_ack(TCPCongestionControl *cc, uint32_t ack_num);
void    tcp_cc_fast_retransmit(TCPCongestionControl *cc);
void    tcp_cc_exit_recovery(TCPCongestionControl *cc);

/* Window validation */
int      tcp_cc_can_send(const TCPCongestionControl *cc);
uint32_t tcp_cc_send_window(const TCPCongestionControl *cc, uint32_t adv_wnd);
uint32_t tcp_cc_slow_start_threshold(const TCPCongestionControl *cc);

/* Statistics and diagnostics */
void        tcp_cc_print_state(const TCPCongestionControl *cc);
void        tcp_rtt_print(const TCPRTTEstimator *rtt);
void        tcp_stats_print(const TCPTransmissionStats *stats);
const char* tcp_cc_state_name(TCPCCState state);

#endif
