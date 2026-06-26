#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "websocket.h"
static void t1(void){tr++;printf("  TEST %d: handshake ... ",tr);
WSConnection cli;ws_handshake_client_init(&cli,"h","/c");
uint8_t rq[512];size_t rl=ws_handshake_build_client(&cli,rq,512);
WSConnection srv;ws_handshake_server_init(&srv);ws_handshake_parse_client(rq,rl,&srv);
uint8_t rs[512];size_t sl=ws_handshake_build_server(&srv,rs,512);
ws_handshake_parse_server(rs,sl,&cli);
if(!cli.handshake_done){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: frame encode/decode ... ",tr);
uint8_t b[256];size_t w=0;ws_send_text("hi",b,256,&w);
char r[256];size_t rc=0;ws_recv_text(b,w,r,256,&rc);
if(strcmp(r,"hi")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: ping ... ",tr);
uint8_t b[256];size_t w=0;ws_send_ping("ka",b,256,&w);
WSFrame f;size_t c=0;ws_frame_decode(b,w,&f,&c);
if(f.opcode!=(uint8_t)WS_OP_PING){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t4(void){tr++;printf("  TEST %d: SHA-1+Base64 ... ",tr);
WSConnection conn;ws_handshake_client_init(&conn,"t.com","/w");
char comb[256];snprintf(comb,256,"%s%s",conn.key,WS_GUID);
uint8_t hash[20];ws_sha1_hash((const uint8_t*)comb,strlen(comb),hash);
char b64[32];ws_base64_encode(hash,20,b64,32);
if(!strlen(b64)){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
int main(void){printf("--- WebSocket ---\n");t1();t2();t3();t4();
printf("  %d/%d passed\n",tp,tr);return tf>0?1:0;}
