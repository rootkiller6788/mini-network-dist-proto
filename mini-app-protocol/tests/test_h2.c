#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "http2_frames.h"
static void t1(void){tr++;printf("  TEST %d: frame build ... ",tr);
uint8_t b[128];size_t n=h2_frame_build(b,sizeof(b),H2_FRAME_DATA,H2_FLAG_END_STREAM,1,(const uint8_t*)"hello",5);
if(n!=14){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: frame parse ... ",tr);
uint8_t b[128];size_t n=h2_frame_build(b,sizeof(b),H2_FRAME_HEADERS,H2_FLAG_END_HEADERS,3,(const uint8_t*)"world",5);
H2FrameHeader h;const uint8_t*p;size_t pl;
if(h2_frame_parse(b,n,&h,&p,&pl)!=0||h.type!=H2_FRAME_HEADERS){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: HPACK ... ",tr);
H2Connection*c=(H2Connection*)calloc(1,sizeof(H2Connection));
H2HeaderBlock blk;memset(&blk,0,sizeof(blk));snprintf(blk.fields[0].name,128,":method");snprintf(blk.fields[0].value,256,"GET");blk.count=1;
uint8_t enc[4096];size_t w=0;h2_header_encode(c,&blk,enc,sizeof(enc),&w);free(c);
if(w==0){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t4(void){tr++;printf("  TEST %d: priority tree ... ",tr);
H2PriorityTree t;h2_priority_tree_init(&t);h2_priority_add(&t,1,0,200,0);h2_priority_add(&t,3,0,100,0);
uint32_t a[16];int ns=h2_priority_allocate_bandwidth(&t,0,1000,a,16);
if(ns<=0){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
int main(void){printf("--- HTTP/2 ---\n");t1();t2();t3();t4();printf("  %d/%d\n",tp,tr);return tf>0?1:0;}
