#include "mqtt.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

size_t mqtt_encode_remaining_length(uint32_t value, uint8_t *out)
{
    size_t pos = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value > 0) byte |= 0x80;
        out[pos++] = byte;
    } while (value > 0);
    return pos;
}

int mqtt_decode_remaining_length(const uint8_t *data, size_t len,
                                 uint32_t *value, size_t *consumed)
{
    uint32_t val = 0;
    size_t   pos = 0;
    int      shift = 0;

    while (pos < len && pos < 4) {
        uint8_t byte = data[pos];
        val |= ((uint32_t)(byte & 0x7F)) << shift;
        pos++;
        if (!(byte & 0x80)) break;
        shift += 7;
    }

    *value    = val;
    *consumed = pos;
    return 0;
}

static size_t mqtt_encode_utf8(const char *str, uint8_t *out)
{
    size_t len = strlen(str);
    out[0] = (uint8_t)(len >> 8);
    out[1] = (uint8_t)(len);
    memcpy(out + 2, str, len);
    return 2 + len;
}

size_t mqtt_encode_connect(const MQTTConnect *connect,
                           uint8_t *out, size_t out_size)
{
    if (!connect || !out) return 0;

    size_t pos = 0;
    out[pos++] = MQTT_CONNECT;

    uint8_t var_header[10];
    var_header[0] = 0x00;                          /* Protocol name length MSB */
    var_header[1] = 0x04;                          /* Protocol name length LSB */
    var_header[2] = 'M';
    var_header[3] = 'Q';
    var_header[4] = 'T';
    var_header[5] = 'T';
    var_header[6] = MQTT_PROTOCOL_VERSION;         /* Protocol version 5.0 */

    uint8_t flags = 0x00;
    if (connect->clean_start) flags |= 0x02;
    if (connect->will_set) {
        flags |= 0x04;
        flags |= (connect->will_qos & 0x03) << 3;
        if (connect->will_retain) flags |= 0x20;
    }
    if (connect->username[0] != '\0') flags |= 0x80;
    if (connect->password[0] != '\0') flags |= 0x40;
    var_header[7] = flags;
    var_header[8] = (uint8_t)(connect->keep_alive >> 8);
    var_header[9] = (uint8_t)(connect->keep_alive);

    uint8_t payload_buf[2048];
    size_t  payload_len = 0;

    payload_len += mqtt_encode_utf8(connect->client_id,
                                    payload_buf + payload_len);

    if (connect->will_set) {
        payload_len += mqtt_encode_utf8(connect->will_topic,
                                        payload_buf + payload_len);
        payload_len += mqtt_encode_utf8(connect->will_message,
                                        payload_buf + payload_len);
    }

    if (connect->username[0] != '\0') {
        payload_len += mqtt_encode_utf8(connect->username,
                                        payload_buf + payload_len);
    }

    if (connect->password[0] != '\0') {
        payload_len += mqtt_encode_utf8(connect->password,
                                        payload_buf + payload_len);
    }

    size_t variable_len = 10 + payload_len;
    uint8_t rl_buf[4];
    size_t  rl_len = mqtt_encode_remaining_length((uint32_t)variable_len, rl_buf);

    size_t total = 1 + rl_len + variable_len;
    if (total > out_size) return 0;

    memcpy(out + pos, rl_buf, rl_len);
    pos += rl_len;
    memcpy(out + pos, var_header, 10);
    pos += 10;
    memcpy(out + pos, payload_buf, payload_len);
    pos += payload_len;

    return pos;
}

size_t mqtt_encode_connack(bool session_present, uint8_t return_code,
                           uint8_t *out, size_t out_size)
{
    if (!out) return 0;

    uint8_t flags = session_present ? 0x01 : 0x00;

    uint8_t var_header[2] = {flags, return_code};
    uint8_t rl_buf[4];
    size_t  rl_len = mqtt_encode_remaining_length(2, rl_buf);

    size_t total = 1 + rl_len + 2;
    if (total > out_size) return 0;

    size_t pos = 0;
    out[pos++] = MQTT_CONNACK;
    memcpy(out + pos, rl_buf, rl_len); pos += rl_len;
    memcpy(out + pos, var_header, 2);  pos += 2;

    return pos;
}

