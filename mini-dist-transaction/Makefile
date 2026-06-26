CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRC = src/two_pc.c src/saga.c src/tcc.c src/dist_lock.c src/idempotency.c \
      src/mvcc.c src/wal.c src/occ.c src/xa.c src/fencing.c
OBJ = $(SRC:.c=.o)
DEMO_NAMES = two_pc_demo saga_demo dist_lock_demo
DEMOS = $(addprefix $(OUTDIR)/, $(DEMO_NAMES))

.PHONY: all test clean

all: $(OUTDIR) $(DEMOS)

test: $(OUTDIR) $(OUTDIR)/test_all
	$(OUTDIR)/test_all

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/test_all: tests/test_all.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ tests/test_all.c $(OBJ) $(LDFLAGS)

$(OUTDIR)/two_pc_demo: examples/two_pc_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/two_pc_demo.c $(OBJ) $(LDFLAGS)

$(OUTDIR)/saga_demo: examples/saga_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/saga_demo.c $(OBJ) $(LDFLAGS)

$(OUTDIR)/dist_lock_demo: examples/dist_lock_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/dist_lock_demo.c $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OUTDIR) src/*.o 2>/dev/null; true
