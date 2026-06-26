# MyDocTitle

## MQTT Broker: Topics, Wildcards, QoS Levels, Persistent Sessions, Retained Messages

### 1. Introduction to MQTT

MQTT (Message Queuing Telemetry Transport) is a lightweight, publish-subscribe
messaging protocol designed for constrained devices and low-bandwidth, high-latency,
or unreliable networks. MQTT 5.0 (OASIS Standard) adds significant enhancements
over MQTT 3.1.1, including session expiry, message expiry, reason codes, shared
subscriptions, and improved error reporting.

The protocol follows a hub-and-spoke architecture where a central broker mediates
all message exchanges between clients. Clients never communicate directly with
each other; they publish messages to topics and subscribe to topics on the broker.

Key Design Principles:
- Minimal protocol overhead (2-byte minimum frame header)
- Asynchronous communication via publish/subscribe
- Three levels of Quality of Service (QoS)
- Persistent sessions with message queuing
- Topic-based filtering with wildcards
- Keep-alive pinging for connection health monitoring

### 2. MQTT Packet Format

Every MQTT packet consists of:
1. Fixed Header (2-5 bytes): Contains packet type, flags, and remaining length.
2. Variable Header (0+ bytes): Packet-type-specific header fields.
3. Payload (0+ bytes): Message content.

Fixed Header:
Byte 1: [Packet Type (4 bits)] [Flags (4 bits)]
Bytes 2-5: Remaining Length (variable-length encoding)

Packet Types (with byte 1 value):
| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| CONNECT | 0x10 | Client->Broker | Connection request |
| CONNACK | 0x20 | Broker->Client | Connection acknowledgment |
| PUBLISH | 0x30 | Both | Publish message |
| PUBACK | 0x40 | Both | Publish acknowledgment (QoS 1) |
| PUBREC | 0x50 | Both | Publish received (QoS 2) |
| PUBREL | 0x60 | Both | Publish release (QoS 2) |
| PUBCOMP | 0x70 | Both | Publish complete (QoS 2) |
| SUBSCRIBE | 0x82 | Client->Broker | Subscribe request |
| SUBACK | 0x90 | Broker->Client | Subscribe acknowledgment |
| UNSUBSCRIBE | 0xA2 | Client->Broker | Unsubscribe request |
| UNSUBACK | 0xB0 | Broker->Client | Unsubscribe acknowledgment |
| PINGREQ | 0xC0 | Client->Broker | Ping request |
| PINGRESP | 0xD0 | Broker->Client | Ping response |
| DISCONNECT | 0xE0 | Client->Broker | Disconnect notification |
| AUTH | 0xF0 | Both | Authentication exchange |

Remaining Length Encoding:
Uses a variable-length scheme where each byte encodes 7 bits of the value and
the MSB indicates continuation. Values 0-127 encode in 1 byte, 128-16383 in 2 bytes,
up to 268,435,455 in 4 bytes.

### 3. Topic Structure and Wildcards

MQTT topics are hierarchical UTF-8 strings using forward slash (/) as separators.
Topics are case-sensitive and must contain at least one character.

Topic Examples:
```
sensors/temperature/living-room
home/bedroom/light/state
factory/machine-1/rpm
devices/+/status        (single-level wildcard)
sensors/#               (multi-level wildcard)
```

Single-Level Wildcard (+):
Matches exactly one topic level. Placeholder for any string at that level.
- "sensors/+" matches "sensors/temp", "sensors/humidity"
- Does NOT match "sensors/temp/celsius" (only one level)
- Can be used multiple times: "home/+/light/+/state"

