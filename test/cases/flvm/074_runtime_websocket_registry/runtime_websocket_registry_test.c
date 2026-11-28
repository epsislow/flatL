#include "flat_runtime.h"
#include "flat_runtime_event.h"
#include "flat_runtime_socket_event.h"
#include "flat_runtime_socket.h"
#include "flat_ws_transport.h"
#include "flat_value.h"
#include "flat_number.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_TIMEOUT_MS 3000

typedef struct TestServer { int listen_fd; int port; pthread_t thread; int failed; } TestServer;

static uint32_t rol(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }
static void sha1(const unsigned char *data, size_t len, unsigned char out[20]) {
    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
    size_t total = ((len + 9 + 63) / 64) * 64, off;
    unsigned char *msg = (unsigned char *)calloc(total, 1);
    if (!msg) { memset(out, 0, 20); return; }
    memcpy(msg, data, len); msg[len] = 0x80;
    { uint64_t bits = (uint64_t)len * 8; int i; for (i=0;i<8;i++) msg[total-1-i]=(unsigned char)(bits>>(i*8)); }
    for (off=0; off<total; off+=64) {
        uint32_t w[80],a,b,c,d,e; int i;
        for(i=0;i<16;i++) w[i]=((uint32_t)msg[off+i*4]<<24)|((uint32_t)msg[off+i*4+1]<<16)|((uint32_t)msg[off+i*4+2]<<8)|msg[off+i*4+3];
        for(i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        a=h0;b=h1;c=h2;d=h3;e=h4;
        for(i=0;i<80;i++) { uint32_t f,k,t; if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;} else if(i<40){f=b^c^d;k=0x6ED9EBA1;} else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;} else {f=b^c^d;k=0xCA62C1D6;} t=rol(a,5)+f+e+k+w[i];e=d;d=c;c=rol(b,30);b=a;a=t; }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    free(msg);
    { uint32_t h[5]={h0,h1,h2,h3,h4}; int i,j; for(i=0;i<5;i++) for(j=0;j<4;j++) out[i*4+j]=(unsigned char)(h[i]>>(24-j*8)); }
}
static void base64(const unsigned char *in, size_t n, char out[32]) {
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; size_t i=0,o=0;
    while(i<n){ size_t rem=n-i; unsigned a=in[i++], b=rem>1?in[i++]:0, c=rem>2?in[i++]:0; out[o++]=t[a>>2]; out[o++]=t[((a&3)<<4)|(b>>4)]; out[o++]=rem>1?t[((b&15)<<2)|(c>>6)]:'='; out[o++]=rem>2?t[c&63]:'='; }
    out[o]='\0';
}
static int send_all(int fd,const void *data,size_t len){const char*p=data;while(len){ssize_t n=send(fd,p,len,0);if(n<=0)return -1;p+=n;len-=(size_t)n;}return 0;}
static void *server_main(void *arg) {
    TestServer *s=(TestServer*)arg; int fd; fd=accept(s->listen_fd,NULL,NULL); char req[4096], key[128]={0}, response[512], accept_src[256], accept_value[32]; size_t used=0; ssize_t n;
    if(fd<0){perror("accept");s->failed=1;return NULL;}
    while(used<sizeof(req)-1){n=recv(fd,req+used,sizeof(req)-1-used,0);if(n<=0){s->failed=1;goto done;}used+=(size_t)n;req[used]='\0';if(strstr(req,"\r\n\r\n"))break;}
    { char *p=strstr(req,"Sec-WebSocket-Key:"); if(!p){s->failed=1;goto done;} p+=strlen("Sec-WebSocket-Key:"); while(*p==' ')p++; {size_t i=0;while(*p&&*p!='\r'&&i<sizeof(key)-1)key[i++]=*p++;key[i]='\0';} }
    snprintf(accept_src,sizeof accept_src,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    { unsigned char digest[20]; sha1((const unsigned char*)accept_src,strlen(accept_src),digest); base64(digest,20,accept_value); }
    snprintf(response,sizeof response,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept_value);
    if(send_all(fd,response,strlen(response))!=0 || send_all(fd,"\x81\x02hi\x88\x00",6)!=0)s->failed=1;
    usleep(100000);
done: close(fd); return NULL;
}
static int wait_event(FlatRuntime *rt, FlatEventSubscriptionId sub, FlatCollectionPool *coll, FlatStringPool *pool, FlatValue *out) {
    int i; for(i=0;i<TEST_TIMEOUT_MS/10;i++){int rc=flat_runtime_event_next(rt,sub,coll,pool,NULL,out);if(rc!=0)return rc;usleep(10000);}return 0;
}
int main(void) {
    int server_started = 0;
    TestServer server; struct sockaddr_in addr; socklen_t alen=sizeof addr; FlatRuntime *rt=NULL; FlatCollectionPool *coll=NULL; FlatStringPool *pool=NULL; FlatEventSubscriptionId open_sub=0, recv_sub=0, close_sub=0; uint64_t socket_id=0; FlatValue v, field; char url[128], error[256]={0}; int rc=1;
    memset(&server,0,sizeof server); memset(&v,0,sizeof v); memset(&field,0,sizeof field);
    if (!flat_runtime_ws_supported()) {
        rt = flat_runtime_create(NULL);
        if (!rt) goto done;
        if (flat_runtime_ws_connect(rt, "ws://127.0.0.1:1/", 500, 4096,
                                &socket_id, error, sizeof error) == 0 ||
            socket_id != 0 || strstr(error, "ws/wss") == NULL) goto done;
        if (flat_runtime_destroy(rt) != 0) goto done;
        rt = NULL;
        puts("runtime-websocket-registry-validated");
        rc = 0;
        goto done;
    }
    server.listen_fd=socket(AF_INET,SOCK_STREAM,0); if(server.listen_fd<0)goto done;
    memset(&addr,0,sizeof addr);addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    if(bind(server.listen_fd,(struct sockaddr*)&addr,sizeof addr)!=0||listen(server.listen_fd,1)!=0||getsockname(server.listen_fd,(struct sockaddr*)&addr,&alen)!=0)goto done;
    server.port=ntohs(addr.sin_port); if(pthread_create(&server.thread,NULL,server_main,&server)!=0)goto done; server_started=1;
    rt=flat_runtime_create(NULL);coll=flat_coll_create();pool=flat_pool_create();if(!rt||!coll||!pool)goto done;
    if(flat_runtime_event_subscribe(rt,1,"socket","open",&open_sub)!=0||flat_runtime_event_subscribe(rt,1,"socket","receive",&recv_sub)!=0||flat_runtime_event_subscribe(rt,1,"socket","close",&close_sub)!=0)goto done;
    snprintf(url,sizeof url,"ws://127.0.0.1:%d/echo",server.port);
    if(flat_runtime_ws_connect(rt,url,2000,4096,&socket_id,error,sizeof error)!=0 || socket_id == 0 || flat_runtime_socket_count(rt) != 1)goto done;
    if(wait_event(rt,open_sub,coll,pool,&v)!=1||v.tag!=VAL_NULL)goto done;
    flat_value_release(coll,pool,&v);memset(&v,0,sizeof v);
    if(wait_event(rt,recv_sub,coll,pool,&v)!=1||v.tag!=VAL_OBJECT)goto done;
    if(flat_obj_get(coll,v.u.coll_id,"opcode",&field)!=0||field.tag!=VAL_NUMBER||flat_number_as_f64(&field)!=FLAT_WS_TEXT)goto done;
    flat_value_release(coll,pool,&field);memset(&field,0,sizeof field);
    if(flat_obj_get(coll,v.u.coll_id,"data",&field)!=0||field.tag!=VAL_STR_REF)goto done;
    {size_t len=0;const unsigned char *p=flat_pool_read(pool,field.u.ref_id,&len);if(!p||len!=2||memcmp(p,"hi",2)!=0)goto done;}
    flat_value_release(coll,pool,&field);memset(&field,0,sizeof field);flat_value_release(coll,pool,&v);memset(&v,0,sizeof v);
    if(wait_event(rt,close_sub,coll,pool,&v)!=1||v.tag!=VAL_NULL)goto done;
    flat_value_release(coll,pool,&v);memset(&v,0,sizeof v);
    pthread_join(server.thread,NULL);server_started=0;close(server.listen_fd);server.listen_fd=-1;
    if(server.failed)goto done;
    if(flat_runtime_event_unsubscribe(rt,open_sub)!=0||flat_runtime_event_unsubscribe(rt,recv_sub)!=0||flat_runtime_event_unsubscribe(rt,close_sub)!=0)goto done;
    open_sub=recv_sub=close_sub=0;
    if(flat_runtime_destroy(rt)==0)goto done; /* live socket must keep runtime alive */
    if(flat_runtime_ws_close(rt,socket_id)!=0 || flat_runtime_socket_count(rt) != 0)goto done;
    socket_id=0;
    if(flat_runtime_destroy(rt)!=0)goto done;
    rt=NULL;
    puts("runtime-websocket-registry-validated");rc=0;
done:
    if(socket_id && rt) (void)flat_runtime_ws_close(rt,socket_id);
    if(server.listen_fd>=0)close(server.listen_fd);
    if(server_started) pthread_join(server.thread,NULL);
    flat_value_release(coll,pool,&field);flat_value_release(coll,pool,&v);
    if(rt){if(open_sub)flat_runtime_event_unsubscribe(rt,open_sub);if(recv_sub)flat_runtime_event_unsubscribe(rt,recv_sub);if(close_sub)flat_runtime_event_unsubscribe(rt,close_sub);flat_runtime_destroy(rt);}
    if(coll)flat_coll_destroy(coll);
    if(pool)flat_pool_destroy(pool);
    if(rc)fprintf(stderr,"runtime WebSocket registry failed: %s\n",error);
    return rc;
}