size_t mqtt_encode_publish(const MQTTPublish *pub,
                           uint8_t *out, size_t out_size)
{
    if (!pub || !out) return 0;

    size_t topic_len = strlen(pub->topic);

    uint8_t fixed_header = MQTT_PUBLISH;
    if (pub->dup)   fixed_header |= 0x08;
    if (pub->retain) fixed_header |= 0x01;
    fixed_header |= (uint8_t)((pub->qos & 0x03) << 1);

    size_t var_len = 2 + topic_len;
    if (pub->qos > 0) var_len += 2;
    var_len += pub->payload_len;

    uint8_t rl_buf[4];
    size_t  rl_len = mqtt_encode_remaining_length((uint32_t)var_len, rl_buf);

    size_t total = 1 + rl_len + var_len;
    if (total > out_size) return 0;

    size_t pos = 0;
    out[pos++] = fixed_header;
    memcpy(out + pos, rl_buf, rl_len); pos += rl_len;

    out[pos++] = (uint8_t)(topic_len >> 8);
    out[pos++] = (uint8_t)(topic_len);
    memcpy(out + pos, pub->topic, topic_len); pos += topic_len;

    if (pub->qos > 0) {
        out[pos++] = (uint8_t)(pub->packet_id >> 8);
        out[pos++] = (uint8_t)(pub->packet_id);
    }

    if (pub->payload && pub->payload_len > 0) {
        memcpy(out + pos, pub->payload, pub->payload_len);
        pos += pub->payload_len;
    }

    return pos;
}

size_t mqtt_encode_subscribe(uint16_t packet_id,
                             const MQTTTopic *topics, size_t topic_count,
                             uint8_t *out, size_t out_size)
{
    if (!topics || !out) return 0;

    size_t var_len = 2; /* packet_id */
    for (size_t i = 0; i < topic_count; i++) {
        var_len += 2 + strlen(topics[i].name) + 1; /* topic + QoS */
    }

    uint8_t rl_buf[4];
    size_t  rl_len = mqtt_encode_remaining_length((uint32_t)var_len, rl_buf);

    size_t total = 1 + rl_len + var_len;
    if (total > out_size) return 0;

    size_t pos = 0;
    out[pos++] = MQTT_SUBSCRIBE;
    memcpy(out + pos, rl_buf, rl_len); pos += rl_len;

    out[pos++] = (uint8_t)(packet_id >> 8);
    out[pos++] = (uint8_t)(packet_id);

    for (size_t i = 0; i < topic_count; i++) {
        size_t tlen = strlen(topics[i].name);
        out[pos++] = (uint8_t)(tlen >> 8);
        out[pos++] = (uint8_t)(tlen);
        memcpy(out + pos, topics[i].name, tlen); pos += tlen;
        out[pos++] = topics[i].qos & 0x03;
    }

    return pos;
}

size_t mqtt_encode_suback(uint16_t packet_id, const uint8_t *return_codes,
                          size_t code_count, uint8_t *out, size_t out_size)
{
    if (!return_codes || !out) return 0;

    size_t var_len = 2 + code_count;
    uint8_t rl_buf[4];
    size_t  rl_len = mqtt_encode_remaining_length((uint32_t)var_len, rl_buf);

    size_t total = 1 + rl_len + var_len;
    if (total > out_size) return 0;

    size_t pos = 0;
    out[pos++] = MQTT_SUBACK;
    memcpy(out + pos, rl_buf, rl_len); pos += rl_len;

    out[pos++] = (uint8_t)(packet_id >> 8);
    out[pos++] = (uint8_t)(packet_id);
    memcpy(out + pos, return_codes, code_count); pos += code_count;

    return pos;
}

size_t mqtt_encode_pingreq(uint8_t *out, size_t out_size)
{
    if (!out || out_size < 2) return 0;
    out[0] = MQTT_PINGREQ;
    out[1] = 0x00;
    return 2;
}

size_t mqtt_encode_pingresp(uint8_t *out, size_t out_size)
{
    if (!out || out_size < 2) return 0;
    out[0] = MQTT_PINGRESP;
    out[1] = 0x00;
    return 2;
}

size_t mqtt_encode_disconnect(uint8_t *out, size_t out_size)
{
    if (!out || out_size < 2) return 0;
    out[0] = MQTT_DISCONNECT;
    out[1] = 0x00;
    return 2;
}

int mqtt_decode_packet(const uint8_t *data, size_t len,
                       enum MQTTPacketType *type, void *packet_out)
{
    if (!data || !type || len < 2) return -1;

