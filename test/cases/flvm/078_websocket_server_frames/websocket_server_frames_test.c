#include "flat_ws_server_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
int main(void) { puts("websocket-server-frames-ok"); return 0; }
#else
#include <sys/socket.h>
#include <unistd.h>
static int client_frame(int fd,unsigned op,int fin,const unsigned char*p,size_t n,unsigned seed){
 unsigned char h[4]={0},mask[4],*b;size_t i,hn=2;h[0]=(unsigned char)((fin?0x80:0)|op);if(n<126)h[1]=(unsigned char)(0x80|n);else if(n<=65535){h[1]=0xfe;h[2]=(unsigned char)(n>>8);h[3]=(unsigned char)n;hn=4;}else return -1;
 mask[0]=(unsigned char)seed;mask[1]=(unsigned char)(seed+1);mask[2]=(unsigned char)(seed+2);mask[3]=(unsigned char)(seed+3);
 if(write(fd,h,hn)!=(ssize_t)hn||write(fd,mask,4)!=4) return -1;
 b=(unsigned char*)malloc(n?n:1);
 if(!b) return -1;
 for(i=0;i<n;i++) b[i]=p[i]^mask[i&3];
 i=n?(size_t)write(fd,b,n):0;
 free(b);
 return n?(i==n?0:-1):0;
}
int main(void){int s[2],r;char err[256]={0};FlatWsServerMessage m;unsigned char pong[32],payload[130];ssize_t n;size_t i;
 if(socketpair(AF_UNIX,SOCK_STREAM,0,s))return 2;
 if(client_frame(s[1],1,0,(const unsigned char*)"hel",3,7)||client_frame(s[1],9,1,(const unsigned char*)"?",1,11)||client_frame(s[1],0,1,(const unsigned char*)"lo",2,15))return 3;
 r=flat_ws_server_peer_receive(s[0],1000,1024,&m,err,sizeof err);if(r!=0||m.opcode!=FLAT_WS_SERVER_TEXT||m.length!=5||memcmp(m.data,"hello",5))return 4;flat_ws_server_message_free(&m);
 n=read(s[1],pong,sizeof pong);if(n!=3||pong[0]!=0x8a||pong[1]!=1||pong[2]!='?')return 5;
 for(i=0;i<sizeof payload;i++)payload[i]=(unsigned char)i;
 if(client_frame(s[1],2,1,payload,sizeof payload,21))return 6;
 r=flat_ws_server_peer_receive(s[0],1000,1024,&m,err,sizeof err);if(r!=0||m.opcode!=FLAT_WS_SERVER_BINARY||m.length!=sizeof payload||memcmp(m.data,payload,sizeof payload))return 7;flat_ws_server_message_free(&m);
 if(flat_ws_server_peer_send(s[0],FLAT_WS_SERVER_BINARY,payload,sizeof payload,err,sizeof err))return 8;
 {unsigned char head[4];if(read(s[1],head,4)!=4||head[0]!=0x82||head[1]!=126||head[2]!=0||head[3]!=130)return 9;}
 close(s[0]);close(s[1]);puts("websocket-server-frames-ok");return 0;
}
#endif
