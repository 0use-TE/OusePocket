// SPDX-License-Identifier: MIT
// Keep framing independent of esp_http_client's blocking fetch_headers loop:
// a continuously trickling peer must not reset the whole-request deadline.
#include "ouse_net.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "http_parser.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    http_parser parser;
    ouse_http_response_t *response;
    ouse_http_sink_t sink;
    void *context;
    size_t limit, header_bytes, field_size, value_size;
    char field[64], value[256];
    bool in_value, done, head;
    esp_err_t error;
} decoder_t;
static bool token(const char *s)
{
    if(!*s)return false;
    for(;*s;++s){unsigned char c=*s;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("!#$%&'*+-.^_`|~",c)))return false;
    }
    return true;
}
static bool safe_value(const char *s)
{
    for(;*s;++s)if((unsigned char)*s<32 || (unsigned char)*s==127)return false;
    return true;
}
static bool reserved(const char *s)
{
    const char *names[]={"host","content-length","transfer-encoding","connection","accept-encoding","expect","upgrade","trailer","proxy-authorization","proxy-connection","te"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!strcasecmp(s,names[i]))return true;
    return false;
}
static bool parse_url(const char *url,struct http_parser_url *parts)
{
    size_t n=strnlen(url,OUSE_NET_URL_MAX+1);
    if(!n || n>OUSE_NET_URL_MAX)return false;
    for(size_t i=0;i<n;++i)if((unsigned char)url[i]<=32 || (unsigned char)url[i]>=127 || url[i]=='\\')return false;
    if(http_parser_parse_url(url,n,0,parts))return false;
    if(!(parts->field_set&(1<<UF_HOST)) || (parts->field_set&((1<<UF_USERINFO)|(1<<UF_FRAGMENT))))return false;
    bool tls=!strncmp(url,"https://",8),plain=!strncmp(url,"http://",7);
    return (tls||plain) && parts->field_data[UF_HOST].len<=253 &&
        (!(parts->field_set&(1<<UF_PORT)) || parts->port>0);
}
esp_err_t ouse_http_validate(const ouse_http_request_t *r)
{
    if(!r || !r->url || !r->method || r->timeout_ms<1000 || r->timeout_ms>30000 ||
       r->body_size>OUSE_NET_BODY_MAX || (r->body_size && !r->body) || r->header_count>OUSE_NET_HEADERS_MAX ||
       (r->header_count && !r->headers))return ESP_ERR_INVALID_ARG;
    const char *methods[]={"GET","HEAD","POST","PUT","PATCH","DELETE"};bool method=false;
    for(unsigned i=0;i<sizeof(methods)/sizeof(methods[0]);++i)if(!strcmp(r->method,methods[i]))method=true;
    struct http_parser_url parts={0};if(!method || !parse_url(r->url,&parts) ||
       (r->body_size && (!strcmp(r->method,"GET") || !strcmp(r->method,"HEAD"))))return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<r->header_count;++i){const ouse_http_header_t *h=&r->headers[i];
        if(strnlen(h->name,sizeof(h->name))>=sizeof(h->name) || strnlen(h->value,sizeof(h->value))>=sizeof(h->value) ||
           !token(h->name) || !safe_value(h->value) || reserved(h->name))return ESP_ERR_INVALID_ARG;
        for(unsigned j=0;j<i;++j)if(!strcasecmp(h->name,r->headers[j].name))return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
static int fail(decoder_t *d,esp_err_t error){d->error=error;return -1;}
static int flush_header(decoder_t *d)
{
    if(!d->field_size)return 0;
    if(!d->in_value || d->response->header_count==OUSE_NET_HEADERS_MAX)return fail(d,ESP_ERR_INVALID_SIZE);
    ouse_http_header_t *h=&d->response->headers[d->response->header_count++];
    memcpy(h->name,d->field,d->field_size);h->name[d->field_size]=0;
    memcpy(h->value,d->value,d->value_size);h->value[d->value_size]=0;
    d->field_size=d->value_size=0;d->in_value=false;return 0;
}
static int field(http_parser *p,const char *data,size_t size)
{
    decoder_t *d=p->data;if(d->in_value && flush_header(d))return -1;
    if(size>=sizeof(d->field)-d->field_size || (d->header_bytes+=size)>4096)return fail(d,ESP_ERR_INVALID_SIZE);
    memcpy(d->field+d->field_size,data,size);d->field_size+=size;return 0;
}
static int value(http_parser *p,const char *data,size_t size)
{
    decoder_t *d=p->data;d->in_value=true;
    if(size>=sizeof(d->value)-d->value_size || (d->header_bytes+=size)>4096)return fail(d,ESP_ERR_INVALID_SIZE);
    memcpy(d->value+d->value_size,data,size);d->value_size+=size;return 0;
}
static int headers(http_parser *p)
{
    decoder_t *d=p->data;if(flush_header(d))return -1;
    if(p->status_code==101 || p->upgrade)return fail(d,ESP_ERR_NOT_SUPPORTED);
    if(p->status_code<200){d->response->header_count=0;return 0;}
    d->response->status=p->status_code;
    for(unsigned i=0;i<d->response->header_count;++i){ouse_http_header_t *h=&d->response->headers[i];
        if(!strcasecmp(h->name,"Content-Encoding") && strcasecmp(h->value,"identity"))return fail(d,ESP_ERR_NOT_SUPPORTED);
    }
    if(!d->head && p->content_length!=UINT64_MAX && p->content_length>d->limit)return fail(d,ESP_ERR_INVALID_SIZE);
    return d->head ? 1 : 0;
}
static int body(http_parser *p,const char *data,size_t size)
{
    decoder_t *d=p->data;
    if(size>d->limit-d->response->body_size)return fail(d,ESP_ERR_INVALID_SIZE);
    if(d->sink){esp_err_t error=d->sink(d->context,(const unsigned char *)data,size);if(error!=ESP_OK)return fail(d,error);}
    d->response->body_size+=size;return 0;
}
static int complete(http_parser *p)
{
    decoder_t *d=p->data;
    if(flush_header(d))return -1;
    if(p->status_code>=200){d->done=true;http_parser_pause(p,1);}
    return 0;
}
static const http_parser_settings callbacks={.on_header_field=field,.on_header_value=value,
    .on_headers_complete=headers,.on_body=body,.on_message_complete=complete};
static esp_err_t checkpoint(int64_t deadline,ouse_http_stop_t stop,void *context)
{
    if(stop && stop(context))return ESP_ERR_INVALID_STATE;
    return esp_timer_get_time()>=deadline ? ESP_ERR_TIMEOUT : ESP_OK;
}
static bool again(int result)
{return result==ESP_TLS_ERR_SSL_WANT_READ || result==ESP_TLS_ERR_SSL_WANT_WRITE || (result==-1 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR));}
static esp_err_t send_bytes(esp_tls_t *tls,const void *data,size_t size,int64_t deadline,ouse_http_stop_t stop,void *context)
{
    const unsigned char *p=data;
    while(size){esp_err_t error=checkpoint(deadline,stop,context);if(error!=ESP_OK)return error;
        errno=0;int n=esp_tls_conn_write(tls,p,size);
        if(n>0){p+=n;size-=n;}else if(!again(n))return ESP_FAIL;
        vTaskDelay(1);
    }
    return ESP_OK;
}
esp_err_t ouse_http_transfer(const ouse_http_request_t *r,int64_t deadline,size_t limit,
    ouse_http_response_t *response,ouse_http_sink_t sink,ouse_http_stop_t stop,void *context)
{
    esp_err_t error=ouse_http_validate(r);if(error!=ESP_OK || !response || !limit || limit>128*1024*1024)return ESP_ERR_INVALID_ARG;
    memset(response,0,sizeof(*response));error=checkpoint(deadline,stop,context);if(error!=ESP_OK)return error;
    int64_t phase_deadline=esp_timer_get_time()+(int64_t)r->timeout_ms*1000;
    if(phase_deadline>deadline)phase_deadline=deadline;
    struct http_parser_url parts={0};(void)parse_url(r->url,&parts);
    char host[254];size_t host_size=parts.field_data[UF_HOST].len;
    memcpy(host,r->url+parts.field_data[UF_HOST].off,host_size);host[host_size]=0;
    bool secure=!strncmp(r->url,"https://",8);
    int port=(parts.field_set&(1<<UF_PORT)) ? parts.port : secure ? 443 : 80;
    // A short transport wait and nonblocking reads preserve a whole-transfer
    // deadline, including header/chunk trickling. DNS is the SDK exception.
    esp_tls_cfg_t config={.non_block=true,.timeout_ms=50,.is_plain_tcp=!secure,
        .crt_bundle_attach=secure ? esp_crt_bundle_attach : NULL};
    esp_tls_t *tls=esp_tls_init();if(!tls)return ESP_ERR_NO_MEM;
    char *request=malloc(5120);decoder_t *d=calloc(1,sizeof(*d));
    if(!request || !d){error=ESP_ERR_NO_MEM;goto done;}
    while((error=checkpoint(phase_deadline,stop,context))==ESP_OK){
        int connected=esp_tls_conn_new_async(host,host_size,port,&config,tls);
        if(connected==1)break;
        if(connected<0){error=ESP_FAIL;break;}
        vTaskDelay(1);
    }
    if(error!=ESP_OK)goto done;
    // Fixed IDF's plain-TCP async path returns before the nonblocking connect
    // is established. Explicitly await writability and SO_ERROR; otherwise a
    // normal pending connect could be misreported as a send failure.
    int socket_fd=-1;
    if(esp_tls_get_conn_sockfd(tls,&socket_fd)!=ESP_OK || socket_fd<0){error=ESP_FAIL;goto done;}
    for(;;){
        error=checkpoint(phase_deadline,stop,context);if(error!=ESP_OK)goto done;
        fd_set writes;FD_ZERO(&writes);FD_SET(socket_fd,&writes);struct timeval timeout={.tv_usec=50000};
        int ready=select(socket_fd+1,NULL,&writes,NULL,&timeout);
        if(ready<0){if(errno==EINTR)continue;error=ESP_FAIL;goto done;}
        if(!ready)continue;
        int socket_error=0;socklen_t length=sizeof(socket_error);
        if(getsockopt(socket_fd,SOL_SOCKET,SO_ERROR,&socket_error,&length)!=0 || socket_error){error=ESP_FAIL;goto done;}
        break;
    }
    // Host syntax preserves IPv6 brackets and explicit nondefault ports.
    bool ipv6=strchr(host,':')!=NULL;char authority[272];
    snprintf(authority,sizeof(authority),ipv6 ? "[%s]:%d" : "%s:%d",host,port);
    const char *path="/";size_t path_size=1;
    if(parts.field_set&(1<<UF_PATH)){path=r->url+parts.field_data[UF_PATH].off;path_size=parts.field_data[UF_PATH].len;}
    int length=snprintf(request,5120,"%s %.*s",r->method,(int)path_size,path);
    if(parts.field_set&(1<<UF_QUERY))length+=snprintf(request+length,5120-length,"?%.*s",parts.field_data[UF_QUERY].len,r->url+parts.field_data[UF_QUERY].off);
    length+=snprintf(request+length,5120-length," HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept-Encoding: identity\r\nContent-Length: %u\r\n",authority,(unsigned)r->body_size);
    for(unsigned i=0;i<r->header_count;++i)length+=snprintf(request+length,5120-length,"%s: %s\r\n",r->headers[i].name,r->headers[i].value);
    length+=snprintf(request+length,5120-length,"\r\n");
    if(length<0 || length>=5120){error=ESP_ERR_INVALID_SIZE;goto done;}
    error=send_bytes(tls,request,length,phase_deadline,stop,context);
    if(error==ESP_OK && r->body_size)error=send_bytes(tls,r->body,r->body_size,phase_deadline,stop,context);
    if(error!=ESP_OK)goto done;
    d->response=response;d->sink=sink;d->context=context;d->limit=limit;d->head=!strcmp(r->method,"HEAD");
    http_parser_init(&d->parser,HTTP_RESPONSE);d->parser.data=d;
    unsigned char bytes[1024];size_t wire=0;
    while(!d->done && (error=checkpoint(deadline,stop,context))==ESP_OK){
        if(esp_timer_get_time()>=phase_deadline){error=ESP_ERR_TIMEOUT;break;}
        errno=0;int count=esp_tls_conn_read(tls,bytes,sizeof(bytes));
        if(count<0){if(again(count)){vTaskDelay(1);continue;}error=ESP_FAIL;break;}
        wire+=count;
        if(wire>limit+65536){error=ESP_ERR_INVALID_SIZE;break;}
        size_t parsed=http_parser_execute(&d->parser,&callbacks,(const char *)bytes,count);
        // Native streaming consumers may apply backpressure (pause / alarm).
        // Sink time is governed by the total deadline, not network idle time.
        if(response->status && count>0){phase_deadline=esp_timer_get_time()+(int64_t)r->timeout_ms*1000;if(phase_deadline>deadline)phase_deadline=deadline;}
        if(d->error!=ESP_OK){error=d->error;break;}
        if(d->done)break;
        if(parsed!=(size_t)count || HTTP_PARSER_ERRNO(&d->parser)!=HPE_OK || !count){error=ESP_FAIL;break;}
        vTaskDelay(1);
    }
    // An expired/cancelled operation must not publish a successful payload just
    // because its last read crossed the deadline or its native sink took time.
    if(error==ESP_OK)error=checkpoint(deadline,stop,context);
done:
    esp_tls_conn_destroy(tls);free(request);free(d);return error;
}
