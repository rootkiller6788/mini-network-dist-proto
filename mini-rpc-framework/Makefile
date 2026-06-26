CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin
SRCDIR   = src
EXAMPLEDIR = examples

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c, $(OUTDIR)/%.o, $(SRCS))
LIB      = $(OUTDIR)/libminirpc.a

EXAMPLES = $(patsubst $(EXAMPLEDIR)/%.c, $(OUTDIR)/%, $(wildcard $(EXAMPLEDIR)/*.c))

.PHONY: all clean examples demos run_json_demo run_registry_demo run_interceptor_demo

all: $(LIB)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: $(SRCDIR)/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	ar rcs $@ $^

examples: $(EXAMPLES)

$(OUTDIR)/rpc_json_demo: $(EXAMPLEDIR)/rpc_json_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_registry_demo: $(EXAMPLEDIR)/rpc_registry_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

$(OUTDIR)/rpc_interceptor_demo: $(EXAMPLEDIR)/rpc_interceptor_demo.c $(LIB) | $(OUTDIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(OUTDIR) -lminirpc $(LDFLAGS)

demos: examples

run_json_demo: $(OUTDIR)/rpc_json_demo
	$(OUTDIR)/rpc_json_demo

run_registry_demo: $(OUTDIR)/rpc_registry_demo
	$(OUTDIR)/rpc_registry_demo

run_interceptor_demo: $(OUTDIR)/rpc_interceptor_demo
	$(OUTDIR)/rpc_interceptor_demo

clean:
	rm -rf $(OUTDIR)
