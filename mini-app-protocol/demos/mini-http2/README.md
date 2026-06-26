# MyDocTitle

## HTTP/2 Frame Protocol: Streams, Multiplexing, HPACK, Priority, Server Push

### 1. Introduction to HTTP/2

HTTP/2 (RFC 9113) represents a fundamental evolution from HTTP/1.1, transforming the
text-based request-response model into a binary framing layer that enables full request
and response multiplexing over a single TCP connection. The protocol introduces
concepts such as streams, frames, flow control, and header compression (HPACK) to
dramatically reduce latency and improve throughput.

HTTP/2 maintains the same HTTP semantics (methods, status codes, header fields, URIs)
but changes their transport significantly. All communication happens over a single
TCP connection, and messages are broken into binary frames that can be interleaved
freely across independent streams.

### 2. Binary Framing Layer

The binary framing layer is the core innovation of HTTP/2. Each communication unit
is a frame consisting of a 9-byte header followed by a variable-length payload.

Frame Header Structure (9 bytes):
- Length (24 bits): The length of the frame payload, up to 16,384 bytes by default
  (can be extended via SETTINGS_MAX_FRAME_SIZE up to 16,777,215 bytes).
- Type (8 bits): Identifies the frame type (DATA, HEADERS, SETTINGS, etc.).
- Flags (8 bits): Type-specific boolean flags (END_STREAM, END_HEADERS, PADDED, ACK).
- R (1 bit): Reserved bit, must be zero.
- Stream Identifier (31 bits): Uniquely identifies the stream (0 for connection-level).

Frame Types:
| Type | Code | Description |
|------|------|-------------|
| DATA | 0x00 | Request/response body data |
| HEADERS | 0x01 | Header block fragments |
| PRIORITY | 0x02 | Stream priority/dependency |
| RST_STREAM | 0x03 | Stream termination |
| SETTINGS | 0x04 | Connection configuration |
| PUSH_PROMISE | 0x05 | Server push notification |
| PING | 0x06 | Liveness/round-trip check |
| GOAWAY | 0x07 | Graceful connection shutdown |
| WINDOW_UPDATE | 0x08 | Flow control window increment |
| CONTINUATION | 0x09 | Continuation of header blocks |

Connection Preface:
Before any frames are exchanged, the client sends the HTTP/2 connection preface:
the 24-byte string "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" followed by a SETTINGS frame.
The server responds with its own SETTINGS frame (possibly preceded by a SETTINGS ACK).

### 3. Streams and Multiplexing

A stream is an independent, bidirectional sequence of frames exchanged between
client and server within a single HTTP/2 connection. Streams enable multiplexing,
where multiple concurrent requests and responses can be interleaved without
head-of-line blocking.

Stream Identifiers:
- Client-initiated streams: Odd numbers (1, 3, 5, ...)
- Server-initiated streams: Even numbers (2, 4, 6, ...)
- Stream 0: Reserved for connection-level control messages
- After GOAWAY, new stream IDs above the last-stream-id are rejected

Stream Lifecycle:
```
IDLE -> RESERVED_LOCAL -> OPEN -> HALF_CLOSED_REMOTE -> CLOSED
IDLE -> RESERVED_REMOTE -> OPEN -> HALF_CLOSED_LOCAL -> CLOSED
```

States:
- IDLE: Initial state; receiving HEADERS or PUSH_PROMISE transitions to OPEN.
- RESERVED_LOCAL: Push promise sent; waiting for response headers.
- RESERVED_REMOTE: Push promise received; waiting for request headers.
- OPEN: Both peers may send frames except frames with END_STREAM.
- HALF_CLOSED_LOCAL: One peer (local) cannot send DATA/HEADERS with END_STREAM
  set; the other can continue.
- HALF_CLOSED_REMOTE: Remote peer sent END_STREAM; local may continue.
- CLOSED: Terminal state; no frames allowed.

Multiplexing Benefits:
1. Eliminates head-of-line blocking at the HTTP layer.
2. Reduces connection count - one TCP connection per origin.
3. Lower latency through concurrent request processing.
4. Better TCP congestion control utilization.
5. Reduced resource consumption on servers (fewer sockets).

Stream Priority:
HTTP/2 allows clients to express priority preferences through PRIORITY frames:
- Dependency: Identifies the parent stream for cascading priorities.
- Weight: Integer 1-256 (default 16), determining proportional resource allocation.
- Exclusive flag: When set, the stream becomes the sole dependency of its parent.

Priority tree processing is advisory - servers MAY ignore prioritization hints
but SHOULD respect them to optimize delivery.

### 4. Flow Control

HTTP/2 implements per-stream and connection-level flow control using a credit-based
window scheme. This prevents fast senders from overwhelming slow receivers and
allows receivers to signal their processing capacity.

