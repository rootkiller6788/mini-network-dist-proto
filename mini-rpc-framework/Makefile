CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm -lws2_32
OUTDIR   = bin
SRCDIR   = src
TESTDIR  = tests
EXAMPLEDIR = examples

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c, $(OUTDIR)/%.o, $(SRCS))
LIB      = $(OUTDIR)/libminirpc.a

EXAMPLES = $(patsubst $(EXAMPLEDIR)/%.c, $(OUTDIR)/%, $(wildcard $(EXAMPLEDIR)/*.c))
TESTS    = $(OUTDIR)/test_main

.PHONY: all clean examples test demos check

# Default target
all: $(LIB)

# Create output directory
$(OUTDIR):
	mkdir -p $(OUTDIR)

# Compile each source file
$(OUTDIR)/%.o: $(SRCDIR)/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build static library
$(LIB): $(OBJS)
	ar rcs $@ $^

# Build and run tests
test: $(TESTS)
	$(OUTDIR)/test_main

# Compile test binary (linked against object files directly, not static lib)
$(OUTDIR)/test_main: $(TESTDIR)/test_main.c $(OBJS) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@ $(LDFLAGS)

# Build all examples
examples: $(EXAMPLES)

# Build individual examples
$(OUTDIR)/rpc_json_demo: $(EXAMPLEDIR)/rpc_json_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_registry_demo: $(EXAMPLEDIR)/rpc_registry_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_interceptor_demo: $(EXAMPLEDIR)/rpc_interceptor_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_protocol_demo: $(EXAMPLEDIR)/rpc_protocol_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_server_demo: $(EXAMPLEDIR)/rpc_server_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

# Quick check: compile + run tests
check: all test

# Build demos (same as examples)
demos: examples

# Run individual demos
run_json_demo: $(OUTDIR)/rpc_json_demo
	$(OUTDIR)/rpc_json_demo

run_registry_demo: $(OUTDIR)/rpc_registry_demo
	$(OUTDIR)/rpc_registry_demo

run_interceptor_demo: $(OUTDIR)/rpc_interceptor_demo
	$(OUTDIR)/rpc_interceptor_demo

run_protocol_demo: $(OUTDIR)/rpc_protocol_demo
	$(OUTDIR)/rpc_protocol_demo

run_server_demo: $(OUTDIR)/rpc_server_demo
	$(OUTDIR)/rpc_server_demo

# Clean build artifacts
clean:
	rm -rf $(OUTDIR)
