#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rate_limiter.h"

static double get_time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

RateLimiter* rl_init(RLAlgorithm algo, double rate, double burst,
                     uint64_t window_ms)
{
    RateLimiter *rl = calloc(1, sizeof(RateLimiter));
    if (!rl) return NULL;

    rl->algorithm = algo;
    rl->rate = rate;
    rl->burst = burst;
    rl->num_clients = 0;

    if (burst <= 0.0) rl->burst = rate > 0 ? rate : 10.0;

    switch (algo) {
    case RL_TOKEN_BUCKET:
        rl->tb.tokens = rl->burst;
        rl->tb.capacity = rl->burst;
        rl->tb.refill_rate = rate;
        rl->tb.last_refill = get_time_seconds();
        printf("[rl] Token bucket: rate=%.1f, burst=%.1f\n", rate, rl->burst);
        break;

    case RL_LEAKY_BUCKET:
        rl->lb.capacity = rl->burst;
        rl->lb.water_level = 0.0;
        rl->lb.drain_rate = rate;
        rl->lb.last_drain = get_time_seconds();
        printf("[rl] Leaky bucket: rate=%.1f, capacity=%.1f\n", rate, rl->burst);
        break;

    case RL_FIXED_WINDOW:
        rl->fw.window_size_ms = window_ms > 0 ? window_ms : 1000;
        rl->fw.max_requests = (int)rate;
        rl->fw.current_count = 0;
        rl->fw.window_start_ms = get_time_ms();
        printf("[rl] Fixed window: %d req/%llums\n",
               rl->fw.max_requests, (unsigned long long)rl->fw.window_size_ms);
        break;

    case RL_SLIDING_WINDOW_LOG:
        rl->sw.window_size_ms = window_ms > 0 ? window_ms : 1000;
        rl->sw.max_requests = (int)rate;
        rl->sw.timestamp_count = 0;
        rl->sw.head = 0;
        rl->sw.tail = 0;
        printf("[rl] Sliding window: %d req/%llums\n",
               rl->sw.max_requests, (unsigned long long)rl->sw.window_size_ms);
        break;
    }

    return rl;
}

bool rl_allow(RateLimiter *rl)
{
    if (!rl) return false;

    rl_refill(rl);

    switch (rl->algorithm) {
    case RL_TOKEN_BUCKET:
        if (rl->tb.tokens >= 1.0) {
            rl->tb.tokens -= 1.0;
            return true;
        }
        return false;

    case RL_LEAKY_BUCKET:
        if (rl->lb.water_level + 1.0 <= rl->lb.capacity) {
            rl->lb.water_level += 1.0;
            return true;
        }
        return false;

    case RL_FIXED_WINDOW: {
        uint64_t now = get_time_ms();
        if (now - rl->fw.window_start_ms >= rl->fw.window_size_ms) {
            rl->fw.window_start_ms = now;
            rl->fw.current_count = 0;
        }
        if (rl->fw.current_count < rl->fw.max_requests) {
            rl->fw.current_count++;
            return true;
        }
        return false;
    }

    case RL_SLIDING_WINDOW_LOG: {
        uint64_t now = get_time_ms();
        uint64_t window_start = now - rl->sw.window_size_ms;

        /* evict old timestamps */
        while (rl->sw.timestamp_count > 0 &&
               rl->sw.timestamps[rl->sw.head] < window_start) {
            rl->sw.head = (rl->sw.head + 1) % RL_MAX_TIMESTAMPS;
            rl->sw.timestamp_count--;
        }

        if (rl->sw.timestamp_count < rl->sw.max_requests) {
            rl->sw.timestamps[rl->sw.tail] = now;
            rl->sw.tail = (rl->sw.tail + 1) % RL_MAX_TIMESTAMPS;
            rl->sw.timestamp_count++;
            return true;
        }
        return false;
    }
    }

    return false;
}

bool rl_allow_client(RateLimiter *rl, const char *client_id)
{
    if (!rl || !client_id) return false;

    ClientLimiter *cl = rl_get_or_create_client(rl, client_id);
    if (!cl) return false;

    switch (cl->algorithm) {
    case RL_TOKEN_BUCKET:
        rl_refill(rl); /* refill parent for global rate */
        if (cl->tb.tokens >= 1.0 && rl_allow(rl)) {
            cl->tb.tokens -= 1.0;
            return true;
        }
        return false;

    case RL_FIXED_WINDOW: {
        uint64_t now = get_time_ms();
        if (now - cl->fw.window_start_ms >= cl->fw.window_size_ms) {
            cl->fw.window_start_ms = now;
            cl->fw.current_count = 0;
        }
        if (cl->fw.current_count < cl->fw.max_requests && rl_allow(rl)) {
            cl->fw.current_count++;
            return true;
        }
        return false;
    }

    default:
        return rl_allow(rl);
    }
}

