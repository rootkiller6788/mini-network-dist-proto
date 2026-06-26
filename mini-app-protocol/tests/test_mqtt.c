#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "mqtt.h"
static void t1(void){tr++;printf("  TEST %d: CONNECT ... ",tr);
MQTTConnect c;memset(&c,0,sizeof(c));snprintf(c.client_id,64,"t1");c.clean_start=1;c.keep_alive=60;
uint8_t b[512];size_t n=mqtt_encode_connect(&c,b,512);
if(!n||(b[0]&0xF0)!=(uint8_t)MQTT_CONNECT){tf++;printf("FAIL\n");return;}
MQTTConnect d;memset(&d,0,sizeof(d));mqtt_decode_connect(b,n,&d);
if(strcmp(d.client_id,"t1")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: PUBLISH ... ",tr);
MQTTPublish p;memset(&p,0,sizeof(p));snprintf(p.topic,256,"s/t");p.payload=(uint8_t*)"25";p.payload_len=2;p.qos=1;p.packet_id=42;
uint8_t b[512];size_t n=mqtt_encode_publish(&p,b,512);
if(!n||(b[0]&0xF0)!=(uint8_t)MQTT_PUBLISH){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: topic match ... ",tr);
if(!mqtt_topic_match("s/+/t","s/k/t")){tf++;printf("FAIL\n");return;}
if(!mqtt_topic_match("s/#","s/a/b")){tf++;printf("FAIL\n");return;}
if(mqtt_topic_match("s/t","s/h")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t4(void){tr++;printf("  TEST %d: broker ... ",tr);
MQTTBroker br;mqtt_broker_init(&br);mqtt_broker_connect(&br,"s1");mqtt_broker_connect(&br,"s2");
MQTTTopic t;snprintf(t.name,256,"h/+/t");t.qos=1;mqtt_broker_subscribe(&br,"s1",&t,1);
MQTTTopic t2;snprintf(t2.name,256,"h/#");t2.qos=1;mqtt_broker_subscribe(&br,"s2",&t2,1);
MQTTPublish p;memset(&p,0,sizeof(p));snprintf(p.topic,256,"h/k/t");p.payload=(uint8_t*)"22";p.payload_len=2;
char*tg[16];size_t cnt=0;mqtt_broker_handle_publish(&br,&p,tg,&cnt,16);
if(cnt!=2){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t5(void){tr++;printf("  TEST %d: QoS SM (L5) ... ",tr);
MQTTQoSManager mgr;mqtt_qos_manager_init(&mgr);
mqtt_qos_track_outgoing(&mgr,1,MQTT_QOS_1,"t",(const uint8_t*)"d",1,0);
mqtt_qos_handle_puback(&mgr,1);
if(!mqtt_qos_is_complete(&mgr,1)){tf++;printf("FAIL\n");return;}
mqtt_qos_track_outgoing(&mgr,2,MQTT_QOS_2,"t",(const uint8_t*)"d",1,0);
mqtt_qos_handle_pubrec(&mgr,2);mqtt_qos_handle_pubcomp(&mgr,2);
if(!mqtt_qos_is_complete(&mgr,2)){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
int main(void){printf("--- MQTT ---\n");t1();t2();t3();t4();t5();
printf("  %d/%d passed\n",tp,tr);return tf>0?1:0;}
