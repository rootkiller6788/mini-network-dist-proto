CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRCS = src/raft.c src/paxos.c src/gossip.c src/swim.c src/leader_election.c \
       src/consensus_theorems.c src/distributed_apps.c src/advanced_topics.c

OBJS = $(OUTDIR)/raft.o $(OUTDIR)/paxos.o $(OUTDIR)/gossip.o \
       $(OUTDIR)/swim.o $(OUTDIR)/leader_election.o \
       $(OUTDIR)/consensus_theorems.o $(OUTDIR)/distributed_apps.o \
       $(OUTDIR)/advanced_topics.o

EXAMPLES = $(OUTDIR)/raft_demo.exe $(OUTDIR)/gossip_demo.exe \
           $(OUTDIR)/leader_election_demo.exe $(OUTDIR)/paxos_demo.exe \
           $(OUTDIR)/swim_demo.exe

TESTS = $(OUTDIR)/test_raft.exe $(OUTDIR)/test_paxos.exe \
        $(OUTDIR)/test_gossip.exe $(OUTDIR)/test_swim.exe \
        $(OUTDIR)/test_leader_election.exe

.PHONY: all clean raft gossip leader paxos swim \
        run-raft run-gossip run-leader run-paxos run-swim \
        test test-raft test-paxos test-gossip test-swim test-leader

all: $(EXAMPLES)

test: $(TESTS)
	@echo "=== Running All Tests ==="
	@for t in $(TESTS); do echo ""; echo "--- $$t ---"; ./$$t || exit 1; done
	@echo ""
	@echo "=== All Tests Passed ==="

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

$(OUTDIR)/raft.o: src/raft.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/raft.c -o $(OUTDIR)/raft.o

$(OUTDIR)/paxos.o: src/paxos.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/paxos.c -o $(OUTDIR)/paxos.o

$(OUTDIR)/gossip.o: src/gossip.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/gossip.c -o $(OUTDIR)/gossip.o

$(OUTDIR)/swim.o: src/swim.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/swim.c -o $(OUTDIR)/swim.o

$(OUTDIR)/leader_election.o: src/leader_election.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/leader_election.c -o $(OUTDIR)/leader_election.o

$(OUTDIR)/consensus_theorems.o: src/consensus_theorems.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/consensus_theorems.c -o $(OUTDIR)/consensus_theorems.o

$(OUTDIR)/distributed_apps.o: src/distributed_apps.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/distributed_apps.c -o $(OUTDIR)/distributed_apps.o

$(OUTDIR)/advanced_topics.o: src/advanced_topics.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c src/advanced_topics.c -o $(OUTDIR)/advanced_topics.o

$(OUTDIR)/raft_demo.exe: examples/raft_demo.c $(OUTDIR)/raft.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/raft_demo.c $(OUTDIR)/raft.o $(LDFLAGS) -o $(OUTDIR)/raft_demo.exe

$(OUTDIR)/gossip_demo.exe: examples/gossip_demo.c $(OUTDIR)/gossip.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/gossip_demo.c $(OUTDIR)/gossip.o $(LDFLAGS) -o $(OUTDIR)/gossip_demo.exe

$(OUTDIR)/leader_election_demo.exe: examples/leader_election_demo.c $(OUTDIR)/leader_election.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/leader_election_demo.c $(OUTDIR)/leader_election.o $(LDFLAGS) -o $(OUTDIR)/leader_election_demo.exe

$(OUTDIR)/paxos_demo.exe: examples/paxos_demo.c $(OUTDIR)/paxos.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/paxos_demo.c $(OUTDIR)/paxos.o $(LDFLAGS) -o $(OUTDIR)/paxos_demo.exe

$(OUTDIR)/swim_demo.exe: examples/swim_demo.c $(OUTDIR)/swim.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/swim_demo.c $(OUTDIR)/swim.o $(LDFLAGS) -o $(OUTDIR)/swim_demo.exe

$(OUTDIR)/test_raft.exe: tests/test_raft.c $(OUTDIR)/raft.o | $(OUTDIR)
	$(CC) $(CFLAGS) tests/test_raft.c $(OUTDIR)/raft.o $(LDFLAGS) -o $(OUTDIR)/test_raft.exe

$(OUTDIR)/test_paxos.exe: tests/test_paxos.c $(OUTDIR)/paxos.o | $(OUTDIR)
	$(CC) $(CFLAGS) tests/test_paxos.c $(OUTDIR)/paxos.o $(LDFLAGS) -o $(OUTDIR)/test_paxos.exe

$(OUTDIR)/test_gossip.exe: tests/test_gossip.c $(OUTDIR)/gossip.o | $(OUTDIR)
	$(CC) $(CFLAGS) tests/test_gossip.c $(OUTDIR)/gossip.o $(LDFLAGS) -o $(OUTDIR)/test_gossip.exe

$(OUTDIR)/test_swim.exe: tests/test_swim.c $(OUTDIR)/swim.o | $(OUTDIR)
	$(CC) $(CFLAGS) tests/test_swim.c $(OUTDIR)/swim.o $(LDFLAGS) -o $(OUTDIR)/test_swim.exe

$(OUTDIR)/test_leader_election.exe: tests/test_leader_election.c $(OUTDIR)/leader_election.o | $(OUTDIR)
	$(CC) $(CFLAGS) tests/test_leader_election.c $(OUTDIR)/leader_election.o $(LDFLAGS) -o $(OUTDIR)/test_leader_election.exe

raft: $(OUTDIR)/raft_demo.exe
paxos: $(OUTDIR)/paxos_demo.exe
gossip: $(OUTDIR)/gossip_demo.exe
swim: $(OUTDIR)/swim_demo.exe
leader: $(OUTDIR)/leader_election_demo.exe

test-raft: $(OUTDIR)/test_raft.exe
	./$(OUTDIR)/test_raft.exe

test-paxos: $(OUTDIR)/test_paxos.exe
	./$(OUTDIR)/test_paxos.exe

test-gossip: $(OUTDIR)/test_gossip.exe
	./$(OUTDIR)/test_gossip.exe

test-swim: $(OUTDIR)/test_swim.exe
	./$(OUTDIR)/test_swim.exe

test-leader: $(OUTDIR)/test_leader_election.exe
	./$(OUTDIR)/test_leader_election.exe

run-raft: $(OUTDIR)/raft_demo.exe
	./$(OUTDIR)/raft_demo.exe

run-gossip: $(OUTDIR)/gossip_demo.exe
	./$(OUTDIR)/gossip_demo.exe

run-leader: $(OUTDIR)/leader_election_demo.exe
	./$(OUTDIR)/leader_election_demo.exe

run-paxos: $(OUTDIR)/paxos_demo.exe
	./$(OUTDIR)/paxos_demo.exe

run-swim: $(OUTDIR)/swim_demo.exe
	./$(OUTDIR)/swim_demo.exe

clean:
	rm -rf "$(OUTDIR)"
