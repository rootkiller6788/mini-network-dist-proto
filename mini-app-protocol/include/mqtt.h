#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MQTT_MAX_CLIENT_ID   64
#define MQTT_MAX_TOPIC       256
#define MQTT_MAX_PAYLOAD     65536
#define MQTT_MAX_USERNAME    64
#define MQTT_MAX_PASSWORD    64
#define MQTT_MAX_WILL_TOPIC  256
#define MQTT_MAX_WILL_MSG    256
#define MQTT_MAX_SUBS_PER_CLIENT 32
#define MQTT_MAX_CLIENTS     64
#define MQTT_PROTOCOL_NAME    "MQTT"
#define MQTT_PROTOCOL_VERSION 5

enum MQTTPacketType {
    MQTT_CONNECT     = 0x10,
    MQTT_CONNACK     = 0x20,
    MQTT_PUBLISH     = 0x30,
    MQTT_PUBACK      = 0x40,
    MQTT_PUBREC      = 0x50,
    MQTT_PUBREL      = 0x60,
    MQTT_PUBCOMP     = 0x70,
    MQTT_SUBSCRIBE   = 0x82,
    MQTT_SUBACK      = 0x90,
    MQTT_UNSUBSCRIBE = 0xA2,
    MQTT_UNSUBACK    = 0xB0,
    MQTT_PINGREQ     = 0xC0,
    MQTT_PINGRESP    = 0xD0,
    MQTT_DISCONNECT  = 0xE0,
    MQTT_AUTH        = 0xF0
};

enum MQTTQoS {
    MQTT_QOS_0 = 0,
    MQTT_QOS_1 = 1,
    MQTT_QOS_2 = 2
};

enum MQTTReturnCode {
    MQTT_RC_SUCCESS              = 0x00,
    MQTT_RC_UNSPECIFIED_ERROR    = 0x80,
    MQTT_RC_MALFORMED_PACKET     = 0x81,
    MQTT_RC_PROTOCOL_ERROR       = 0x82,
    MQTT_RC_NOT_AUTHORIZED       = 0x87,
    MQTT_RC_SERVER_UNAVAILABLE   = 0x88,
    MQTT_RC_BAD_USERNAME_PASSWORD = 0x86
};

typedef struct {
    char     client_id[MQTT_MAX_CLIENT_ID];
    bool     clean_start;
    uint16_t keep_alive;
    char     username[MQTT_MAX_USERNAME];
    char     password[MQTT_MAX_PASSWORD];
    char     will_topic[MQTT_MAX_WILL_TOPIC];
    char     will_message[MQTT_MAX_WILL_MSG];
    bool     will_set;
    uint8_t  will_qos;
    bool     will_retain;
} MQTTConnect;

typedef struct {
    char     name[MQTT_MAX_TOPIC];
    uint8_t  qos;
} MQTTTopic;

typedef struct {
    uint16_t packet_id;
    char     topic[MQTT_MAX_TOPIC];
    uint8_t *payload;
    size_t   payload_len;
    uint8_t  qos;
    bool     retain;
    bool     dup;
} MQTTPublish;

typedef struct {
    char     client_id[MQTT_MAX_CLIENT_ID];
    MQTTTopic subscriptions[MQTT_MAX_SUBS_PER_CLIENT];
    size_t   sub_count;
    bool     connected;
} MQTTClient;

typedef struct {
    MQTTClient clients[MQTT_MAX_CLIENTS];
    size_t     client_count;
} MQTTBroker;

size_t  mqtt_encode_connect(const MQTTConnect *connect,
                            uint8_t *out, size_t out_size);
size_t  mqtt_encode_connack(bool session_present, uint8_t return_code,
                            uint8_t *out, size_t out_size);
size_t  mqtt_encode_publish(const MQTTPublish *pub,
                            uint8_t *out, size_t out_size);
size_t  mqtt_encode_subscribe(uint16_t packet_id,
                              const MQTTTopic *topics, size_t topic_count,
                              uint8_t *out, size_t out_size);
size_t  mqtt_encode_suback(uint16_t packet_id, const uint8_t *return_codes,
                           size_t code_count, uint8_t *out, size_t out_size);
size_t  mqtt_encode_pingreq(uint8_t *out, size_t out_size);
size_t  mqtt_encode_pingresp(uint8_t *out, size_t out_size);
size_t  mqtt_encode_disconnect(uint8_t *out, size_t out_size);
int     mqtt_decode_packet(const uint8_t *data, size_t len,
                           enum MQTTPacketType *type, void *packet_out);
int     mqtt_decode_publish(const uint8_t *data, size_t len,
                            MQTTPublish *pub);
int     mqtt_decode_connect(const uint8_t *data, size_t len,
                            MQTTConnect *connect);
int     mqtt_decode_subscribe(const uint8_t *data, size_t len,
                              uint16_t *packet_id, MQTTTopic *topics,
                              size_t *topic_count, size_t max_topics);

void    mqtt_broker_init(MQTTBroker *broker);
int     mqtt_broker_connect(MQTTBroker *broker, const char *client_id);
void    mqtt_broker_disconnect(MQTTBroker *broker, const char *client_id);
int     mqtt_broker_subscribe(MQTTBroker *broker, const char *client_id,
                              const MQTTTopic *topics, size_t topic_count);
int     mqtt_broker_handle_publish(MQTTBroker *broker, const MQTTPublish *pub,
                                   char *target_clients[], size_t *count,
                                   size_t max_targets);
bool    mqtt_topic_match(const char *filter, const char *topic);
size_t  mqtt_encode_remaining_length(uint32_t value, uint8_t *out);
int     mqtt_decode_remaining_length(const uint8_t *data, size_t len,
                                     uint32_t *value, size_t *consumed);

#endif