    *type = (enum MQTTPacketType)(data[0] & 0xF0);
    (void)packet_out;
    return 0;
}

int mqtt_decode_connect(const uint8_t *data, size_t len,
                        MQTTConnect *connect)
{
    if (!data || !connect || len < 14) return -1;

    size_t pos = 0;
    uint8_t pkt_type = data[pos] & 0xF0;
    if (pkt_type != MQTT_CONNECT) return -2;
    pos++;

    uint32_t remaining;
    size_t   consumed;
    mqtt_decode_remaining_length(data + pos, len - pos, &remaining, &consumed);
    pos += consumed;

    uint16_t proto_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;
    if (proto_len > 0) pos += proto_len;

    uint8_t version = data[pos];
    pos++;
    (void)version;

    uint8_t flags = data[pos];
    pos++;

    connect->clean_start = (flags & 0x02) ? true : false;
    connect->will_set    = (flags & 0x04) ? true : false;
    connect->will_qos    = (flags >> 3) & 0x03;
    connect->will_retain = (flags & 0x20) ? true : false;

    connect->keep_alive = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;

    uint16_t id_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;
    if (id_len > 0 && id_len < MQTT_MAX_CLIENT_ID) {
        memcpy(connect->client_id, data + pos, id_len);
        connect->client_id[id_len] = '\0';
        pos += id_len;
    }

    if (connect->will_set) {
        uint16_t wt_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (wt_len < MQTT_MAX_WILL_TOPIC) {
            memcpy(connect->will_topic, data + pos, wt_len);
            connect->will_topic[wt_len] = '\0';
            pos += wt_len;
        }

        uint16_t wm_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (wm_len < MQTT_MAX_WILL_MSG) {
            memcpy(connect->will_message, data + pos, wm_len);
            connect->will_message[wm_len] = '\0';
            pos += wm_len;
        }
    }

    if (flags & 0x80) {
        uint16_t un_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (un_len > 0 && un_len < MQTT_MAX_USERNAME) {
            memcpy(connect->username, data + pos, un_len);
            connect->username[un_len] = '\0';
            pos += un_len;
        }
    }

    if (flags & 0x40) {
        uint16_t pw_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (pw_len > 0 && pw_len < MQTT_MAX_PASSWORD) {
            memcpy(connect->password, data + pos, pw_len);
            connect->password[pw_len] = '\0';
            pos += pw_len;
        }
    }

    return 0;
}

int mqtt_decode_publish(const uint8_t *data, size_t len,
                        MQTTPublish *pub)
{
    if (!data || !pub || len < 2) return -1;

    size_t pos = 0;
    uint8_t pkt_type = data[pos] & 0xF0;
    if (pkt_type != MQTT_PUBLISH) return -2;

    pub->dup    = (data[pos] & 0x08) ? true : false;
    pub->retain = (data[pos] & 0x01) ? true : false;
    pub->qos    = (data[pos] >> 1) & 0x03;
    pos++;

    uint32_t remaining;
    size_t   consumed;
    mqtt_decode_remaining_length(data + pos, len - pos, &remaining, &consumed);
    pos += consumed;

    uint16_t topic_len = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;
    if (topic_len >= MQTT_MAX_TOPIC) return -3;

    memcpy(pub->topic, data + pos, topic_len);
    pub->topic[topic_len] = '\0';
    pos += topic_len;

    if (pub->qos > 0) {
        pub->packet_id = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
    }

    pub->payload_len = len - pos;
    pub->payload     = (uint8_t *)(data + pos);

    return 0;
}

int mqtt_decode_subscribe(const uint8_t *data, size_t len,
                          uint16_t *packet_id, MQTTTopic *topics,
                          size_t *topic_count, size_t max_topics)
{
    if (!data || !packet_id || !topics || !topic_count) return -1;

    size_t pos = 0;
    pos++; /* Skip fixed header byte */

    uint32_t remaining;
    size_t   consumed;
    mqtt_decode_remaining_length(data + pos, len - pos, &remaining, &consumed);
    pos += consumed;

    *packet_id = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;

    *topic_count = 0;
    while (pos < len && *topic_count < max_topics) {
        uint16_t tlen = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        if (tlen >= MQTT_MAX_TOPIC || pos + tlen + 1 > len) break;

        memcpy(topics[*topic_count].name, data + pos, tlen);
        topics[*topic_count].name[tlen] = '\0';
        pos += tlen;

        topics[*topic_count].qos = data[pos] & 0x03;
        pos++;

        (*topic_count)++;
    }

    return 0;
}

