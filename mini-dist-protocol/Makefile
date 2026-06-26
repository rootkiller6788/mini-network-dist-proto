CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRCS = src/raft.c src/paxos.c src/gossip.c src/swim.c src/leader_election.c
OBJS = $(OUTDIR)/raft.o $(OUTDIR)/paxos.o $(OUTDIR)/gossip.o $(OUTDIR)/swim.o $(OUTDIR)/leader_election.o

EXAMPLES = $(OUTDIR)/raft_demo.exe $(OUTDIR)/gossip_demo.exe $(OUTDIR)/leader_election_demo.exe

.PHONY: all clean raft gossip leader run-raft run-gossip run-leader

all: $(EXAMPLES)

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

$(OUTDIR)/raft_demo.exe: examples/raft_demo.c $(OUTDIR)/raft.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/raft_demo.c $(OUTDIR)/raft.o $(LDFLAGS) -o $(OUTDIR)/raft_demo.exe

$(OUTDIR)/gossip_demo.exe: examples/gossip_demo.c $(OUTDIR)/gossip.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/gossip_demo.c $(OUTDIR)/gossip.o $(LDFLAGS) -o $(OUTDIR)/gossip_demo.exe

$(OUTDIR)/leader_election_demo.exe: examples/leader_election_demo.c $(OUTDIR)/leader_election.o | $(OUTDIR)
	$(CC) $(CFLAGS) examples/leader_election_demo.c $(OUTDIR)/leader_election.o $(LDFLAGS) -o $(OUTDIR)/leader_election_demo.exe

raft: $(OUTDIR)/raft_demo.exe

gossip: $(OUTDIR)/gossip_demo.exe

leader: $(OUTDIR)/leader_election_demo.exe

run-raft: $(OUTDIR)/raft_demo.exe
	./$(OUTDIR)/raft_demo.exe

run-gossip: $(OUTDIR)/gossip_demo.exe
	./$(OUTDIR)/gossip_demo.exe

run-leader: $(OUTDIR)/leader_election_demo.exe
	./$(OUTDIR)/leader_election_demo.exe

clean:
	rm -rf "$(OUTDIR)"
