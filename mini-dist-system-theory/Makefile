CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRCDIR = src
EXDIR = examples

SRCS = $(SRCDIR)/cap_theorem.c $(SRCDIR)/time_ordering.c $(SRCDIR)/flp_impossibility.c $(SRCDIR)/crdt.c $(SRCDIR)/byzantine.c

OBJS = $(SRCS:.c=.o)

DEMOS = $(OUTDIR)/cap_demo $(OUTDIR)/vector_clock_demo $(OUTDIR)/crdt_demo

.PHONY: all clean dirs

all: dirs $(DEMOS)

dirs:
	mkdir -p $(OUTDIR)

$(OUTDIR)/cap_demo: $(EXDIR)/cap_demo.c $(SRCDIR)/cap_theorem.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/cap_demo.c $(SRCDIR)/cap_theorem.c $(LDFLAGS)

$(OUTDIR)/vector_clock_demo: $(EXDIR)/vector_clock_demo.c $(SRCDIR)/time_ordering.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/vector_clock_demo.c $(SRCDIR)/time_ordering.c $(LDFLAGS)

$(OUTDIR)/crdt_demo: $(EXDIR)/crdt_demo.c $(SRCDIR)/crdt.c
	$(CC) $(CFLAGS) -o $@ $(EXDIR)/crdt_demo.c $(SRCDIR)/crdt.c $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUTDIR)
	rm -f $(SRCDIR)/*.o $(EXDIR)/*.o
