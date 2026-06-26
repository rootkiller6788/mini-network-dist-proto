# MyDocTitle

## Course Alignment: Protocol Implementations vs. RFC/Specifications

### 1. HTTP/2 Frame Protocol

| Feature | RFC 9113 Reference | Implementation | Status |
|---------|-------------------|----------------|--------|
| Binary framing layer | Section 4 | h2_frame_build / h2_frame_parse | Core |
| Frame header format (9 bytes) | Section 4.1 | H2FrameHeader struct | Core |
| DATA frame | Section 6.1 | H2_FRAME_DATA type | Core |
| HEADERS frame | Section 6.2 | H2_FRAME_HEADERS, h2_send_headers | Core |
| PRIORITY frame | Section 6.3 | H2_FRAME_PRIORITY type | Type defined |
| RST_STREAM frame | Section 6.4 | H2_FRAME_RST_STREAM type | Type defined |
| SETTINGS frame | Section 6.5 | h2_settings_build/parse/exchange | Core |
| PUSH_PROMISE frame | Section 6.6 | H2_FRAME_PUSH_PROMISE type | Type defined |
| PING frame | Section 6.7 | H2_FRAME_PING type | Type defined |
| GOAWAY frame | Section 6.8 | H2_FRAME_GOAWAY type | Type defined |
| WINDOW_UPDATE frame | Section 6.9 | h2_flow_control_update | Core |
| CONTINUATION frame | Section 6.10 | H2_FRAME_CONTINUATION type | Type defined |
| Stream identifiers | Section 5.1.1 | stream_id: 31 bits, odd=client | Core |
| Stream states | Section 5.1 | H2StreamState enum | Core |
| Stream lifecycle | Section 5.1 | h2_stream_open/close | Core |
| Flow control | Section 5.2 | h2_flow_control_update/can_send | Core |
| Connection preface | Section 3.5 | Not implemented (transport layer) | Future |
| HPACK header compression | RFC 7541 | h2_header_encode/decode (simplified) | Partial |
| Static table (61 entries) | RFC 7541 Sec 2.3.1 | Hardcoded static table | Core |
| Dynamic table | RFC 7541 Sec 2.3.2 | Dynamic table with 64 entries | Core |
| Integer encoding | RFC 7541 Sec 5.1 | hpack_encode_int | Core |
| Huffman encoding | RFC 7541 Sec 5.2 | Not implemented | Future |
| Server push | Section 8.2 | PUSH_PROMISE type defined | Minimal |
| Error codes | Section 7 | H2Error enum (all 14 codes) | Core |
| SETTINGS parameters | Section 6.5.2 | H2Settings struct (6 params) | Core |
| Stream priority | Section 5.3 | Not implemented | Future |

### 2. gRPC Protocol

| Feature | gRPC Spec Reference | Implementation | Status |
|---------|-------------------|----------------|--------|
| Length-prefixed messages | gRPC over HTTP/2 | grpc_encode_message / grpc_decode_message | Core |
| 5-byte message header | Compressed flag + 4-byte length | GRPCMessageHeader | Core |
| Service descriptor | Protobuf service definition | grpc_build_service_desc | Core |
| Unary RPC | gRPC concept | GRPC_UNARY | Core |
| Client streaming | gRPC concept | GRPC_CLIENT_STREAM | Type defined |
| Server streaming | gRPC concept | GRPC_SERVER_STREAM | Type defined |
| Bidirectional streaming | gRPC concept | GRPC_BIDI | Type defined |
| Status codes | gRPC status codes | GRPCStatusCode enum (17 codes) | Core |
| Request/response framing | gRPC wire format | grpc_send_request / grpc_send_response | Core |
| Service method registry | gRPC server | grpc_server_register / grpc_server_find_method | Core |
| Key-value serialization | Protobuf simulation | grpc_kv_serialize / grpc_kv_deserialize | Core |
| Protobuf binary encoding | Protobuf spec | Simplified as key-value pairs | Simulation |

### 3. WebSocket Protocol