Mechanism:
- Initial window size: 65,535 bytes (negotiable via SETTINGS_INITIAL_WINDOW_SIZE).
- WINDOW_UPDATE frames: Increment the flow control window.
- DATA frames: Consume the flow control window by their payload size.
- Stream-level windows track individual stream flow control.
- Connection-level windows track aggregate flow across all streams.

Rules:
1. Flow control is hop-by-hop, not end-to-end.
2. Receivers cannot rely on senders maintaining a large window.
3. WINDOW_UPDATE for stream ID 0 affects the connection window.
4. Senders MUST NOT exceed the receiver's advertised window.
5. SETTINGS_INITIAL_WINDOW_SIZE changes affect all streams, preserving existing
   window ratios but requiring adjustment.

Flow Control Error (GOAWAY with H2_FLOW_CONTROL_ERROR) occurs when:
- A sender transmits more data than the window allows.
- A WINDOW_UPDATE causes the window to exceed 2^31 - 1.
- An endpoint violates flow control rules.

### 5. HPACK: Header Compression

HTTP/2 uses HPACK (RFC 7541) for header compression, replacing HTTP/1.1's
uncompressed headers. HPACK combines static and dynamic tables with Huffman
encoding to dramatically reduce header overhead, which is critical since
websites commonly transmit 500+ headers per page load.

Static Table (61 entries):
Contains common header name-value pairs like ":method: GET", ":status: 200",
"content-type", "accept-encoding: gzip, deflate", etc. Each entry is indexed
for efficient reference.

Dynamic Table:
- Starts empty and grows as header fields are exchanged.
- Maximum size configured via SETTINGS_HEADER_TABLE_SIZE.
- Uses FIFO eviction: oldest entries removed when table exceeds max size.
- Allows literal headers with incremental indexing (add to dynamic table)
  or without indexing (one-time use, like sensitive data).

HPACK Representations:
1. Indexed Header Field (0x80 prefix):
   Direct reference to static or dynamic table entry.
   Most compact: single byte for indices 1-62.

2. Literal Header Field with Incremental Indexing (0x40 prefix):
   Inserts new name-value pair into the dynamic table.
   Used for first occurrence of a header pair.

3. Literal Header Field without Indexing (0x00 prefix):
   One-time transmission, not added to dynamic table.
   Used for sensitive or rarely-repeated headers.

4. Literal Header Field Never Indexed (0x10 prefix):
   Similar to non-indexed but with explicit prohibition against indexing.
   Used for authorization headers and similar security-sensitive data.

5. Dynamic Table Size Update (0x20 prefix):
   Signals changes to the maximum dynamic table size.

Integer Representation:
HPACK uses a variable-length encoding for integers:
- Values 0 to (2^N - 1) fit in the N-bit prefix of the first byte.
- Larger values use the prefix set to all-1s, followed by continuation bytes
  using the 7-bit continuation pattern (MSB indicates continuation).

Huffman Encoding:
- String literals can optionally use Huffman coding for additional compression.
- A predefined Huffman table maps ASCII characters to variable-length codes.
- The Huffman code string is padded with 1s to align to byte boundaries.

### 6. Server Push

Server push enables the server to proactively send resources to the client
before the client requests them. This eliminates the round-trip time for
discovering dependent resources.

Push Process:
1. Client sends a request (stream A).
2. Server associates a push promise with stream A.
3. Server sends PUSH_PROMISE frame on stream A, creating promised stream B.
4. Server sends HEADERS and DATA on the promised stream.
5. Client may RST_STREAM the promised stream to reject the push.

Push Promise Frame:
- Contains the promised stream ID (even, server-initiated).
- Includes the request headers the server would use to generate the response.
- Sent on the associated request stream, not the promised stream.

Restrictions:
1. The promised stream ID MUST be greater than any previously promised or used
   stream ID.
2. PUSH_PROMISE MUST NOT be sent if SETTINGS_ENABLE_PUSH is 0.
3. Only safe, cacheable resources should be pushed.
4. Servers should monitor push utilization to avoid wasting bandwidth.

### 7. Connection Management

SETTINGS Frame:
Bidirectional configuration exchange at connection start and during the connection.

Key Parameters:
| ID | Parameter | Default |
|----|-----------|---------|
| 0x01 | HEADER_TABLE_SIZE | 4096 |
| 0x02 | ENABLE_PUSH | 1 |
| 0x03 | MAX_CONCURRENT_STREAMS | unlimited |
| 0x04 | INITIAL_WINDOW_SIZE | 65535 |
| 0x05 | MAX_FRAME_SIZE | 16384 |
| 0x06 | MAX_HEADER_LIST_SIZE | unlimited |

