# mini-app-protocol — 应用层协议 (C 语言实现)

> 参考 HTTP/2 RFC 9113, gRPC, WebSocket RFC 6455, MQTT 5.0

## Overview

A C99 library implementing five core application-layer networking protocols with
binary serialization, framing, and routing. Each module is self-contained with
its own header and implementation, requiring only libc and libm.

## Modules

### 1. `http2_frames` — HTTP/2 Frame Protocol
Binary framing layer, stream multiplexing, HPACK header compression, flow control.

| Function | Description |
|----------|-------------|
| `h2_frame_build` | Construct a binary frame with 9-byte header |
| `h2_frame_parse` | Parse and validate an incoming frame |
| `h2_settings_exchange` | Negotiate connection parameters with SETTINGS frame |
| `h2_stream_open` | Allocate a new multiplexed stream |
| `h2_header_encode` | Compress headers using static/dynamic HPACK table |
| `h2_flow_control_update` | Signal window increment for a stream |

### 2. `grpc_proto` — gRPC Wire Protocol
Length-prefixed message framing, service descriptors, streaming RPC types.

| Function | Description |
|----------|-------------|
| `grpc_encode_message` | 5-byte header + payload length-prefixed message |
| `grpc_send_request` | Build a gRPC call with service/method routing |
| `grpc_build_service_desc` | Generate textual service descriptor |
| `grpc_kv_serialize` | Simple key-value payload (protobuf simulation) |
| `grpc_server_find_method` | Look up registered RPC methods |

### 3. `websocket` — WebSocket Protocol (RFC 6455)
HTTP upgrade handshake, binary/text frame encoding with masking, ping/pong.

| Function | Description |
|----------|-------------|
| `ws_handshake_build_client` | HTTP GET with Upgrade + Sec-WebSocket-Key |
| `ws_handshake_build_server` | 101 Switching Protocols + Accept key |
| `ws_frame_encode` | Build masked/unmasked WS frame with length encoding |
| `ws_send_text` | Convenience: encode and mask a text frame |
| `ws_sha1_hash` + `ws_base64_encode` | Handshake key computation |

### 4. `mqtt` — MQTT 5.0 Protocol
Packet encoding/decoding, topic matching with wildcards, broker simulation.

| Function | Description |
|----------|-------------|
| `mqtt_encode_connect` | CONNECT packet with clean start, will, keep-alive |
| `mqtt_encode_publish` | PUBLISH with topic, QoS, retain, payload |
| `mqtt_encode_subscribe` | SUBSCRIBE with topic filter list |
| `mqtt_topic_match` | Match a topic against a filter with `+` and `#` |
| `mqtt_broker_handle_publish` | Dispatch message to matching subscribers |

### 5. `rest_api` — RESTful API Framework
URI pattern matching, method dispatch, query string parsing, response helpers.

| Function | Description |
|----------|-------------|
| `rest_router_init` | Create a new router with resource table |
| `rest_register_route` | Map (method, URI pattern) to handler function |
| `rest_dispatch` | Route incoming request URI to matching handler |
| `rest_url_parse` | Extract path and query parameters from URL |
| `rest_uri_match` | Match URI against pattern with `{param}` extraction |

## Building

```
make              # Build all demo executables
make http2_demo   # Build HTTP/2 demo only
make ws_demo      # Build WebSocket demo only
make mqtt_demo    # Build MQTT demo only
make clean        # Remove build artifacts
```

Binaries are output to `bin/`.

## Running Demos

```
bin/http2_demo    # Settings exchange, stream open, HEADERS/DATA, HPACK
bin/ws_demo       # Handshake, text send/receive, ping/pong, close
bin/mqtt_demo     # Broker with subscribe/publish dispatch, topic matching
```

## Project Structure

```
mini-app-protocol/
├── include/
│   ├── http2_frames.h     # HTTP/2 frame types, structs, function declarations
│   ├── grpc_proto.h       # gRPC messages, services, serialization
│   ├── websocket.h        # WebSocket frames, handshake, masking
│   ├── mqtt.h             # MQTT packets, broker, topic matching
│   └── rest_api.h         # REST router, resources, request/response
├── src/
│   ├── http2_frames.c     # 250+ lines: binary framing + HPACK + flow control
│   ├── grpc_proto.c       # 180+ lines: message encode/decode + server registry
│   ├── websocket.c        # 320+ lines: SHA1 + base64 + frame encode/decode
│   ├── mqtt.c             # 400+ lines: all packet types + broker simulation
│   └── rest_api.c         # 200+ lines: router + URI matching + parse
├── examples/
│   ├── http2_demo.c       # Full HTTP/2 connection simulation
│   ├── websocket_demo.c   # Handshake + messaging cycle
│   └── mqtt_demo.c        # Broker with wildcard topic dispatch
├── demos/
│   ├── mini-http2/README.md       # HTTP/2 deep dive (streams, HPACK, push)
│   └── mini-mqtt-broker/README.md # MQTT deep dive (QoS, sessions, retain)
├── docs/
│   ├── course-alignment.md         # Feature vs RFC specification mapping
│   └── application-protocols-survey.md  # Protocol comparison survey
├── README.md
└── Makefile
```

## Dependencies

- C99 compiler (GCC or Clang)
- Standard C library (`libc`)
- Math library (`libm`)

No external dependencies. No POSIX-specific APIs. No networking stack required.

## Design Notes

- All functions use `snake_case`, types use `PascalCase`, constants use `UPPER_SNAKE_CASE`.
- Headers include `<stdbool.h>` for portable `bool` support.
- Bounds checking on all buffer operations to prevent overflow.
- Consistent error return values: negative for errors, zero for success.
- Header guards use `#ifndef X_H` / `#define X_H` / `#endif` pattern.