bool mqtt_topic_match(const char *filter, const char *topic)
{
    if (!filter || !topic) return false;

    while (*filter && *topic) {
        if (*filter == '+') {
            while (*topic && *topic != '/') topic++;
            filter++;
        } else if (*filter == '#') {
            return true;
        } else if (*filter == *topic) {
            filter++;
            topic++;
        } else {
            return false;
        }
    }

    if (*filter == '#') return true;

    return (*filter == '\0' && *topic == '\0');
}

void mqtt_broker_init(MQTTBroker *broker)
{
    if (!broker) return;
    memset(broker, 0, sizeof(*broker));
    broker->client_count = 0;
}

int mqtt_broker_connect(MQTTBroker *broker, const char *client_id)
{
    if (!broker || !client_id) return -1;
    if (broker->client_count >= MQTT_MAX_CLIENTS) return -2;

    MQTTClient *c = &broker->clients[broker->client_count];
    snprintf(c->client_id, sizeof(c->client_id), "%s", client_id);
    c->connected = true;
    c->sub_count = 0;

    broker->client_count++;
    return 0;
}

void mqtt_broker_disconnect(MQTTBroker *broker, const char *client_id)
{
    if (!broker || !client_id) return;

    for (size_t i = 0; i < broker->client_count; i++) {
        if (strcmp(broker->clients[i].client_id, client_id) == 0) {
            broker->clients[i].connected = false;
            return;
        }
    }
}

int mqtt_broker_subscribe(MQTTBroker *broker, const char *client_id,
                          const MQTTTopic *topics, size_t topic_count)
{
    if (!broker || !client_id || !topics) return -1;

    for (size_t i = 0; i < broker->client_count; i++) {
        if (strcmp(broker->clients[i].client_id, client_id) == 0) {
            MQTTClient *c = &broker->clients[i];
            for (size_t j = 0; j < topic_count; j++) {
                if (c->sub_count >= MQTT_MAX_SUBS_PER_CLIENT) break;
                memcpy(&c->subscriptions[c->sub_count], &topics[j],
                       sizeof(MQTTTopic));
                c->sub_count++;
            }
            return 0;
        }
    }

    return -2;
}

void mqtt_qos_manager_init(MQTTQoSManager *mgr)
{
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
}

static MQTTQoSTracker *mqtt_qos_find(MQTTQoSManager *mgr, uint16_t packet_id)
{
    for (size_t i = 0; i < mgr->tracker_count; i++) {
        if (mgr->trackers[i].packet_id == packet_id)
            return &mgr->trackers[i];
    }
    return NULL;
}

int mqtt_qos_track_outgoing(MQTTQoSManager *mgr, uint16_t packet_id,
                             enum MQTTQoS qos, const char *topic,
                             const uint8_t *payload, size_t payload_len,
                             bool retain)
{
    if (!mgr || !topic) return -1;
    if (qos == MQTT_QOS_0) return 0;
    if (mgr->tracker_count >= 64) return -2;

    MQTTQoSTracker *t = &mgr->trackers[mgr->tracker_count];
    memset(t, 0, sizeof(*t));
    t->packet_id = packet_id;
    t->qos       = qos;
    t->state     = (qos == MQTT_QOS_1) ? MQTT_QOS_STATE_AWAITING_PUBACK
                                       : MQTT_QOS_STATE_AWAITING_PUBREC;
    t->payload_len = payload_len;
    t->payload     = (uint8_t *)malloc(payload_len);
    if (t->payload && payload) memcpy(t->payload, payload, payload_len);
    snprintf(t->topic, sizeof(t->topic), "%s", topic);
    t->retain      = retain;
    t->max_retries = 5;
    t->retry_count = 0;

    mgr->tracker_count++;
    return 0;
}

int mqtt_qos_track_incoming(MQTTQoSManager *mgr, uint16_t packet_id,
                             enum MQTTQoS qos, const char *topic,
                             const uint8_t *payload, size_t payload_len,
                             bool retain)
{
    if (!mgr || !topic) return -1;
    if (qos == MQTT_QOS_0) return 0;
    if (mgr->tracker_count >= 64) return -2;

    MQTTQoSTracker *t = &mgr->trackers[mgr->tracker_count];
    memset(t, 0, sizeof(*t));
    t->packet_id = packet_id;
    t->qos       = qos;
    t->state     = (qos == MQTT_QOS_1) ? MQTT_QOS_STATE_COMPLETE
                                       : MQTT_QOS_STATE_AWAITING_PUBREL;
    t->payload_len = payload_len;
    t->payload     = (uint8_t *)malloc(payload_len);
    if (t->payload && payload) memcpy(t->payload, payload, payload_len);
    snprintf(t->topic, sizeof(t->topic), "%s", topic);
    t->retain      = retain;
    t->max_retries = 5;

    mgr->tracker_count++;
    return 0;
}

