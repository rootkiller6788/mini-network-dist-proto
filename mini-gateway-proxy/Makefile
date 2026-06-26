CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin

SRC_REVERSE       = src/reverse_proxy.c
SRC_LB            = src/load_balancer.c
SRC_CB            = src/circuit_breaker.c
SRC_GW            = src/api_gateway.c
SRC_RL            = src/rate_limiter.c

SRC_EX_LB         = examples/load_balance_demo.c
SRC_EX_CB         = examples/circuit_breaker_demo.c
SRC_EX_GW         = examples/api_gateway_demo.c

OBJ_REVERSE       = $(OUTDIR)/reverse_proxy.o
OBJ_LB            = $(OUTDIR)/load_balancer.o
OBJ_CB            = $(OUTDIR)/circuit_breaker.o
OBJ_GW            = $(OUTDIR)/api_gateway.o
OBJ_RL            = $(OUTDIR)/rate_limiter.o

EXE_LB            = $(OUTDIR)/load_balance_demo
EXE_CB            = $(OUTDIR)/circuit_breaker_demo
EXE_GW            = $(OUTDIR)/api_gateway_demo

.PHONY: all clean run-load-balance run-circuit-breaker run-api-gateway

all: $(EXE_LB) $(EXE_CB) $(EXE_GW)

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

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

$(EXE_LB): $(SRC_EX_LB) $(OBJ_LB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_LB) $(LDFLAGS) -o $@

$(EXE_CB): $(SRC_EX_CB) $(OBJ_CB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_CB) $(LDFLAGS) -o $@

$(EXE_GW): $(SRC_EX_GW) $(OBJ_GW) $(OBJ_RL) $(OBJ_CB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJ_GW) $(OBJ_RL) $(OBJ_CB) $(LDFLAGS) -o $@

run-load-balance: $(EXE_LB)
	$(EXE_LB)

run-circuit-breaker: $(EXE_CB)
	$(EXE_CB)

run-api-gateway: $(EXE_GW)
	$(EXE_GW)

clean:
	rm -rf "$(OUTDIR)"
