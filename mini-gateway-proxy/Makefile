CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin

# Core source files
SRC_REVERSE    = src/reverse_proxy.c
SRC_LB         = src/load_balancer.c
SRC_CB         = src/circuit_breaker.c
SRC_GW         = src/api_gateway.c
SRC_RL         = src/rate_limiter.c
SRC_HTTP       = src/http_message.c
SRC_SR         = src/service_discovery.c
SRC_MW         = src/middleware_chain.c
SRC_TLS        = src/tls_termination.c
SRC_CP         = src/connection_pool.c

# Example source files
SRC_EX_LB      = examples/load_balance_demo.c
SRC_EX_CB      = examples/circuit_breaker_demo.c
SRC_EX_GW      = examples/api_gateway_demo.c

# Test source file
SRC_TEST       = tests/test_all.c

# Object files
OBJ_REVERSE    = $(OUTDIR)/reverse_proxy.o
OBJ_LB         = $(OUTDIR)/load_balancer.o
OBJ_CB         = $(OUTDIR)/circuit_breaker.o
OBJ_GW         = $(OUTDIR)/api_gateway.o
OBJ_RL         = $(OUTDIR)/rate_limiter.o
OBJ_HTTP       = $(OUTDIR)/http_message.o
OBJ_SR         = $(OUTDIR)/service_discovery.o
OBJ_MW         = $(OUTDIR)/middleware_chain.o
OBJ_TLS        = $(OUTDIR)/tls_termination.o
OBJ_CP         = $(OUTDIR)/connection_pool.o

# Example executables
EXE_LB         = $(OUTDIR)/load_balance_demo
EXE_CB         = $(OUTDIR)/circuit_breaker_demo
EXE_GW         = $(OUTDIR)/api_gateway_demo

# Test executable
EXE_TEST       = $(OUTDIR)/test_all

.PHONY: all clean test run-load-balance run-circuit-breaker run-api-gateway run-test

all: $(EXE_LB) $(EXE_CB) $(EXE_GW)

test: $(EXE_TEST)
	$(EXE_TEST)

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

# Core object compilation rules
$(OBJ_REVERSE): $(SRC_REVERSE) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_LB): $(SRC_LB) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_CB): $(SRC_CB) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_GW): $(SRC_GW) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_RL): $(SRC_RL) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_HTTP): $(SRC_HTTP) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_SR): $(SRC_SR) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_MW): $(SRC_MW) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_TLS): $(SRC_TLS) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_CP): $(SRC_CP) | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Example executable build rules
$(EXE_LB): $(SRC_EX_LB) $(OBJ_LB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_LB) $(LDFLAGS) -o $@

$(EXE_CB): $(SRC_EX_CB) $(OBJ_CB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_CB) $(LDFLAGS) -o $@

$(EXE_GW): $(SRC_EX_GW) $(OBJ_GW) $(OBJ_RL) $(OBJ_CB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_GW) $(OBJ_RL) $(OBJ_CB) $(LDFLAGS) -o $@

# Test executable build rule
$(EXE_TEST): $(SRC_TEST) $(OBJ_HTTP) $(OBJ_LB) $(OBJ_CB) $(OBJ_GW) $(OBJ_RL) $(OBJ_SR) $(OBJ_MW) $(OBJ_TLS) $(OBJ_CP) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_HTTP) $(OBJ_LB) $(OBJ_CB) $(OBJ_GW) $(OBJ_RL) $(OBJ_SR) $(OBJ_MW) $(OBJ_TLS) $(OBJ_CP) $(LDFLAGS) -o $@

# Run targets
run-load-balance: $(EXE_LB)
	$(EXE_LB)

run-circuit-breaker: $(EXE_CB)
	$(EXE_CB)

run-api-gateway: $(EXE_GW)
	$(EXE_GW)

run-test: $(EXE_TEST)
	$(EXE_TEST)

clean:
	rm -rf "$(OUTDIR)"
