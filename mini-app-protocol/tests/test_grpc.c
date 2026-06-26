#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "grpc_proto.h"
static void t1(void){tr++;printf("  TEST %d: message encode/decode ... ",tr);
uint8_t b[256];size_t tot=grpc_encode_message((const uint8_t*)"x",1,0,b,256);
if(tot!=6){tf++;printf("FAIL\n");return;}
uint8_t*dp;size_t dl;bool cm;
if(grpc_decode_message(b,tot,&dp,&dl,&cm)!=0||cm!=0){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: KV serialize ... ",tr);
uint8_t b[256];size_t w=0;grpc_kv_serialize("k","v",b,256,&w);
char k[32],v[32];grpc_kv_deserialize(b,w,k,32,v,32);
if(strcmp(k,"k")||strcmp(v,"v")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: server method ... ",tr);
GRPCService svc;memset(&svc,0,sizeof(svc));snprintf(svc.name,128,"Svc");
grpc_service_add_method(&svc,"M","Rq","Rs",GRPC_UNARY);
GRPCServer srv;grpc_server_init(&srv);grpc_server_register(&srv,&svc);
if(!grpc_server_find_method(&srv,"Svc","M")){tf++;printf("FAIL\n");return;}
if(grpc_server_find_method(&srv,"Svc","X")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
int main(void){printf("--- gRPC ---\n");t1();t2();t3();
printf("  %d/%d passed\n",tp,tr);return tf>0?1:0;}