ClientLimiter* rl_get_or_create_client(RateLimiter *rl, const char *client_id)
{
    for (int i = 0; i < rl->num_clients; i++) {
        if (strcmp(rl->clients[i].client_id, client_id) == 0) {
            return &rl->clients[i];
        }
    }

    if (rl->num_clients >= RL_MAX_CLIENTS) return NULL;

    ClientLimiter *cl = &rl->clients[rl->num_clients++];
    memset(cl, 0, sizeof(ClientLimiter));
    snprintf(cl->client_id, sizeof(cl->client_id), "%s", client_id);
    cl->algorithm = rl->algorithm;
    cl->max_rps = (int)rl->rate;

    if (rl->algorithm == RL_TOKEN_BUCKET) {
        cl->tb.tokens = rl->burst;
        cl->tb.capacity = rl->burst;
        cl->tb.refill_rate = rl->rate;
        cl->tb.last_refill = get_time_seconds();
    } else if (rl->algorithm == RL_FIXED_WINDOW) {
        cl->fw.window_size_ms = rl->fw.window_size_ms;
        cl->fw.max_requests = cl->max_rps;
        cl->fw.current_count = 0;
        cl->fw.window_start_ms = get_time_ms();
    }

    return cl;
}

void rl_refill(RateLimiter *rl)
{
    if (!rl) return;

    switch (rl->algorithm) {
    case RL_TOKEN_BUCKET: {
        double now = get_time_seconds();
        double elapsed = now - rl->tb.last_refill;
        double new_tokens = elapsed * rl->tb.refill_rate;
        rl->tb.tokens += new_tokens;
        if (rl->tb.tokens > rl->tb.capacity) {
            rl->tb.tokens = rl->tb.capacity;
        }
        rl->tb.last_refill = now;
        break;
    }

    case RL_LEAKY_BUCKET: {
        double now = get_time_seconds();
        double elapsed = now - rl->lb.last_drain;
        double drained = elapsed * rl->lb.drain_rate;
        rl->lb.water_level -= drained;
        if (rl->lb.water_level < 0.0) rl->lb.water_level = 0.0;
        rl->lb.last_drain = now;
        break;
    }

    case RL_FIXED_WINDOW: {
        uint64_t now = get_time_ms();
        if (now - rl->fw.window_start_ms >= rl->fw.window_size_ms) {
            rl->fw.window_start_ms = now;
            rl->fw.current_count = 0;
        }
        break;
    }

    case RL_SLIDING_WINDOW_LOG: {
        uint64_t now = get_time_ms();
        uint64_t window_start = now - rl->sw.window_size_ms;
        while (rl->sw.timestamp_count > 0 &&
               rl->sw.timestamps[rl->sw.head] < window_start) {
            rl->sw.head = (rl->sw.head + 1) % RL_MAX_TIMESTAMPS;
            rl->sw.timestamp_count--;
        }
        break;
    }
    }

    /* also refill per-client limiters */
    for (int i = 0; i < rl->num_clients; i++) {
        ClientLimiter *cl = &rl->clients[i];
        if (cl->algorithm == RL_TOKEN_BUCKET) {
            double now = get_time_seconds();
            double elapsed = now - cl->tb.last_refill;
            double new_tokens = elapsed * cl->tb.refill_rate;
            cl->tb.tokens += new_tokens;
            if (cl->tb.tokens > cl->tb.capacity) {
                cl->tb.tokens = cl->tb.capacity;
            }
            cl->tb.last_refill = now;
        } else if (cl->algorithm == RL_FIXED_WINDOW) {
            uint64_t now = get_time_ms();
            if (now - cl->fw.window_start_ms >= cl->fw.window_size_ms) {
                cl->fw.window_start_ms = now;
                cl->fw.current_count = 0;
            }
        }
    }
}

int rl_consume(RateLimiter *rl, RLConsumeFunc func)
{
    if (!rl) return -1;
    if (rl_allow(rl)) {
        return func ? func() : 0;
    }
    return -1;
}

void rl_print_state(const RateLimiter *rl)
{
    if (!rl) return;

    const char *algo_names[] = {
        "TOKEN_BUCKET", "LEAKY_BUCKET", "FIXED_WINDOW", "SLIDING_WINDOW_LOG"
    };

    printf("=== Rate Limiter ===\n");
    printf("Algorithm: %s\n",
           rl->algorithm < 4 ? algo_names[rl->algorithm] : "UNKNOWN");
    printf("Rate: %.1f, Burst: %.1f\n", rl->rate, rl->burst);

    switch (rl->algorithm) {
    case RL_TOKEN_BUCKET:
        printf("Tokens: %.2f / %.2f\n", rl->tb.tokens, rl->tb.capacity);
        printf("Refill rate: %.2f/s\n", rl->tb.refill_rate);
        break;
    case RL_LEAKY_BUCKET:
        printf("Water level: %.2f / %.2f\n", rl->lb.water_level, rl->lb.capacity);
        printf("Drain rate: %.2f/s\n", rl->lb.drain_rate);
        break;
    case RL_FIXED_WINDOW:
        printf("Window: %d / %d\n",
               rl->fw.current_count, rl->fw.max_requests);
        break;
    case RL_SLIDING_WINDOW_LOG:
        printf("Window count: %d / %d\n",
               rl->sw.timestamp_count, rl->sw.max_requests);
        break;
    }

    printf("Clients: %d\n", rl->num_clients);
    for (int i = 0; i < rl->num_clients; i++) {
        printf("  %s: %d rps\n",
               rl->clients[i].client_id, rl->clients[i].max_rps);
    }
    printf("====================\n");
}
