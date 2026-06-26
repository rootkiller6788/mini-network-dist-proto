#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define RL_MAX_TIMESTAMPS   1024
#define RL_MAX_TOKENS       10000
#define RL_MAX_CLIENTS      256

typedef enum {
    RL_TOKEN_BUCKET,
    RL_LEAKY_BUCKET,
    RL_FIXED_WINDOW,
    RL_SLIDING_WINDOW_LOG
} RLAlgorithm;

typedef struct {
    double    tokens;
    double    capacity;
    double    refill_rate;
    double    last_refill;
} TokenBucket;

typedef struct {
    double    capacity;
    double    water_level;
    double    drain_rate;
    double    last_drain;
} LeakyBucket;

typedef struct {
    uint64_t  window_size_ms;
    int       max_requests;
    int       current_count;
    uint64_t  window_start_ms;
} FixedWindow;

typedef struct {
    uint64_t  window_size_ms;
    int       max_requests;
    uint64_t  timestamps[RL_MAX_TIMESTAMPS];
    int       timestamp_count;
    int       head;
    int       tail;
} SlidingWindowLog;

typedef struct {
    char            client_id[64];
    RLAlgorithm     algorithm;
    TokenBucket     tb;
    LeakyBucket     lb;
    FixedWindow     fw;
    SlidingWindowLog sw;
    int             max_rps;
} ClientLimiter;

typedef struct {
    RLAlgorithm     algorithm;
    double          rate;          
    double          burst;         
    TokenBucket     tb;
    LeakyBucket     lb;
    FixedWindow     fw;
    SlidingWindowLog sw;
    ClientLimiter   clients[RL_MAX_CLIENTS];
    int             num_clients;
} RateLimiter;

typedef int (*RLConsumeFunc)(void);

RateLimiter*    rl_init(RLAlgorithm algo, double rate, double burst,
                        uint64_t window_ms);
bool            rl_allow(RateLimiter *rl);
bool            rl_allow_client(RateLimiter *rl, const char *client_id);
void            rl_refill(RateLimiter *rl);
void            rl_print_state(const RateLimiter *rl);
int             rl_consume(RateLimiter *rl, RLConsumeFunc func);
ClientLimiter*  rl_get_or_create_client(RateLimiter *rl, const char *client_id);

#endif
