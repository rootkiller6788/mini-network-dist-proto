#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "sse.h"
static void t1(void){tr++;printf("  TEST %d: encode ... ",tr);
SSEEvent ev;memset(&ev,0,sizeof(ev));snprintf(ev.data,4096,"x");ev.data_len=1;
uint8_t b[1024];size_t n=sse_encode_event(&ev,b,1024);
if(!n||!strstr((char*)b,"data:")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: parse ... ",tr);
const char*s="id:1\ndata:h\n\n";SSEEvent*evs=(SSEEvent*)calloc(4,sizeof(SSEEvent));
size_t c=0;sse_parse_event((const uint8_t*)s,strlen(s),evs,&c,4);
if(c!=1){tf++;printf("FAIL\n");free(evs);return;}free(evs);tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: handshake ... ",tr);
SSEConnection conn;sse_connection_init(&conn,"/e");uint8_t b[512];sse_build_handshake(&conn,b,512);
if(!strstr((char*)b,"Accept: text/event-stream")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t4(void){tr++;printf("  TEST %d: lifecycle ... ",tr);
SSEConnection conn;sse_connection_init(&conn,"/s");
if(conn.state!=SSE_STATE_CONNECTING||!sse_should_reconnect(&conn)){tf++;printf("FAIL\n");return;}
conn.state=SSE_STATE_OPEN;if(sse_should_reconnect(&conn)){tf++;printf("FAIL\n");return;}
sse_connection_close(&conn);if(conn.state!=SSE_STATE_CLOSED){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
int main(void){printf("--- SSE ---\n");t1();t2();t3();t4();
printf("  %d/%d passed\n",tp,tr);return tf>0?1:0;}