Multi-Level Wildcard (#):
Matches all remaining levels including parent level. Must be the last character.
- "sensors/#" matches "sensors/temp", "sensors/temp/celsius", "sensors/a/b/c/d"
- "#" matches all topics (use with caution)
- "#/status" is INVALID (# must be at the end)

Topic Matching Algorithm:
1. Split both filter and topic by '/'.
2. For each segment:
   a. If filter segment is '#', match succeeds immediately.
   b. If filter segment is '+', topic segment matches any string.
   c. Otherwise, exact string comparison (case-sensitive).
3. After all filter segments consumed, topic must also be fully consumed.

Subscription Rules:
- Clients can subscribe to multiple topics.
- Multiple subscriptions to the same topic may use different QoS levels.
- The broker delivers matching messages at the highest QoS level among
  overlapping subscriptions for the same client.

### 4. Quality of Service (QoS)

MQTT defines three QoS levels that provide increasing delivery guarantees.

QoS 0: At most once (Fire and Forget)
- PUBLISH sent once, no acknowledgment required.
- No retransmission, no queuing for offline clients.
- Fastest, lowest overhead.
- Suitable for high-frequency telemetry where occasional loss is acceptable
  (e.g., temperature readings every second).

Flow: PUBLISH -> (delivered)

QoS 1: At least once (Acknowledged Delivery)
- PUBLISH acknowledged with PUBACK.
- Sender MUST retransmit PUBLISH until PUBACK received (with DUP flag).
- May deliver duplicates to subscriber.
- Suitable for important messages that must not be lost.
- Receiver MUST ACK to the sender but may forward to subscribers at any QoS.

Flow: PUBLISH -> PUBACK

QoS 2: Exactly once (Assured Delivery)
- Four-packet handshake ensures exactly-once delivery.
- Sender: PUBLISH -> wait for PUBREC -> send PUBREL -> wait for PUBCOMP.
- Receiver: RECEIVE PUBLISH -> send PUBREC -> wait for PUBREL -> send PUBCOMP.
- Each packet identified by Packet Identifier for correlation.
- Highest overhead, used for critical messages (e.g., payment confirmations).

Flow: PUBLISH -> PUBREC -> PUBREL -> PUBCOMP

QoS Downgrade:
The broker can deliver a published message to a subscriber at a lower QoS
than the original publication. For example, a QoS 2 publication can be
delivered to a QoS 1 subscriber at QoS 1. This allows flexible per-subscriber
delivery guarantees.

### 5. Persistent Sessions

MQTT sessions enable message queuing for disconnected clients.

Non-Clean Session (clean_start=false):
- Broker stores subscriptions for the client.
- Broker queues messages for the client that arrive while disconnected:
  - QoS 1 and QoS 2 messages for matching subscriptions.
  - QoS 0 messages are NOT queued.
- When client reconnects, broker delivers queued messages.
- Session lasts until client disconnects with clean_start=true or session
  expiry interval elapses.

Clean Session (clean_start=true):
- Broker discards any existing session state.
- No subscriptions or queued messages persist across disconnection.
- Each connection starts fresh.

Session Expiry Interval (MQTT 5.0):
- Configurable via CONNECT properties.
- Set to 0 for session to end when network connection closes.
- Set to 0xFFFFFFFF for session never to expire (until broker restart
  or explicit clean session).
- Allows fine-grained control over session lifetime.

Session State Components:
1. Client subscriptions (topic filters + QoS).
2. Queued QoS 1 and QoS 2 messages not yet acknowledged.
3. QoS 2 messages in progress (awaiting PUBREL or PUBCOMP).
4. Will message configured for the session.

Will Message:
- Optional message published by broker when client disconnects unexpectedly.
- Components: Will Topic, Will Payload, Will QoS, Will Retain flag.
- Sent when: TCP connection lost without DISCONNECT, keep-alive timeout,
  protocol error causing disconnection.
- NOT sent when: Client sends DISCONNECT before closing.
- Use cases: "sensor-7/status" -> "offline" to detect device disconnection.

### 6. Retained Messages

The RETAIN flag on PUBLISH causes the broker to store the message along with
its QoS level. The latest retained message for each topic replaces any
previously retained message.

Behavior:
1. Client publishes with RETAIN=1.
2. Broker stores: (topic, payload, QoS).
3. When a new client subscribes to a matching topic:
   - Broker immediately sends the retained message to that client.
   - Only one retained message exists per topic.
4. To delete a retained message:
   - Publish a zero-byte payload with RETAIN=1 to that topic.
5. Retained messages are NOT sent for subscriptions with wildcards (# or +)
   unless the wildcard matches the specific topic.

Use Cases:
- Device status: "device/status" -> "online" with retain.
  New subscribers immediately know the device is online.
- Configuration: "config/timezone" -> "UTC" with retain.
  Clients always receive current configuration on connect.
- Last known values: "sensors/latest" with retain for dashboard.
- NOT suitable for: Rapidly changing values (use non-retained with QoS 0).

### 7. Keep-Alive Mechanism

Keep Alive Interval (CONNECT keep_alive):
- Maximum interval (seconds) between client messages.
- Client must send PINGREQ if no other traffic within the interval.
- Broker responds with PINGRESP.
- Broker disconnects client after 1.5x the keep-alive interval without
  any message from the client.

Purpose:
- Detects half-open connections (TCP break without proper closure).
- Ensures NAT/firewall timeout doesn't close idle connections.
- Triggers Will Message delivery upon timeout.
- Typical values: 10-60 seconds for IoT devices, 300+ for stable connections.

### 8. MQTT 5.0 Enhancements

Reason Codes:
- Every acknowledgment packet contains a reason code.
- Success codes: 0x00 (OK), 0x01 (Granted QoS 1), 0x02 (Granted QoS 2).
- Error codes: 0x80 (Unspecified), 0x81 (Malformed), 0x87 (Not Authorized).

Session Expiry:
- CONNECT and DISCONNECT include Session Expiry Interval property.
- Fine-grained session lifetime control.

Message Expiry:
- PUBLISH can include Message Expiry Interval property.
- Broker discards undelivered messages after expiry.
- Prevents stale data delivery to reconnecting clients.

Shared Subscriptions:
- Subscriptions prefixed with "$share/{group}/" distribute messages
  across clients in the same group.
- Enables load-balanced message processing.
- Each message delivered to exactly one client in the group.

User Properties:
- UTF-8 string key-value pairs.
- Can be added to most packets.
- Extensible metadata without protocol changes.

### 9. Broker Architecture

A minimal MQTT broker manages:

1. Client Registry:
   - Connected clients (client_id, TCP connection, session state).
   - Authentication: username/password in CONNECT.
   - Authorization: topic-level read/write permissions.

2. Subscription Table:
   - Topic-filter to client list mapping.
   - Wildcard expansion for efficient matching.
   - QoS negotiation per subscription.

3. Session Store:
   - Persistent sessions with message queues.
   - Will messages per session.
   - Session expiry timers.

4. Message Router:
   - Accept PUBLISH from clients.
   - Match against subscription table.
   - Deliver to authorized subscribers at their QoS level.
   - Handle retained messages (store and deliver).

5. Flow Control (MQTT 5.0):
   - Receive Maximum property limits in-flight QoS 1/2 messages.
   - Maximum Packet Size property limits PUBLISH payload size.

### 10. Our Implementation

The mini-app-protocol MQTT module provides:

- Binary packet encoding: mqtt_encode_connect, mqtt_encode_publish,
  mqtt_encode_subscribe, mqtt_encode_pingreq/pingresp/disconnect.
- Binary packet decoding: mqtt_decode_packet, mqtt_decode_connect,
  mqtt_decode_publish, mqtt_decode_subscribe.
- Remaining length encoding/decoding: mqtt_encode_remaining_length,
  mqtt_decode_remaining_length.
- Topic matching: mqtt_topic_match with + and # wildcard support.
- Broker simulation: mqtt_broker_init, mqtt_broker_connect,
  mqtt_broker_subscribe, mqtt_broker_handle_publish.

The implementation demonstrates core MQTT 5.0 concepts without requiring a
real TCP/IP transport, making it ideal for education and embedded system
integration where MQTT semantics are needed with minimal overhead.

### References

- MQTT Version 5.0 OASIS Standard: https://docs.oasis-open.org/mqtt/mqtt/v5.0/
- MQTT Version 3.1.1 OASIS Standard: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/
- HiveMQ MQTT Essentials: https://www.hivemq.com/mqtt-essentials/
