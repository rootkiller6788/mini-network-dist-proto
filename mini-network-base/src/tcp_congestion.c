#include "tcp_congestion.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/*
 * L4: Jacobson's RTO Algorithm (RFC 6298, Section 2)
 *
 * Mathematical foundation:
 *   After first RTT measurement R:
 *     SRTT   = R
 *     RTTVAR = R/2
 *     RTO    = SRTT + max(G, K*RTTVAR)
 *
 *   On subsequent measurement R':
 *     RTTVAR = (1 - beta)*RTTVAR + beta*|SRTT - R'|
 *     SRTT   = (1 - alpha)*SRTT + alpha*R'
 *     RTO    = SRTT + max(G, K*RTTVAR)
 *
 *   where alpha = 1/8, beta = 1/4, K = 4, G = clock granularity
 *
 * Reference: Van Jacobson, "Congestion Avoidance and Control", SIGCOMM '88
 */

void tcp_rtt_init(TCPRTTEstimator *rtt)
{
    if (!rtt) return;
    memset(rtt, 0, sizeof(TCPRTTEstimator));
    rtt->rto = 1000.0;
    rtt->rtt_measured = false;
}

void tcp_rtt_sample(TCPRTTEstimator *rtt, double measured_rtt_ms)
{
    if (!rtt || measured_rtt_ms <= 0.0) return;
    if (!rtt->rtt_measured) {
        /* First RTT measurement (RFC 6298 2.2) */
        rtt->srtt = measured_rtt_ms;
        rtt->rttvar = measured_rtt_ms / 2.0;
        rtt->rtt_measured = true;
    } else {
        double alpha = 1.0 / TCP_RTO_ALPHA_DIV;
        double beta  = 1.0 / TCP_RTO_BETA_DIV;
        double delta = rtt->srtt - measured_rtt_ms;
        if (delta < 0.0) delta = -delta;
        rtt->rttvar = (1.0 - beta) * rtt->rttvar + beta * delta;
        rtt->srtt   = (1.0 - alpha) * rtt->srtt + alpha * measured_rtt_ms;
    }
    double granularity = (double)TCP_CLOCK_GRANULARITY_MS;
    double k_var = (double)TCP_RTO_K_FACTOR * rtt->rttvar;
    double guard = (granularity > k_var) ? granularity : k_var;
    rtt->rto = rtt->srtt + guard;
    if (rtt->rto < (double)TCP_MIN_RTO_MS)
        rtt->rto = (double)TCP_MIN_RTO_MS;
    if (rtt->rto > (double)TCP_MAX_RTO_MS)
        rtt->rto = (double)TCP_MAX_RTO_MS;
}

double tcp_rto_compute(const TCPRTTEstimator *rtt)
{
    if (!rtt) return (double)TCP_MIN_RTO_MS;
    return rtt->rto;
}

void tcp_rtt_backoff(TCPRTTEstimator *rtt)
{
    if (!rtt) return;
    rtt->rto *= 2.0;
    if (rtt->rto > (double)TCP_MAX_RTO_MS)
        rtt->rto = (double)TCP_MAX_RTO_MS;
}

void tcp_rtt_timeout(TCPRTTEstimator *rtt)
{
    if (!rtt) return;
    /* Karn's algorithm: don't use RTT samples from retransmitted
     * segments. Reset for fresh measurements after timeout. */
    rtt->rtt_measured = false;
    rtt->srtt = 0.0;
    rtt->rttvar = 0.0;
}

/*
 * L4: AIMD - Additive Increase Multiplicative Decrease
 * The fundamental principle of TCP congestion control:
 *
 *   Congestion Avoidance (per RTT):
 *     cwnd += 1 MSS per RTT (additive increase)
 *
 *   On loss (multiplicative decrease):
 *     ssthresh = max(2*MSS, cwnd/2)
 *     cwnd = 1*MSS
 *
 * Reference: Chiu & Jain (1989), "Analysis of Increase/Decrease
 * Algorithms for Congestion Avoidance in Computer Networks"
 */

void tcp_cc_init(TCPCongestionControl *cc)
{
    if (!cc) return;
    memset(cc, 0, sizeof(TCPCongestionControl));
    cc->cwnd       = TCP_CWND_INIT;
    cc->ssthresh    = TCP_SSTHRESH_INIT;
    cc->inflight    = 0;
    cc->cc_state    = TCP_CC_SLOW_START;
    cc->in_recovery = false;
    cc->dup_ack_count = 0;
    cc->recover_seq = 0;
}

void tcp_stats_init(TCPTransmissionStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(TCPTransmissionStats));
}

