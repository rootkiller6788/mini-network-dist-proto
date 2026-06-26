CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR = bin

SRC = src/two_pc.c src/saga.c src/tcc.c src/dist_lock.c src/idempotency.c
OBJ = $(SRC:.c=.o)
DEMO_NAMES = two_pc_demo saga_demo dist_lock_demo
DEMOS = $(addprefix $(OUTDIR)/, $(DEMO_NAMES))

.PHONY: all clean

all: $(OUTDIR) $(DEMOS)

$(OUTDIR):
	if not exist "$(subst /,\,$(OUTDIR))" mkdir "$(subst /,\,$(OUTDIR))"

$(OUTDIR)/two_pc_demo: examples/two_pc_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/two_pc_demo.c $(OBJ) $(LDFLAGS)

$(OUTDIR)/saga_demo: examples/saga_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/saga_demo.c $(OBJ) $(LDFLAGS)

$(OUTDIR)/dist_lock_demo: examples/dist_lock_demo.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ examples/dist_lock_demo.c $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	if exist "$(subst /,\,$(OUTDIR))" rmdir /s /q "$(subst /,\,$(OUTDIR))"
	del /q src\*.o 2>nul
