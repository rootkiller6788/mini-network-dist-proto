CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
OUTDIR  = bin

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=$(OUTDIR)/%.o)

EXAMPLES_SRC = $(wildcard examples/*.c)
EXAMPLES_BIN = $(EXAMPLES_SRC:examples/%.c=$(OUTDIR)/%)

.PHONY: all clean dirs \
        http2_demo ws_demo mqtt_demo \
        http2_frames grpc_proto websocket mqtt rest_api

all: dirs $(EXAMPLES_BIN)

dirs:
	@mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/http2_demo: examples/http2_demo.c $(OUTDIR)/http2_frames.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/http2_frames.o -o $@ $(LDFLAGS)

$(OUTDIR)/ws_demo: examples/websocket_demo.c $(OUTDIR)/websocket.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/websocket.o -o $@ $(LDFLAGS)

$(OUTDIR)/mqtt_demo: examples/mqtt_demo.c $(OUTDIR)/mqtt.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/mqtt.o -o $@ $(LDFLAGS)

http2_demo: dirs $(OUTDIR)/http2_frames.o $(OUTDIR)/http2_demo

ws_demo: dirs $(OUTDIR)/websocket.o $(OUTDIR)/ws_demo

mqtt_demo: dirs $(OUTDIR)/mqtt.o $(OUTDIR)/mqtt_demo

http2_frames: dirs $(OUTDIR)/http2_frames.o

grpc_proto: dirs $(OUTDIR)/grpc_proto.o

websocket: dirs $(OUTDIR)/websocket.o

mqtt: dirs $(OUTDIR)/mqtt.o

rest_api: dirs $(OUTDIR)/rest_api.o

clean:
	@rm -rf "$(OUTDIR)"