SETTINGS with ACK flag acknowledge receipt; no acknowledgment needed for
other SETTINGS frames.

PING Frame:
- Measures round-trip time and tests connection liveness.
- ACK flag distinguishes PING from PING acknowledgment.
- PING frames have higher priority than DATA frames.
- Payload MUST be exactly 8 bytes of opaque data.

GOAWAY Frame:
Initiates graceful shutdown. Contains:
- Last-Stream-ID: Highest stream ID the sender will process.
- Error Code: Reason for shutdown.
- Optional debug data.

After receiving GOAWAY:
1. Stop creating new streams.
2. Continue processing remaining streams initiated before Last-Stream-ID.
3. Send pending data and close open streams normally.
4. Close connection when all streams are complete.

RST_STREAM Frame:
Immediate stream termination with an error code:
- NO_ERROR: Normal closure.
- PROTOCOL_ERROR: Generic protocol violation.
- INTERNAL_ERROR: Implementation error.
- REFUSED_STREAM: Server rejects stream before processing.
- CANCEL: Stream is no longer needed.
- STREAM_CLOSED: Frame received for half-closed/closed stream.

### 8. Error Handling

HTTP/2 defines 14 error codes (0x00-0x0D):
- NO_ERROR (0x00): Graceful shutdown.
- PROTOCOL_ERROR (0x01): Generic protocol error.
- INTERNAL_ERROR (0x02): Implementation bug detected.
- FLOW_CONTROL_ERROR (0x03): Flow control violation.
- SETTINGS_TIMEOUT (0x04): SETTINGS acknowledgment not received.
- STREAM_CLOSED (0x05): Frame for closed/half-closed stream.
- FRAME_SIZE_ERROR (0x06): Frame size exceeds limit.
- REFUSED_STREAM (0x07): Stream refused before processing.
- CANCEL (0x08): Stream canceled by endpoint.
- COMPRESSION_ERROR (0x09): HPACK state inconsistent.
- CONNECT_ERROR (0x0A): CONNECT request failed.
- ENHANCE_YOUR_CALM (0x0B): Peer exhibiting excessive behavior.
- INADEQUATE_SECURITY (0x0C): TLS handshake insufficient.
- HTTP_1_1_REQUIRED (0x0D): Endpoint requires HTTP/1.1.

### 9. Performance Considerations

1. Stream Concurrency:
   SETTINGS_MAX_CONCURRENT_STREAMS limits active streams. Configure based on
   server capacity (typically 100-1000). Exceeding this leads to REFUSED_STREAM.

2. Frame Size Tuning:
   Default 16KB frame size works well for most content. Larger frame sizes
   (up to 16MB) reduce framing overhead but increase latency sensitivity.

3. Header Compression:
   HPACK is stateful - dynamic table size must be managed. Too small reduces
   compression efficiency. Too large wastes memory. 4096 bytes is typical.

4. Server Push Strategy:
   Push only critical resources. Over-pushing wastes bandwidth. Use Cache-Digest
   or similar mechanisms to avoid pushing already-cached resources.

5. TCP Optimization:
   HTTP/2 connection multiplexing changes TCP dynamics. Consider BBR congestion
   control and TCP_NOTSENT_LOWAT for optimal performance.

### 10. Security

- HTTP/2 over TLS requires TLS 1.2+ with specific cipher suites (RFC 7540
  Appendix A blacklists certain ciphers).
- ALPN (Application-Layer Protocol Negotiation) identifies HTTP/2 ("h2" or "h2c").
- HTTP/2 can run over cleartext TCP ("h2c") but browser support is limited.
- SETTINGS_ENABLE_PUSH allows disabling server push.
- RST_STREAM with ENHANCE_YOUR_CALM signals abusive client behavior.

### 11. Our Implementation

The mini-app-protocol implementation provides:
- Binary frame serialization/deserialization (h2_frame_build, h2_frame_parse).
- SETTINGS parameter negotiation (h2_settings_exchange).
- Stream lifecycle management (h2_stream_open, h2_stream_close).
- Simplified HPACK encoding (static table lookups, dynamic table insertion).
- Flow control credit tracking (h2_flow_control_update, h2_flow_control_can_send).
- HEADERS and DATA frame transmission helpers.

This implementation demonstrates the core HTTP/2 mechanisms without a full
TCP/TLS transport layer, making it suitable for educational purposes and
embedded systems where a full HTTP/2 stack is impractical but binary framing
and multiplexing capabilities are desired.

### References

- RFC 9113: HTTP/2
- RFC 7541: HPACK Header Compression for HTTP/2
- RFC 7540: HTTP/2 (obsoleted by RFC 9113)
- https://httpwg.org/specs/rfc9113.html