/* Slow Start (RFC 5681 3.1): cwnd grows exponentially */
void tcp_cc_slow_start(TCPCongestionControl *cc, uint32_t bytes_acked)
{
    if (!cc || bytes_acked == 0) return;
    if (cc->cc_state != TCP_CC_SLOW_START) return;
    cc->cwnd += bytes_acked;
    if (cc->cwnd > TCP_CWND_MAX) cc->cwnd = TCP_CWND_MAX;
    if (cc->cwnd >= cc->ssthresh)
        cc->cc_state = TCP_CC_CONGESTION_AVOID;
}

/* Congestion Avoidance (RFC 5681 3.1): additive increase */
void tcp_cc_congestion_avoidance(TCPCongestionControl *cc, uint32_t bytes_acked)
{
    if (!cc || bytes_acked == 0) return;
    if (cc->cc_state != TCP_CC_CONGESTION_AVOID) return;
    uint32_t inc = bytes_acked;
    if (inc > cc->cwnd) inc = cc->cwnd;
    cc->cwnd += (inc * (uint64_t)inc) / cc->cwnd;
    if (cc->cwnd < 1) cc->cwnd = 1;
    if (cc->cwnd > TCP_CWND_MAX) cc->cwnd = TCP_CWND_MAX;
}

/* On triple dup ACK loss: ssthresh=cwnd/2, enter Fast Recovery */
void tcp_cc_on_loss(TCPCongestionControl *cc, uint32_t seq_num)
{
    if (!cc) return;
    uint32_t fs = cc->inflight > 0 ? cc->inflight : cc->cwnd;
    cc->ssthresh = fs / 2;
    if (cc->ssthresh < 2) cc->ssthresh = 2;
    cc->cwnd = cc->ssthresh + TCP_DUPACK_THRESHOLD;
    if (cc->cwnd > TCP_CWND_MAX) cc->cwnd = TCP_CWND_MAX;
    cc->recover_seq  = seq_num;
    cc->cc_state     = TCP_CC_FAST_RECOVERY;
    cc->in_recovery  = true;
}

/* On RTO timeout: severe congestion, revert to slow start */
void tcp_cc_on_timeout(TCPCongestionControl *cc)
{
    if (!cc) return;
    uint32_t fs = cc->inflight > 0 ? cc->inflight : cc->cwnd;
    cc->ssthresh = fs / 2;
    if (cc->ssthresh < 2) cc->ssthresh = 2;
    cc->cwnd = TCP_CWND_INIT;
    cc->cc_state = TCP_CC_SLOW_START;
    cc->in_recovery = false;
    cc->dup_ack_count = 0;
}

/* Duplicate ACK handling: count towards Fast Retransmit threshold */
void tcp_cc_on_dup_ack(TCPCongestionControl *cc, uint32_t ack_num)
{
    if (!cc) return;
    if (cc->in_recovery) {
        cc->cwnd += 1;
        if (cc->cwnd > TCP_CWND_MAX) cc->cwnd = TCP_CWND_MAX;
        return;
    }
    if (ack_num == cc->last_ack)
        cc->dup_ack_count++;
    else
        cc->dup_ack_count = 0;
    cc->last_ack = ack_num;
    if (cc->dup_ack_count >= TCP_DUPACK_THRESHOLD)
        cc->cc_state = TCP_CC_FAST_RETRANSMIT;
}

/* Fast Retransmit: retransmit without waiting for RTO timeout */
void tcp_cc_fast_retransmit(TCPCongestionControl *cc)
{
    if (!cc) return;
    if (cc->cc_state != TCP_CC_FAST_RETRANSMIT) return;
    tcp_cc_on_loss(cc, cc->last_ack);
    cc->dup_ack_count = 0;
}

/* Exit Fast Recovery: deflate cwnd to ssthresh */
void tcp_cc_exit_recovery(TCPCongestionControl *cc)
{
    if (!cc || !cc->in_recovery) return;
    cc->cwnd = cc->ssthresh;
    if (cc->cwnd < TCP_CWND_INIT) cc->cwnd = TCP_CWND_INIT;
    cc->cc_state     = TCP_CC_CONGESTION_AVOID;
    cc->in_recovery  = false;
    cc->dup_ack_count = 0;
}

int tcp_cc_can_send(const TCPCongestionControl *cc)
{
    if (!cc) return 0;
    return (cc->inflight < cc->cwnd) ? 1 : 0;
}

uint32_t tcp_cc_send_window(const TCPCongestionControl *cc, uint32_t adv_wnd)
{
    if (!cc) return 0;
    uint32_t eff = (cc->cwnd < adv_wnd) ? cc->cwnd : adv_wnd;
    return (eff > cc->inflight) ? (eff - cc->inflight) : 0;
}

