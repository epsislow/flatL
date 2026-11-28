#include "flat_ws_server_transport.h"
#include "flat_ws_server_frames_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET WsFd;
#else
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int WsFd;
#endif
static void ferr(char *e,unsigned long c,const char *s){if(e&&c)snprintf(e,(size_t)c,"%s",s);}
static int waitfd(WsFd fd,int wr,long ms){
#ifdef _WIN32
 fd_set r,w;struct timeval tv,*pt=NULL;int x;FD_ZERO(&r);FD_ZERO(&w);if(wr)FD_SET(fd,&w);else FD_SET(fd,&r);if(ms>=0){tv.tv_sec=ms/1000;tv.tv_usec=(ms%1000)*1000;pt=&tv;}x=select(0,wr?NULL:&r,wr?&w:NULL,NULL,pt);return x>0?1:x;
#else
 struct pollfd p;int x;p.fd=fd;p.events=wr?POLLOUT:POLLIN;p.revents=0;do{x=poll(&p,1,ms<0?-1:(int)ms);}while(x<0&&errno==EINTR);if(x>0&&(p.revents&(POLLERR|POLLHUP|POLLNVAL)))return -1;return x;
#endif
}
static int readn(WsFd fd,unsigned char *p,size_t n,long ms){size_t o=0;while(o<n){int r=waitfd(fd,0,ms);if(r<=0)return r;
#ifdef _WIN32
 int k=recv(fd,(char*)p+o,(int)(n-o),0);
#else
 ssize_t k=recv(fd,p+o,n-o,0);
#endif
 if(k<=0) return -1;
 o+=(size_t)k;
 }
 return 1;
}
static int writen(WsFd fd,const unsigned char*p,size_t n){size_t o=0;while(o<n){
#ifdef _WIN32
 int k=send(fd,(const char*)p+o,(int)(n-o),0);
#else
 ssize_t k=send(fd,p+o,n-o,
#ifdef MSG_NOSIGNAL
 MSG_NOSIGNAL
#else
 0
#endif
 );
#endif
 if(k<=0) return -1;
 o+=(size_t)k;
 }
 return 0;
}
static int utf8ok(const unsigned char *s, size_t n);
static int closecode(unsigned c);
int flat_ws_server_peer_send(intptr_t peer,FlatWsServerOpcode op,const void*data,unsigned long len,char*err,unsigned long cap){
 unsigned char h[10];size_t hn=2;uint64_t n=len;WsFd fd=(WsFd)peer;
 if(peer==(intptr_t)-1||(!data&&len)||!(op==1||op==2||op==8||op==9||op==10)||((op&8)&&len>125)){
  ferr(err,cap,"invalid WebSocket send arguments");return -1;
 }
 if(op==FLAT_WS_SERVER_TEXT && !utf8ok((const unsigned char *)data,(size_t)len)){
  ferr(err,cap,"WebSocket text payload is not valid UTF-8");return -1;
 }
 if(op==FLAT_WS_SERVER_CLOSE && (len==1 || (len>=2 &&
    (!closecode(((unsigned)((const unsigned char *)data)[0]<<8)|((const unsigned char *)data)[1]) ||
     !utf8ok((const unsigned char *)data+2,(size_t)len-2))))){
  ferr(err,cap,"invalid WebSocket close payload");return -1;
 }
 h[0]=(unsigned char)(0x80|op);if(len<126)h[1]=(unsigned char)len;else if(len<=65535){h[1]=126;h[2]=(unsigned char)(len>>8);h[3]=(unsigned char)len;hn=4;}else{unsigned i;h[1]=127;hn=10;for(i=0;i<8;i++)h[2+i]=(unsigned char)(n>>(56-8*i));}
 if(writen(fd,h,hn)||(len&&writen(fd,(const unsigned char*)data,(size_t)len))){ferr(err,cap,"unable to write WebSocket frame");return -1;}if(err&&cap)err[0]=0;return 0;
}
static int utf8ok(const unsigned char*s,size_t n){size_t i=0;while(i<n){unsigned c=s[i++],need;uint32_t cp;if(c<128)continue;if(c>=0xc2&&c<=0xdf){need=1;cp=c&31;}else if(c>=0xe0&&c<=0xef){need=2;cp=c&15;}else if(c>=0xf0&&c<=0xf4){need=3;cp=c&7;}else return 0;if(i+need>n)return 0;while(need--){unsigned d=s[i++];if((d&0xc0)!=0x80)return 0;cp=(cp<<6)|(d&63);}if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||cp<0x80||(cp<0x800&&c>=0xe0)||(cp<0x10000&&c>=0xf0))return 0;}return 1;}
static int closecode(unsigned c){return c>=1000&&c<5000&&c!=1004&&c!=1005&&c!=1006&&c!=1015&&!(c>=1016&&c<3000);}
int flat_ws_server_peer_receive_internal(intptr_t peer,long ms,unsigned long max,FlatWsServerMessage*out,char*err,unsigned long cap,FlatMutex *send_mutex){
 WsFd fd=(WsFd)peer;unsigned char*buf=NULL;size_t used=0,alloc=0;int fragmented=0;unsigned char msgop=0;
 if(out) memset(out,0,sizeof *out);
 if(peer==(intptr_t)-1||!out||ms < -1||!max){ferr(err,cap,"invalid WebSocket receive arguments");return -1;}
 for(;;){unsigned char h[2],ext[8],mask[4],ctl[125];unsigned fin,op,masked;uint64_t len;size_t en=0,i;int r=readn(fd,h,2,ms);if(r==0){free(buf);return 1;}if(r<0)goto io;
 fin=h[0]>>7;op=h[0]&15;masked=h[1]>>7;len=h[1]&127;if((h[0]&0x70)||!masked||!(op==0||op==1||op==2||op==8||op==9||op==10))goto proto;
 if(len==126)en=2;else if(len==127)en=8;if(en){if(readn(fd,ext,en,ms)<=0)goto io;len=0;for(i=0;i<en;i++)len=(len<<8)|ext[i];if((en==2&&len<126)||(en==8&&(len<65536||(ext[0]&0x80))))goto proto;}
 if((op&8)&&(!fin||len>125))goto proto;
 if(len>SIZE_MAX)goto big;
 if(readn(fd,mask,4,ms)<=0)goto io;
 if(op&8){if(len&&readn(fd,ctl,(size_t)len,ms)<=0)goto io;for(i=0;i<(size_t)len;i++)ctl[i]^=mask[i&3];if(op==9){int send_rc;if(send_mutex)flat_mutex_lock(send_mutex);send_rc=flat_ws_server_peer_send(peer,FLAT_WS_SERVER_PONG,ctl,(unsigned long)len,err,cap);if(send_mutex)flat_mutex_unlock(send_mutex);if(send_rc)goto fail;continue;}if(op==10)continue;if(len==1)goto proto;if(len>=2){unsigned c=((unsigned)ctl[0]<<8)|ctl[1];if(!closecode(c)||!utf8ok(ctl+2,(size_t)len-2))goto proto;}if(send_mutex)flat_mutex_lock(send_mutex);{int send_rc=flat_ws_server_peer_send(peer,FLAT_WS_SERVER_CLOSE,ctl,(unsigned long)len,err,cap);if(send_mutex)flat_mutex_unlock(send_mutex);if(send_rc!=0)goto fail;}
 free(buf);if(err&&cap)err[0]=0;return 2;}
 if(op==0){if(!fragmented)goto proto;}else{if(fragmented||(op!=1&&op!=2))goto proto;msgop=(unsigned char)op;}
 if(len>max || len>(uint64_t)(max-used))goto big;
 if(used+(size_t)len>alloc){size_t need=used+(size_t)len,nc=alloc?alloc:256;unsigned char*np;if(nc>max)nc=max;while(nc<need){if(nc>max/2){nc=max;break;}nc*=2;}np=(unsigned char*)realloc(buf,nc);if(!np)goto oom;buf=np;alloc=nc;}
 if(len&&readn(fd,buf+used,(size_t)len,ms)<=0)goto io;
 for(i=0;i<(size_t)len;i++)buf[used+i]^=mask[i&3];
 used+=(size_t)len;
 if(!fin)fragmented=1;else{if(msgop==1&&!utf8ok(buf,used))goto proto;out->opcode=(FlatWsServerOpcode)msgop;out->data=buf;out->length=(unsigned long)used;if(err&&cap)err[0]=0;return 0;}
 }
proto:free(buf);ferr(err,cap,"invalid RFC 6455 WebSocket frame");return -1;
big:free(buf);ferr(err,cap,"WebSocket message exceeds configured limit");return -1;
oom:free(buf);ferr(err,cap,"out of memory");return -1;
io:free(buf);ferr(err,cap,"incomplete WebSocket frame");return -1;
fail:free(buf);return -1;
}
void flat_ws_server_message_free(FlatWsServerMessage*m){if(m){free(m->data);memset(m,0,sizeof *m);}}

int flat_ws_server_peer_receive(intptr_t peer,long ms,unsigned long max,FlatWsServerMessage*out,char*err,unsigned long cap){return flat_ws_server_peer_receive_internal(peer,ms,max,out,err,cap,NULL);}
