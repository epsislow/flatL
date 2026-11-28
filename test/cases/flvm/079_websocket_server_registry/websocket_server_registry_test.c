#include "flat_ws_server_registry.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
int main(void){puts("websocket-server-registry-ok");return 0;}
#else
#include <pthread.h>
#include <unistd.h>
typedef struct { FlatWsServerRegistry *registry; uint64_t listener; int result; } AcceptCtx;
static void *accept_worker(void *arg){AcceptCtx*c=(AcceptCtx*)arg;uint64_t peer=0;char err[128]={0};c->result=flat_ws_server_registry_accept(c->registry,c->listener,-1,&peer,err,sizeof err);if(c->result==0)flat_ws_server_registry_close_peer(c->registry,peer);return NULL;}
int main(void){FlatWsServerRegistry*r;uint64_t listener=0;uint16_t port=0;char err[256]={0};pthread_t thread;AcceptCtx ctx;
 r=flat_ws_server_registry_create();if(!r)return 2;
 if(flat_ws_server_registry_listen(r,"127.0.0.1",0,8,&listener,&port,err,sizeof err)||!listener||!port)return 3;
 ctx.registry=r;ctx.listener=listener;ctx.result=0;
 if(pthread_create(&thread,NULL,accept_worker,&ctx)!=0)return 4;
 usleep(50000);
 if(flat_ws_server_registry_close_listener(r,listener)!=0)return 5;
 pthread_join(thread,NULL);
 if(ctx.result==0)return 6;
 if(flat_ws_server_registry_close_listener(r,listener)==0)return 7;
 if(flat_ws_server_registry_destroy(r)!=0)return 8;
 puts("websocket-server-registry-ok");return 0;
}
#endif
