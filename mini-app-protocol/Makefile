CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=$(OUTDIR)/%.o)

EXAMPLES_SRC = $(wildcard examples/*.c)
EXAMPLES_BIN = $(EXAMPLES_SRC:examples/%.c=$(OUTDIR)/%)

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:tests/%.c=$(OUTDIR)/%)

.PHONY: all clean dirs test \
        http2_demo ws_demo mqtt_demo sse_demo \
        http2_frames grpc_proto websocket mqtt rest_api sse

all: dirs $(EXAMPLES_BIN)

dirs:
	@mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/http2_demo: examples/http2_demo.c $(OUTDIR)/http2_frames.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/http2_frames.o -o $@ $(LDFLAGS)

$(OUTDIR)/websocket_demo: examples/websocket_demo.c $(OUTDIR)/websocket.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/websocket.o -o $@ $(LDFLAGS)

$(OUTDIR)/mqtt_demo: examples/mqtt_demo.c $(OUTDIR)/mqtt.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/mqtt.o -o $@ $(LDFLAGS)

$(OUTDIR)/sse_demo: examples/sse_demo.c $(OUTDIR)/sse.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/sse.o -o $@ $(LDFLAGS)

$(OUTDIR)/test_%: tests/test_%.c $(OBJ)
	$(CC) $(CFLAGS) $< $(OBJ) -o $@ $(LDFLAGS)

test: dirs $(OBJ) $(TEST_BINS)
	@echo "=== Running Tests ==="
	@PASSED=0; FAILED=0; \
	for t in $(TEST_BINS); do \
		echo ""; \
		if $$t 2>&1; then \
			PASSED=$$((PASSED+1)); \
		else \
			FAILED=$$((FAILED+1)); \
			echo "  *** FAILED ***"; \
		fi; \
	done; \
	echo ""; \
	echo "=== Test Suites: $$((PASSED+FAILED)) total, $$PASSED passed, $$FAILED failed ==="; \
	[ $$FAILED -eq 0 ]

http2_demo: dirs $(OUTDIR)/http2_frames.o $(OUTDIR)/http2_demo

ws_demo: dirs $(OUTDIR)/websocket.o $(OUTDIR)/websocket_demo

mqtt_demo: dirs $(OUTDIR)/mqtt.o $(OUTDIR)/mqtt_demo

sse_demo: dirs $(OUTDIR)/sse.o $(OUTDIR)/sse_demo

http2_frames: dirs $(OUTDIR)/http2_frames.o

grpc_proto: dirs $(OUTDIR)/grpc_proto.o

websocket: dirs $(OUTDIR)/websocket.o

mqtt: dirs $(OUTDIR)/mqtt.o

rest_api: dirs $(OUTDIR)/rest_api.o

sse: dirs $(OUTDIR)/sse.o

clean:
	@rm -rf "$(OUTDIR)"
