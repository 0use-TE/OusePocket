// SPDX-License-Identifier: GPL-2.0-or-later
// Local loopback only; multiplayer deliberately excluded from this prototype.
#include "engine/qcommon/qcommon.h"
typedef struct {int size;unsigned char data[MAX_MSGLEN];} packet_t;
static struct {unsigned read,write;packet_t packets[4];} loop[2];
netadr_t net_local_adr={.type=NA_LOOPBACK};
void NET_Init(void){memset(loop,0,sizeof(loop));Cvar_Get("net_noudp","1",0);}
void NET_Shutdown(void){}
void NET_Config(qboolean multiplayer){(void)multiplayer;}
qboolean NET_CompareAdr(netadr_t a,netadr_t b){return a.type==b.type && a.type==NA_LOOPBACK;}
qboolean NET_CompareBaseAdr(netadr_t a,netadr_t b){return NET_CompareAdr(a,b);}
qboolean NET_IsLocalAddress(netadr_t a){return a.type==NA_LOOPBACK;}
char *NET_AdrToString(netadr_t a){(void)a;return "loopback";}
char *NET_BaseAdrToString(netadr_t a){return NET_AdrToString(a);}
qboolean NET_StringToAdr(char *s,netadr_t *a){if(strcmp(s,"localhost"))return false;memset(a,0,sizeof(*a));a->type=NA_LOOPBACK;return true;}
qboolean NET_GetPacket(netsrc_t sock,netadr_t *from,sizebuf_t *message){
    if(loop[sock].write-loop[sock].read>4)loop[sock].read=loop[sock].write-4;
    if(loop[sock].read==loop[sock].write)return false;
    packet_t *p=&loop[sock].packets[loop[sock].read++&3];
    if(p->size>message->maxsize)return false;
    memcpy(message->data,p->data,p->size);message->cursize=p->size;*from=net_local_adr;return true;
}
void NET_SendPacket(netsrc_t sock,int length,void *data,netadr_t to){
    if(to.type!=NA_LOOPBACK || length<0 || length>MAX_MSGLEN)return;
    packet_t *p=&loop[sock^1].packets[loop[sock^1].write++&3];memcpy(p->data,data,length);p->size=length;
}
void NET_Sleep(int milliseconds){(void)milliseconds;}
