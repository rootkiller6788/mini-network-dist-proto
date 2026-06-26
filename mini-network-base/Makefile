CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin

SRCS     = src/socket_tcp.c src/udp_dns.c src/ip_packet.c \
           src/tls_handshake.c src/http_basic.c \
           src/tcp_congestion.c src/ip_routing.c src/icmp_proto.c
OBJS     = $(SRCS:src/%.c=$(OUTDIR)/%.o)

EXAMPLES = examples/tcp_demo.c examples/dns_demo.c examples/tls_demo.c
TARGETS  = $(EXAMPLES:examples/%.c=$(OUTDIR)/%.exe) $(OUTDIR)/test_runner.exe

.PHONY: all clean test run-tcp-demo run-dns-demo run-tls-demo

all: $(TARGETS)

$(OUTDIR):
	@mkdir -p "$(OUTDIR)"

# Ensure bin/ exists before any object or exe targets
$(OBJS): | $(OUTDIR)
$(TARGETS): | $(OUTDIR)

$(OUTDIR)/socket_tcp.o: src/socket_tcp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/udp_dns.o: src/udp_dns.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/ip_packet.o: src/ip_packet.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/tls_handshake.o: src/tls_handshake.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/http_basic.o: src/http_basic.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/tcp_congestion.o: src/tcp_congestion.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/ip_routing.o: src/ip_routing.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/icmp_proto.o: src/icmp_proto.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/tcp_demo.exe: examples/tcp_demo.c $(OUTDIR)/socket_tcp.o $(OUTDIR)/http_basic.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/socket_tcp.o $(OUTDIR)/http_basic.o -o $@ $(LDFLAGS)

$(OUTDIR)/dns_demo.exe: examples/dns_demo.c $(OUTDIR)/udp_dns.o $(OUTDIR)/ip_packet.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/udp_dns.o $(OUTDIR)/ip_packet.o -o $@ $(LDFLAGS)

$(OUTDIR)/tls_demo.exe: examples/tls_demo.c $(OUTDIR)/tls_handshake.o
	$(CC) $(CFLAGS) $< $(OUTDIR)/tls_handshake.o -o $@ $(LDFLAGS)

$(OUTDIR)/test_runner.exe: tests/test_main.c $(OBJS)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@ $(LDFLAGS)

test: $(OUTDIR)/test_runner.exe
	@echo Running tests...
	./$(OUTDIR)/test_runner.exe

run-tcp-demo: $(OUTDIR)/tcp_demo.exe
	$(OUTDIR)/tcp_demo.exe

run-dns-demo: $(OUTDIR)/dns_demo.exe
	$(OUTDIR)/dns_demo.exe

run-tls-demo: $(OUTDIR)/tls_demo.exe
	$(OUTDIR)/tls_demo.exe

clean:
	@rm -rf "$(OUTDIR)"

print-src:
	@echo Source files: $(SRCS)
	@echo Object files: $(OBJS)
	@echo Demo targets: $(TARGETS)
