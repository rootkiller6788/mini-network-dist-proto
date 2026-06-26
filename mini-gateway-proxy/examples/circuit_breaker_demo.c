#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "circuit_breaker.h"

static int successful_call_count = 0;
static int failed_call_count = 0;
static bool simulate_failures = false;

static int mock_backend_call(void *arg)
{
    (void)arg;

    if (simulate_failures) {
        failed_call_count++;
        printf("  [backend] CALL FAILED (failure #%d)\n", failed_call_count);
        return -1;
    }

    successful_call_count++;
    printf("  [backend] CALL SUCCEEDED (success #%d)\n", successful_call_count);
    return 0;
}

static void print_separator(const char *title)
{
    printf("\n--------------------------------------------------\n");
    printf("  %s\n", title);
    printf("--------------------------------------------------\n");
}

static void demo_basic_cycle(void)
{
    print_separator("Demo 1: Basic OPEN-CLOSED Cycle");

    CBCircuit *cb = cb_init("user-service", 3, 2, 2000);

    printf("\n  Phase 1: CLOSED - Normal operation, all succeed\n");
    for (int i = 0; i < 5; i++) {
        int ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Request %d: %s\n", i + 1, ret == 0 ? "OK" : "FAIL");
    }
    cb_print_state(cb);

    printf("\n  Phase 2: Inject failures -> circuit should OPEN\n");
    simulate_failures = true;
    for (int i = 0; i < 5; i++) {
        int ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Request %d: %s\n", i + 1, ret == 0 ? "OK" : "FAIL");
        if (cb_is_open(cb)) {
            printf("    *** Circuit OPENED after request %d ***\n", i + 1);
            break;
        }
    }
    cb_print_state(cb);

    printf("\n  Phase 3: OPEN - Fast-fail, no backend calls\n");
    simulate_failures = true;
    for (int i = 0; i < 3; i++) {
        int ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Request %d: %s (fast-fail)\n",
               i + 1, ret == 0 ? "OK" : "REJECTED");
    }
    cb_print_state(cb);

    printf("\n  Phase 4: Wait for timeout, then half-open probe\n");
    printf("  Waiting %llums for circuit timeout...\n",
           (unsigned long long)cb->timeout_ms);
    usleep(2500000);

    simulate_failures = false;
    printf("\n  Phase 5: HALF_OPEN - Probing with limited requests\n");
    for (int i = 0; i < 5; i++) {
        int ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Probe %d: %s (state=%s)\n",
               i + 1, ret == 0 ? "OK" : "FAIL",
               cb_state_name(cb->state));
    }
    cb_print_state(cb);

    free(cb);
}

static void demo_half_open_failure(void)
{
    print_separator("Demo 2: Half-Open Fails -> Re-Opens");

    CBCircuit *cb = cb_init("order-service", 2, 3, 1000);

    printf("\n  Phase 1: Trip to OPEN with 2 failures\n");
    simulate_failures = true;
    cb_call(cb, mock_backend_call, NULL);
    cb_call(cb, mock_backend_call, NULL);
    cb_print_state(cb);

    printf("\n  Phase 2: Wait timeout, enter HALF_OPEN\n");
    printf("  Waiting 1.5s...\n");
    usleep(1500000);

    printf("\n  Phase 3: HALF_OPEN probe fails -> back to OPEN\n");
    simulate_failures = true;
    int ret = cb_call(cb, mock_backend_call, NULL);
    printf("    Probe result: %s\n", ret == 0 ? "OK" : "FAIL");
    cb_print_state(cb);

    printf("\n  Phase 4: Still OPEN, fast-fail continues\n");
    for (int i = 0; i < 3; i++) {
        ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Request %d: %s\n", i + 1, ret == 0 ? "OK" : "REJECTED");
    }
    cb_print_state(cb);

    printf("\n  Phase 5: Wait timeout again, HALF_OPEN probe succeeds -> CLOSE\n");
    printf("  Waiting 1.5s...\n");
    usleep(1500000);

    simulate_failures = false;
    for (int i = 0; i < 5; i++) {
        ret = cb_call(cb, mock_backend_call, NULL);
        printf("    Request %d: %s (state=%s)\n",
               i + 1, ret == 0 ? "OK" : "FAIL",
               cb_state_name(cb->state));
    }
    cb_print_state(cb);

    free(cb);
}

static void demo_cascading_failure_prevention(void)
{
    print_separator("Demo 3: Cascading Failure Prevention");

    printf("\n  Scenario: 3 dependent services, each with circuit breaker\n");
    printf("  Without CB: slow service causes thread pool exhaustion -> cascade\n");
    printf("  With CB: fast-fail prevents resource drain\n\n");

    CBCircuit *cb_auth = cb_init("auth-svc", 3, 2, 2000);
    CBCircuit *cb_db = cb_init("database", 5, 3, 5000);
    CBCircuit *cb_cache = cb_init("cache-svc", 2, 2, 1500);

    const char *names[] = {"auth-svc", "database", "cache-svc"};

    /* simulate: auth succeeds, database starts failing, cache healthy */
    for (int round = 0; round < 8; round++) {
        printf("  Round %d:\n", round + 1);

        simulate_failures = false;
        int auth_ret = cb_call(cb_auth, mock_backend_call, NULL);

        simulate_failures = (round >= 1);
        int db_ret = cb_call(cb_db, mock_backend_call, NULL);

        simulate_failures = false;
        int cache_ret = cb_call(cb_cache, mock_backend_call, NULL);

        printf("    %-12s: %-8s (state=%s)\n", names[0],
               auth_ret == 0 ? "OK" : "FAIL", cb_state_name(cb_auth->state));
        printf("    %-12s: %-8s (state=%s)\n", names[1],
               db_ret == 0 ? "OK" : "FAIL", cb_state_name(cb_db->state));
        printf("    %-12s: %-8s (state=%s)\n", names[2],
               cache_ret == 0 ? "OK" : "FAIL", cb_state_name(cb_cache->state));
    }

    printf("\n  Result: database tripped (OPEN), auth & cache still CLOSED\n");
    printf("  Cascading failure prevented - only DB calls rejected\n");

    free(cb_auth);
    free(cb_db);
    free(cb_cache);
}

static void demo_reset_and_stats(void)
{
    print_separator("Demo 4: Reset and Statistics");

    CBCircuit *cb = cb_init("api-gateway", 4, 3, 3000);

    simulate_failures = true;
    for (int i = 0; i < 6; i++) {
        cb_call(cb, mock_backend_call, NULL);
    }
    cb_print_state(cb);

    printf("\n  Manual reset:\n");
    cb_reset(cb);
    cb_print_state(cb);

    simulate_failures = false;
    for (int i = 0; i < 10; i++) {
        cb_call(cb, mock_backend_call, NULL);
    }
    cb_print_state(cb);
    printf("  Circuit stayed CLOSED: all 10 requests succeeded after reset\n");

    free(cb);
}

int main(void)
{
    printf("============================================\n");
    printf("  Circuit Breaker Pattern Demo\n");
    printf("============================================\n");
    printf("Simulating backend service calls with fault injection\n");

    simulate_failures = false;
    successful_call_count = 0;
    failed_call_count = 0;

    demo_basic_cycle();
    demo_half_open_failure();
    demo_cascading_failure_prevention();
    demo_reset_and_stats();

    printf("\n============================================\n");
    printf("  Demo Complete - Total: %d success, %d failures\n",
           successful_call_count, failed_call_count);
    printf("============================================\n");

    return 0;
}
