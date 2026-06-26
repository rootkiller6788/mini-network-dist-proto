#include "saga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[32];
    bool booked;
    bool cancelled;
} BookingContext;

static bool book_hotel(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Hotel] Booking hotel room for '%s'... SUCCESS\n", bc->name);
    bc->booked = true;
    return true;
}

static bool cancel_hotel(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Hotel] Cancelling hotel booking for '%s'... DONE\n", bc->name);
    bc->cancelled = true;
    bc->booked = false;
    return true;
}

static bool book_flight(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Flight] Booking flight for '%s'... SUCCESS\n", bc->name);
    bc->booked = true;
    return true;
}

static bool cancel_flight(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Flight] Cancelling flight booking for '%s'... DONE\n", bc->name);
    bc->cancelled = true;
    bc->booked = false;
    return true;
}

static bool charge_payment_success(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Payment] Charging payment for '%s'... SUCCESS\n", bc->name);
    return true;
}

static bool charge_payment_fail(void *ctx) {
    (void)ctx;
    printf("    [Payment] Charging payment... FAILED (insufficient funds)\n");
    return false;
}

static bool refund_payment(void *ctx) {
    BookingContext *bc = (BookingContext *)ctx;
    printf("    [Payment] Refunding payment for '%s'... DONE\n", bc->name);
    return true;
}

int main(void) {
    printf("========================================\n");
    printf("  Saga Demo — All Steps Succeed\n");
    printf("========================================\n\n");

    BookingContext ctx_success = {"Alice", false, false};

    SagaTransaction txn1;
    saga_transaction_init(&txn1, 5001);

    SagaStep s1, s2, s3;
    saga_step_init(&s1, 1, "BookHotel",  book_hotel,           cancel_hotel);
    saga_step_init(&s2, 2, "BookFlight", book_flight,          cancel_flight);
    saga_step_init(&s3, 3, "ChargePay",  charge_payment_success, refund_payment);

    saga_transaction_add_step(&txn1, &s1);
    saga_transaction_add_step(&txn1, &s2);
    saga_transaction_add_step(&txn1, &s3);

    void *ctxs1[] = {&ctx_success, &ctx_success, &ctx_success};
    bool result = saga_execute(&txn1, ctxs1);

    printf("\n[Result] Transaction %d: %s\n", txn1.txn_id,
           result ? "COMPLETED" : "COMPENSATED");
    saga_print_log(&txn1);

    printf("\n========================================\n");
    printf("  Saga Demo — Payment Fails → Compensate\n");
    printf("========================================\n\n");

    BookingContext ctx_fail = {"Bob", false, false};

    SagaTransaction txn2;
    saga_transaction_init(&txn2, 5002);

    SagaStep s4, s5, s6;
    saga_step_init(&s4, 4, "BookHotel",  book_hotel,          cancel_hotel);
    saga_step_init(&s5, 5, "BookFlight", book_flight,         cancel_flight);
    saga_step_init(&s6, 6, "ChargePay",  charge_payment_fail, refund_payment);

    saga_transaction_add_step(&txn2, &s4);
    saga_transaction_add_step(&txn2, &s5);
    saga_transaction_add_step(&txn2, &s6);

    void *ctxs2[] = {&ctx_fail, &ctx_fail, &ctx_fail};
    result = saga_execute(&txn2, ctxs2);

    printf("\n[Result] Transaction %d: %s\n", txn2.txn_id,
           result ? "COMPLETED" : "COMPENSATED");
    printf("[State] Hotel booked=%s cancelled=%s\n",
           ctx_fail.booked ? "yes" : "no",
           ctx_fail.cancelled ? "yes" : "no");
    saga_print_log(&txn2);

    printf("\n========================================\n");
    printf("  Saga Demo — Retry After Compensation\n");
    printf("========================================\n\n");

    printf("Retrying transaction %d (now payment works)...\n", txn2.txn_id);

    SagaStep s4_r, s5_r, s6_r;
    saga_step_init(&s4_r, 4, "BookHotel",  book_hotel,          cancel_hotel);
    saga_step_init(&s5_r, 5, "BookFlight", book_flight,         cancel_flight);
    saga_step_init(&s6_r, 6, "ChargePay",  charge_payment_success, refund_payment);

    txn2.steps[0] = s4_r;
    txn2.steps[1] = s5_r;
    txn2.steps[2] = s6_r;
    txn2.status = SAGA_TXN_COMPENSATED;

    void *ctxs3[] = {&ctx_fail, &ctx_fail, &ctx_fail};
    result = saga_retry(&txn2, ctxs3);

    printf("\n[Result] Retry: %s\n", result ? "COMPLETED" : "FAILED");
    saga_print_log(&txn2);

    printf("\n=== Saga vs 2PC Comparison ===\n");
    printf("  Saga: Long-lived transactions, eventual consistency, "
           "compensating actions\n");
    printf("  2PC: Short-lived, strong consistency, blocking protocol\n");
    printf("  Saga use: Microservices, long-running business processes\n");

    return 0;
}
