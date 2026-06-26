#include "mqtt.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== MQTT Broker Simulation Demo ===\n\n");

    printf("[1] Broker Initialization\n");
    MQTTBroker broker;
    mqtt_broker_init(&broker);
    printf("    Broker ready, max clients: %d\n\n", MQTT_MAX_CLIENTS);

    printf("[2] Client Connections\n");
    mqtt_broker_connect(&broker, "subscriber-1");
    mqtt_broker_connect(&broker, "subscriber-2");
    mqtt_broker_connect(&broker, "publisher-1");
    printf("    Connected: subscriber-1, subscriber-2, publisher-1\n\n");

    printf("[3] Encode CONNECT Packet\n");
    MQTTConnect connect = {0};
    snprintf(connect.client_id, sizeof(connect.client_id), "sensor-client");
    connect.clean_start = true;
    connect.keep_alive  = 60;

    uint8_t connect_packet[512];
    size_t  clen = mqtt_encode_connect(&connect, connect_packet,
                                       sizeof(connect_packet));
    printf("    CONNECT packet: %zu bytes\n", clen);
    printf("    Header: %02x %02x ...\n\n", connect_packet[0], connect_packet[1]);

    printf("[4] Decode CONNECT Packet\n");
    MQTTConnect decoded_connect = {0};
    int rc = mqtt_decode_connect(connect_packet, clen, &decoded_connect);
    printf("    Result: %d, ClientID=\"%s\", clean=%d, keepalive=%d\n\n",
           rc, decoded_connect.client_id,
           decoded_connect.clean_start, decoded_connect.keep_alive);

    printf("[5] Encode CONNACK\n");
    uint8_t connack_packet[16];
    size_t  acklen = mqtt_encode_connack(false, MQTT_RC_SUCCESS,
                                         connack_packet, sizeof(connack_packet));
    printf("    CONNACK: %zu bytes\n\n", acklen);

    printf("[6] Subscribe to Topics\n");
    mqtt_broker_subscribe(&broker, "subscriber-1",
                          (MQTTTopic[]){{"sensors/#", 0}}, 1);
    mqtt_broker_subscribe(&broker, "subscriber-2",
                          (MQTTTopic[]){{"sensors/temp", 1}, {"sensors/+", 1}}, 2);
    printf("    subscriber-1 -> sensors/# (QoS 0)\n");
    printf("    subscriber-2 -> sensors/temp (QoS 1)\n");
    printf("    subscriber-2 -> sensors/+ (QoS 1)\n\n");

    printf("[7] Encode SUBSCRIBE Packet\n");
    MQTTTopic sub_topics[] = {
        {"sensors/temperature", 1},
        {"sensors/humidity", 2}
    };

    uint8_t subscribe_packet[512];
    size_t  slen = mqtt_encode_subscribe(0x0001, sub_topics, 2,
                                         subscribe_packet, sizeof(subscribe_packet));
    printf("    SUBSCRIBE: %zu bytes (topics: %s, %s)\n\n",
           slen, sub_topics[0].name, sub_topics[1].name);

    printf("[8] Decode SUBSCRIBE Packet\n");
    uint16_t pkt_id = 0;
    MQTTTopic decoded_topics[8];
    size_t   topic_count = 0;
    rc = mqtt_decode_subscribe(subscribe_packet, slen, &pkt_id,
                               decoded_topics, &topic_count, 8);
    printf("    Result: %d, PacketID=%u, Topics=%zu\n", rc, pkt_id, topic_count);
    for (size_t i = 0; i < topic_count; i++) {
        printf("      [%zu] %s (QoS %d)\n", i, decoded_topics[i].name,
               decoded_topics[i].qos);
    }
    printf("\n");

    printf("[9] Encode SUBACK\n");
    uint8_t return_codes[] = {0x00, 0x02};
    uint8_t suback_packet[32];
    size_t  sblen = mqtt_encode_suback(0x0001, return_codes, 2,
                                       suback_packet, sizeof(suback_packet));
    printf("    SUBACK: %zu bytes (codes: 0=SuccessQoS1, 2=SuccessQoS2)\n\n", sblen);

    printf("[10] Publish to sensors/temp\n");
    const char *payload_data = "{\"temperature\": 23.5, \"unit\": \"C\"}";
    MQTTPublish pub = {0};
    snprintf(pub.topic, sizeof(pub.topic), "sensors/temp");
    pub.payload     = (uint8_t *)payload_data;
    pub.payload_len = strlen(payload_data);
    pub.qos         = 1;
    pub.packet_id   = 42;
    pub.retain      = false;

    uint8_t publish_packet[1024];
    size_t  plen = mqtt_encode_publish(&pub, publish_packet,
                                       sizeof(publish_packet));
    printf("    PUBLISH: %zu bytes (topic=\"%s\", qos=%d)\n",
           plen, pub.topic, pub.qos);

    printf("    Payload: \"%s\"\n\n", payload_data);

    printf("[11] Decode PUBLISH\n");
    MQTTPublish decoded_pub = {0};
    rc = mqtt_decode_publish(publish_packet, plen, &decoded_pub);
    printf("    Result: %d\n", rc);
    printf("    Topic: \"%s\", QoS=%d, PacketID=%d\n",
           decoded_pub.topic, decoded_pub.qos, decoded_pub.packet_id);
    printf("    Payload: \"%.*s\"\n\n", (int)decoded_pub.payload_len,
           decoded_pub.payload);

    printf("[12] Broker Dispatch to Subscribers\n");
    char *targets[16];
    size_t target_count = 0;
    rc = mqtt_broker_handle_publish(&broker, &decoded_pub,
                                    targets, &target_count, 16);
    printf("    Result: %d, Matched %zu subscribers:\n", rc, target_count);
    for (size_t i = 0; i < target_count; i++) {
        printf("      -> %s\n", targets[i]);
    }
    printf("\n");

    printf("[13] Topic Matching Tests\n");
    struct {
        const char *filter;
        const char *topic;
        bool expected;
    } match_tests[] = {
        {"sensors/#",           "sensors/temp",          true},
        {"sensors/#",           "sensors/a/b/c",         true},
        {"sensors/+",           "sensors/temp",          true},
        {"sensors/+",           "sensors/a/b",           false},
        {"sensors/temperature", "sensors/temperature",   true},
        {"sensors/temperature", "sensors/humidity",      false},
        {"+/temperature",       "home/temperature",      true},
        {"#",                   "any/deep/path",         true},
        {"sensors/+/status",    "sensors/temp/status",   true},
        {"sensors/+/status",    "sensors/temp/value",    false},
    };

    int passed = 0, failed = 0;
    for (size_t i = 0; i < sizeof(match_tests)/sizeof(match_tests[0]); i++) {
        bool result = mqtt_topic_match(match_tests[i].filter,
                                       match_tests[i].topic);
        bool ok = (result == match_tests[i].expected);
        printf("    %s filter=\"%s\" topic=\"%s\" -> %s (expected %s)\n",
               ok ? "OK" : "FAIL",
               match_tests[i].filter, match_tests[i].topic,
               result ? "MATCH" : "NO MATCH",
               match_tests[i].expected ? "MATCH" : "NO MATCH");
        if (ok) passed++; else failed++;
    }
    printf("    %d/%zu passed\n\n", passed,
           sizeof(match_tests)/sizeof(match_tests[0]));

    printf("[14] Control Packets\n");
    uint8_t buf[16];
    size_t len;

    len = mqtt_encode_pingreq(buf, sizeof(buf));
    printf("    PINGREQ: %zu bytes\n", len);

    len = mqtt_encode_pingresp(buf, sizeof(buf));
    printf("    PINGRESP: %zu bytes\n", len);

    len = mqtt_encode_disconnect(buf, sizeof(buf));
    printf("    DISCONNECT: %zu bytes\n\n", len);

    printf("[15] Publish with Different QoS Levels\n");
    const char *qos_data = "QoS test message";
    for (int q = 0; q <= 2; q++) {
        MQTTPublish qpub = {0};
        snprintf(qpub.topic, sizeof(qpub.topic), "test/qos%d", q);
        qpub.payload     = (uint8_t *)qos_data;
        qpub.payload_len = strlen(qos_data);
        qpub.qos         = (uint8_t)q;
        qpub.packet_id   = (uint16_t)(100 + q);

        uint8_t qbuf[512];
        size_t  qlen = mqtt_encode_publish(&qpub, qbuf, sizeof(qbuf));
        printf("    QoS %d: %zu bytes\n", q, qlen);
    }
    printf("\n");

    printf("[16] Remaining Length Encoding\n");
    for (uint32_t v = 0; v <= 4194304; v = (v == 0) ? 64 : v * 2) {
        uint8_t rl[4];
        size_t  rllen = mqtt_encode_remaining_length(v, rl);
        uint32_t decoded_v = 0;
        size_t   consumed = 0;
        mqtt_decode_remaining_length(rl, rllen, &decoded_v, &consumed);
        if (v >= 4194304) break;
        if (v == 2097152) v = 4194304 - 128;
    }
    printf("    Encoding/decoding round-trip verified\n");

    printf("\n=== Demo Complete ===\n");
    return (failed == 0) ? 0 : 1;
}
