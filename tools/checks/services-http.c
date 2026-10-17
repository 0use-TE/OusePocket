#include "ouse_net.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "platform.h"
#include "lwip/sockets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static const char *input;
static unsigned offset,fragment,created,destroyed,reads;
static int64_t read_delay,dns_delay;
static bool cancelled,secure;
static unsigned char collected[32769];static size_t collected_size;
static char sent[6000];static size_t sent_size;
esp_tls_t *esp_tls_init(void){++created;return calloc(1,sizeof(esp_tls_t));}
int esp_tls_conn_destroy(esp_tls_t *tls){assert(tls);++destroyed;free(tls);return 0;}
int esp_crt_bundle_attach(void *config){(void)config;return ESP_OK;}
int esp_tls_conn_new_async(const char *host,int size,int port,const esp_tls_cfg_t *c,esp_tls_t *tls)
{assert(tls&&size>0&&host&&port>0&&c->non_block&&!c->skip_common_name&&c->timeout_ms<=50);secure=!c->is_plain_tcp;if(secure)assert(c->crt_bundle_attach==esp_crt_bundle_attach);test_clock+=dns_delay;return 1;}
int esp_tls_get_conn_sockfd(esp_tls_t *tls,int *fd){assert(tls);*fd=1;return ESP_OK;}
int select(int n,void *r,fd_set *w,void *e,struct timeval *t){(void)n;(void)r;(void)w;(void)e;(void)t;return 1;}
int getsockopt(int fd,int l,int o,void *value,socklen_t *size){assert(fd==1&&l==SOL_SOCKET&&o==SO_ERROR&&*size==sizeof(int));*(int*)value=0;return 0;}
int esp_tls_conn_write(esp_tls_t *t,const void *data,size_t size)
{assert(t);size_t n=size>17 ? 17 : size;assert(sent_size+n<sizeof(sent));memcpy(sent+sent_size,data,n);sent_size+=n;sent[sent_size]=0;return (int)n;}
int esp_tls_conn_read(esp_tls_t *t,void *data,size_t size)
{assert(t);++reads;test_clock+=read_delay;size_t n=strlen(input)-offset;if(n>fragment)n=fragment;if(n>size)n=size;memcpy(data,input+offset,n);offset+=n;return (int)n;}
static bool stopped(void *p){(void)p;return cancelled;}
static esp_err_t sink(void *p,const unsigned char *bytes,size_t n)
{(void)p;assert(collected_size+n<=32768);memcpy(collected+collected_size,bytes,n);collected_size+=n;return ESP_OK;}
static void reset(const char *response)
{input=response;offset=reads=sent_size=collected_size=0;test_clock=read_delay=dns_delay=0;cancelled=false;fragment=7;}
static esp_err_t transfer(ouse_http_request_t *q,size_t limit,ouse_http_response_t *r)
{unsigned before=destroyed;esp_err_t e=ouse_http_transfer(q,test_clock+1000000,limit,r,sink,stopped,NULL);assert(destroyed==before+1 && created==destroyed);return e;}
int main(void)
{
    ouse_http_request_t q={.url="https://example.test/path?q=1",.method="GET",.timeout_ms=1000};ouse_http_response_t r;
    reset("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello");
    assert(transfer(&q,32768,&r)==ESP_OK&&secure&&r.status==200&&r.body_size==5&&!memcmp(collected,"hello",5));
    assert(strstr(sent,"GET /path?q=1 HTTP/1.1\r\n")&&strstr(sent,"Host: example.test:443\r\n")&&strstr(sent,"Accept-Encoding: identity"));
    reset("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\nX-End: yes\r\n\r\n");
    fragment=1;assert(transfer(&q,5,&r)==ESP_OK&&r.status==200&&collected_size==5&&r.header_count==2);
    reset("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: /next\r\n\r\n");assert(transfer(&q,64,&r)==ESP_OK&&r.status==302&&reads>1);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n");assert(transfer(&q,8,&r)==ESP_ERR_INVALID_SIZE);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort");assert(transfer(&q,64,&r)!=ESP_OK);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 1\r\n\r\n");assert(transfer(&q,64,&r)!=ESP_OK);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Encoding: gzip\r\n\r\n");assert(transfer(&q,64,&r)==ESP_ERR_NOT_SUPPORTED);
    reset("HTTP/1.1 200 OK\r\n\r\neof-body");assert(transfer(&q,64,&r)==ESP_OK&&collected_size==8);
    q.method="HEAD";reset("HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n");assert(transfer(&q,64,&r)==ESP_OK&&r.body_size==0);q.method="GET";
    reset("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");fragment=1;read_delay=100000;assert(transfer(&q,64,&r)==ESP_ERR_TIMEOUT&&reads<12);
    reset("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");dns_delay=1000001;assert(transfer(&q,64,&r)==ESP_ERR_TIMEOUT&&reads==0);
    reset("");cancelled=true;unsigned before=created;assert(ouse_http_transfer(&q,1000000,64,&r,sink,stopped,NULL)==ESP_ERR_INVALID_STATE&&created==before);
    q.url="http://example.test/";reset("HTTP/1.1 204 No Content\r\n\r\n");assert(transfer(&q,64,&r)==ESP_OK&&!secure);
    const char *bad[]={"file:///x","http://u:p@example.test/","http://example.test/#secret","http://example.test/\r\nx","http://example.test:0/"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){q.url=bad[i];assert(ouse_http_validate(&q)==ESP_ERR_INVALID_ARG);}
    q.url="https://example.test/";ouse_http_header_t h={.name="Host",.value="other"};q.headers=&h;q.header_count=1;assert(ouse_http_validate(&q)==ESP_ERR_INVALID_ARG);
    strcpy(h.name,"Authorization");strcpy(h.value,"x\r\nInjected: yes");assert(ouse_http_validate(&q)==ESP_ERR_INVALID_ARG);
    puts("PASS HTTP: production framing, fragment/chunk/trailer/HEAD/EOF, truncation, limits, whole deadline, DNS overrun cleanup, cancellation, TLS config, no redirect/injection");
}