| Feature | RFC 6455 Reference | Implementation | Status |
|---------|-------------------|----------------|--------|
| Opening handshake (client) | Section 4.2.1 | ws_handshake_build_client | Core |
| Opening handshake (server) | Section 4.2.2 | ws_handshake_build_server | Core |
| HTTP upgrade request | Section 4.1 | GET + Upgrade + Sec-WebSocket-Key | Core |
| Sec-WebSocket-Accept | Section 4.2.2 Item 5.4 | SHA1(Key+GUID) -> Base64 | Core |
| WebSocket GUID | Section 4.2.2 | 258EAFA5-E914-47DA-95CA-C5AB0DC85B11 | Core |
| Base framing protocol | Section 5.2 | ws_frame_encode / ws_frame_decode | Core |
| FIN bit | Section 5.2 | fin field in WSFrame | Core |
| Opcode (Text/Binary/Close/Ping/Pong) | Section 5.2 | WSOpcode enum | Core |
| Masking (client->server) | Section 5.3 | ws_apply_mask, mask field | Core |
| Payload length (7/16/64 bit) | Section 5.2 | ws_encode_length | Core |
| Close frame | Section 5.5.1 | ws_send_close, WSCloseCode enum | Core |
| Ping frame | Section 5.5.2 | ws_send_ping | Core |
| Pong frame | Section 5.5.3 | ws_send_pong | Core |
| Text frame send/receive | Section 5.6 | ws_send_text / ws_recv_text | Core |
| SHA-1 hash | N/A | ws_sha1_hash (simplified) | Core |
| Base64 encoding | RFC 4648 | ws_base64_encode | Core |
| Fragmentation | Section 5.4 | Not implemented | Future |
| Extensions | Section 9 | Not implemented | Future |

### 4. MQTT 5.0 Protocol

| Feature | MQTT 5.0 Spec Reference | Implementation | Status |
|---------|------------------------|----------------|--------|
| CONNECT packet | Section 3.1 | mqtt_encode_connect / mqtt_decode_connect | Core |
| CONNACK packet | Section 3.2 | mqtt_encode_connack | Core |
| PUBLISH packet | Section 3.3 | mqtt_encode_publish / mqtt_decode_publish | Core |
| PUBACK packet | Section 3.4 | MQTT_PUBACK type defined | Type defined |
| PUBREC/PUBREL/PUBCOMP | Section 3.5-3.7 | Types defined | Type defined |
| SUBSCRIBE packet | Section 3.8 | mqtt_encode_subscribe / mqtt_decode_subscribe | Core |
| SUBACK packet | Section 3.9 | mqtt_encode_suback | Core |
| PINGREQ/PINGRESP | Section 3.12-3.13 | mqtt_encode_pingreq/pingresp | Core |
| DISCONNECT packet | Section 3.14 | mqtt_encode_disconnect | Core |
| Fixed header format | Section 2.1 | Packet type + flags in first byte | Core |
| Remaining length encoding | Section 2.2.3 | mqtt_encode_remaining_length / decode | Core |
| UTF-8 string encoding | Section 1.5.4 | mqtt_encode_utf8 | Core |
| Topic filters | Section 4.7 | Topic matching with + and # | Core |
| Topic matching algorithm | Section 4.7.2 | mqtt_topic_match | Core |
| QoS 0 delivery | Section 4.3.1 | MQTT_QOS_0 | Core |
| QoS 1 delivery | Section 4.3.2 | MQTT_QOS_1 | Core |
| QoS 2 delivery | Section 4.3.3 | MQTT_QOS_2 | Type defined |
| Clean start flag | Section 3.1.2.4 | clean_start in MQTTConnect | Core |
| Will message | Section 3.1.3 | will_* fields in MQTTConnect | Core |
| Keep alive | Section 3.1.2.10 | keep_alive in MQTTConnect | Core |
| Session expiry | Section 3.1.2.11 | Not implemented (MQTT 5.0 property) | Future |
| Reason codes | Section 3.2.2.2 | MQTTReturnCode enum | Partial |
| Shared subscriptions | Section 4.8 | Not implemented | Future |
| Broker simulation | N/A | mqtt_broker_init/connect/subscribe/handle | Simulation |

### 5. REST API Framework

| Feature | HTTP Standard Reference | Implementation | Status |
|---------|------------------------|----------------|--------|
| HTTP methods (GET/POST/PUT/DELETE) | RFC 7231 | RESTMethod enum | Core |
| URI pattern matching | N/A | rest_uri_match with {param} | Core |
| Route registration | N/A | rest_register_route / rest_register_routes | Core |
| Request dispatching | N/A | rest_dispatch / rest_dispatch_full | Core |
| Status codes | RFC 7231 | RESTStatusCode enum (12 codes) | Core |
| Query string parsing | RFC 3986 | rest_url_parse | Core |
| Path parameters | N/A | path_params extraction | Core |
| Request/response model | N/A | RESTRequest / RESTResponse | Core |
| Headers management | RFC 7230 | rest_response_add_header | Core |
| JSON/Text response helpers | N/A | rest_response_json / rest_response_text | Core |

### Summary

The implementation covers the core binary/wire-level semantics of each protocol
while intentionally simplifying or omitting transport-layer concerns (TLS, TCP
connection management, socket I/O) and advanced features (HTTP/2 priority,
MQTT session expiry, WebSocket extensions). This aligns with the educational
goal of demonstrating protocol semantics in pure C without external dependencies.
