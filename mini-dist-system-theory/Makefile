CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRCDIR = src
EXDIR = examples
TESTDIR = tests

SRCS = $(SRCDIR)/cap_theorem.c \
       $(SRCDIR)/time_ordering.c \
       $(SRCDIR)/flp_impossibility.c \
       $(SRCDIR)/crdt.c \
       $(SRCDIR)/byzantine.c \
       $(SRCDIR)/raft.c \
       $(SRCDIR)/gossip.c \
       $(SRCDIR)/snapshot.c \
       $(SRCDIR)/dht.c \
       $(SRCDIR)/leader_election.c

DEMOS = $(OUTDIR)/cap_demo \
        $(OUTDIR)/vector_clock_demo \
        $(OUTDIR)/crdt_demo \
        $(OUTDIR)/raft_demo \
        $(OUTDIR)/gossip_demo \
        $(OUTDIR)/snapshot_demo \
        $(OUTDIR)/dht_demo \
        $(OUTDIR)/leader_election_demo

TEST_BIN = $(OUTDIR)/test_all

.PHONY: all clean test dirs

all: dirs $(DEMOS)

test: dirs $(TEST_BIN)
	@echo "=== Running Tests ==="
	$(TEST_BIN)

dirs:
	mkdir -p $(OUTDIR)

$(OUTDIR)/cap_demo: $(EXDIR)/cap_demo.c $(SRCDIR)/cap_theorem.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/cap_demo.c $(SRCDIR)/cap_theorem.c $(LDFLAGS)

$(OUTDIR)/vector_clock_demo: $(EXDIR)/vector_clock_demo.c $(SRCDIR)/time_ordering.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/vector_clock_demo.c $(SRCDIR)/time_ordering.c $(LDFLAGS)

$(OUTDIR)/crdt_demo: $(EXDIR)/crdt_demo.c $(SRCDIR)/crdt.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/crdt_demo.c $(SRCDIR)/crdt.c $(LDFLAGS)

$(OUTDIR)/raft_demo: $(EXDIR)/raft_demo.c $(SRCDIR)/raft.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/raft_demo.c $(SRCDIR)/raft.c $(LDFLAGS)

$(OUTDIR)/gossip_demo: $(EXDIR)/gossip_demo.c $(SRCDIR)/gossip.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/gossip_demo.c $(SRCDIR)/gossip.c $(LDFLAGS)

$(OUTDIR)/snapshot_demo: $(EXDIR)/snapshot_demo.c $(SRCDIR)/snapshot.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/snapshot_demo.c $(SRCDIR)/snapshot.c $(LDFLAGS)

$(OUTDIR)/dht_demo: $(EXDIR)/dht_demo.c $(SRCDIR)/dht.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/dht_demo.c $(SRCDIR)/dht.c $(LDFLAGS)

$(OUTDIR)/leader_election_demo: $(EXDIR)/leader_election_demo.c $(SRCDIR)/leader_election.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/leader_election_demo.c $(SRCDIR)/leader_election.c $(LDFLAGS)

$(TEST_BIN): $(TESTDIR)/test_all.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(TESTDIR)/test_all.c $(SRCS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUTDIR)
	rm -f $(SRCDIR)/*.o $(TESTDIR)/*.o $(EXDIR)/*.o
