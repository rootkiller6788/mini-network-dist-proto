#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int tr=0,tp=0,tf=0;
#include "rest_api.h"
static void t1(void){tr++;printf("  TEST %d: router ... ",tr);
RESTRouter rt;rest_router_init(&rt);rest_register_route(&rt,"/a/{id}",REST_GET,NULL);
RESTResponse r;int rc=rest_dispatch(&rt,REST_GET,"/a/42",NULL,0,&r);
if(rc!=404){tf++;printf("FAIL\n");return;}
rc=rest_dispatch(&rt,REST_POST,"/x",NULL,0,&r);if(rc!=404){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t2(void){tr++;printf("  TEST %d: URI match ... ",tr);
RESTParam p[16];size_t c=0;
if(!rest_uri_match("/u/{id}","/u/42",p,&c,16)||c!=1||strcmp(p[0].value,"42")){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t3(void){tr++;printf("  TEST %d: URL parse ... ",tr);
char uri[256];RESTQueryString q;memset(&q,0,sizeof(q));
rest_url_parse("/s?q=t&p=1",uri,256,&q);
if(strcmp(uri,"/s")||q.count!=2){tf++;printf("FAIL\n");return;}tp++;printf("PASS\n");}
static void t4(void){tr++;printf("  TEST %d: middleware auth (L8) ... ",tr);
RESTMiddlewareChain ch;rest_middleware_chain_init(&ch);
RESTMiddlewareAuthCtx ax;memset(&ax,0,sizeof(ax));snprintf(ax.token,256,"t");
rest_middleware_use(&ch,rest_middleware_auth_basic,&ax);
RESTRequest rq;rest_request_init(&rq);rq.method=REST_GET;
snprintf(rq.headers[0].name,64,"Authorization");snprintf(rq.headers[0].value,256,"Bearer t");rq.header_count=1;
RESTResponse rs;rest_response_init(&rs);
if(rest_middleware_execute(&ch,&rq,&rs)!=0){tf++;printf("FAIL\n");rest_middleware_chain_free(&ch);return;}
RESTRequest br;rest_request_init(&br);br.method=REST_GET;RESTResponse brs;rest_response_init(&brs);
if(rest_middleware_execute(&ch,&br,&brs)!=401){tf++;printf("FAIL\n");rest_middleware_chain_free(&ch);return;}
rest_middleware_chain_free(&ch);tp++;printf("PASS\n");}
int main(void){printf("--- REST API ---\n");t1();t2();t3();t4();
printf("  %d/%d passed\n",tp,tr);return tf>0?1:0;}