uint32_t tcp_cc_slow_start_threshold(const TCPCongestionControl *cc)
{
    if (!cc) return 0;
    return cc->ssthresh;
}

const char* tcp_cc_state_name(TCPCCState state)
{
    switch (state) {
    case TCP_CC_SLOW_START:       return "SLOW_START";
    case TCP_CC_CONGESTION_AVOID: return "CONGESTION_AVOIDANCE";
    case TCP_CC_FAST_RECOVERY:    return "FAST_RECOVERY";
    case TCP_CC_FAST_RETRANSMIT:  return "FAST_RETRANSMIT";
    default:                       return "UNKNOWN";
    }
}

void tcp_cc_print_state(const TCPCongestionControl *cc)
{
    if (!cc) return;
    fprintf(stderr, "  [TCP CC] cwnd=%u ssthresh=%u inflight=%u state=%s recovery=%s dupacks=%u\n",
            cc->cwnd, cc->ssthresh, cc->inflight,
            tcp_cc_state_name(cc->cc_state),
            cc->in_recovery ? "yes" : "no",
            cc->dup_ack_count);
}

void tcp_rtt_print(const TCPRTTEstimator *rtt)
{
    if (!rtt) return;
    fprintf(stderr, "  [TCP RTT] srtt=%.2fms rttvar=%.2fms rto=%.2fms measured=%s\n",
            rtt->srtt, rtt->rttvar, rtt->rto,
            rtt->rtt_measured ? "yes" : "no");
}

void tcp_stats_print(const TCPTransmissionStats *stats)
{
    if (!stats) return;
    fprintf(stderr, "  [TCP Stats] sent=%llu acked=%llu retx=%llu segs=%llu seg_retx=%llu timeouts=%llu fast_rtx=%llu\n",
            (unsigned long long)stats->bytes_sent,
            (unsigned long long)stats->bytes_acked,
            (unsigned long long)stats->bytes_retransmitted,
            (unsigned long long)stats->segments_sent,
            (unsigned long long)stats->segments_retransmitted,
            (unsigned long long)stats->timeout_count,
            (unsigned long long)stats->fast_retransmit_count);
    fprintf(stderr, "              max_cwnd=%u min_ssthresh=%u\n",
            stats->max_cwnd_reached, stats->min_ssthresh_reached);
}
/*
 * L4: TCP Throughput Bounds (Mathis et al., 1997)
 *
 * Theorem: For TCP Reno with MSS bytes and RTT seconds, when packet loss
 * probability is p, the steady-state throughput is bounded by:
 *   Rate ≤ (MSS / RTT) * (C / sqrt(p))
 * where C ≈ sqrt(3/2) for Reno (delayed ACK, no timeouts).
 *
 * Padhye et al. (1998) extended model including timeouts:
 *   Rate = MSS / (RTT * sqrt(2p/3) + T0 * min(1, 3*sqrt(3p/8)) * p * (1+32p^2))
 * where T0 = retransmission timeout.
 *
 * Reference: Stanford CS 244: Advanced Topics in Networking
 *            MIT 6.829: Computer Networks
 */
double tcp_mathis_rate(double mss, double rtt, double loss_rate) {
    if (rtt <= 0.0 || loss_rate <= 0.0 || mss <= 0.0) return 0.0;
    const double C = sqrt(3.0 / 2.0);
    return (mss / rtt) * (C / sqrt(loss_rate));
}
double tcp_padhye_rate(double mss, double rtt, double t0, double p) {
    if (rtt <= 0.0 || p <= 0.0 || mss <= 0.0) return 0.0;
    double term1 = rtt * sqrt(2.0 * p / 3.0);
    double term2 = t0 * fmin(1.0, 3.0 * sqrt(3.0 * p / 8.0)) * p * (1.0 + 32.0 * p * p);
    return mss / (term1 + term2);
}

/*
 * L4: AIMD Convergence (Chiu & Jain, 1989)
 * Theorem: Additive Increase / Multiplicative Decrease is the only
 * distributed algorithm that converges to fairness and efficiency
 * using only binary feedback.  All linear controls (x_dot = a - b*x)
 * with a>0, b>0 achieve fairness.  Non-linear controls diverge.
 *
 * TCP fairness index (Jain et al. 1984):
 *   J(x1,...,xn) = (sum xi)^2 / (n * sum xi^2)
 *   where J ∈ [1/n, 1]; J=1 means perfect fairness.
 */
double tcp_jain_fairness(const double *rates, int n) {
    if (n <= 0 || !rates) return 0.0;
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        sum += rates[i];
        sum_sq += rates[i] * rates[i];
    }
    if (sum_sq <= 0.0) return 0.0;
    return (sum * sum) / ((double)n * sum_sq);
}