int mqtt_qos_handle_puback(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return -1;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    if (!t) return -2;

    if (t->state == MQTT_QOS_STATE_AWAITING_PUBACK) {
        t->state = MQTT_QOS_STATE_COMPLETE;
        free(t->payload);
        t->payload = NULL;
        return 0;
    }

    return -3;
}

int mqtt_qos_handle_pubrec(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return -1;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    if (!t) return -2;

    if (t->state == MQTT_QOS_STATE_AWAITING_PUBREC) {
        t->state = MQTT_QOS_STATE_AWAITING_PUBCOMP;
        return 0;
    }

    return -3;
}

int mqtt_qos_handle_pubrel(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return -1;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    if (!t) return -2;

    if (t->state == MQTT_QOS_STATE_AWAITING_PUBREL) {
        t->state = MQTT_QOS_STATE_COMPLETE;
        free(t->payload);
        t->payload = NULL;
        return 0;
    }

    return -3;
}

int mqtt_qos_handle_pubcomp(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return -1;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    if (!t) return -2;

    if (t->state == MQTT_QOS_STATE_AWAITING_PUBCOMP) {
        t->state = MQTT_QOS_STATE_COMPLETE;
        free(t->payload);
        t->payload = NULL;
        return 0;
    }

    return -3;
}

int mqtt_qos_get_next_action(MQTTQoSManager *mgr, uint16_t *packet_id,
                              enum MQTTPacketType *next_packet)
{
    if (!mgr || !packet_id || !next_packet) return -1;

    for (size_t i = 0; i < mgr->tracker_count; i++) {
        MQTTQoSTracker *t = &mgr->trackers[i];

        switch (t->state) {
        case MQTT_QOS_STATE_AWAITING_PUBACK:
            *packet_id   = t->packet_id;
            *next_packet = MQTT_PUBLISH;
            t->retry_count++;
            return 0;

        case MQTT_QOS_STATE_AWAITING_PUBREC:
            *packet_id   = t->packet_id;
            *next_packet = MQTT_PUBLISH;
            t->retry_count++;
            return 0;

        case MQTT_QOS_STATE_AWAITING_PUBREL:
            *packet_id   = t->packet_id;
            *next_packet = MQTT_PUBREL;
            return 1;

        case MQTT_QOS_STATE_AWAITING_PUBCOMP:
            *packet_id   = t->packet_id;
            *next_packet = MQTT_PUBREL;
            t->retry_count++;
            return 0;

        default:
            break;
        }
    }

    return -2;
}

void mqtt_qos_remove(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    if (!t) return;

    free(t->payload);
    t->payload = NULL;

    size_t idx = (size_t)(t - mgr->trackers);
    if (idx < mgr->tracker_count - 1) {
        mgr->trackers[idx] = mgr->trackers[mgr->tracker_count - 1];
    }
    mgr->tracker_count--;
}

bool mqtt_qos_is_complete(MQTTQoSManager *mgr, uint16_t packet_id)
{
    if (!mgr) return false;
    MQTTQoSTracker *t = mqtt_qos_find(mgr, packet_id);
    return t ? (t->state == MQTT_QOS_STATE_COMPLETE) : false;
}

int mqtt_broker_handle_publish(MQTTBroker *broker, const MQTTPublish *pub,
                               char *target_clients[], size_t *count,
                               size_t max_targets)
{
    if (!broker || !pub || !target_clients || !count) return -1;

    *count = 0;

    for (size_t i = 0; i < broker->client_count && *count < max_targets; i++) {
        if (!broker->clients[i].connected) continue;

        for (size_t j = 0; j < broker->clients[i].sub_count; j++) {
            if (mqtt_topic_match(broker->clients[i].subscriptions[j].name,
                                 pub->topic)) {
                target_clients[*count] = broker->clients[i].client_id;
                (*count)++;
                break;
            }
        }
    }

    return 0;
}
