// SPDX-License-Identifier: MIT
#include "ouse_net.h"
#include "ouse_channels.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    ouse_http_request_t request;
    char url[OUSE_NET_URL_MAX+1],method[8];
    ouse_http_header_t headers[OUSE_NET_HEADERS_MAX];
    unsigned char *body;
    uint32_t id;
    int64_t deadline;
    ouse_net_complete_t complete;
    void *context;
    ouse_net_result_t *result;
} job_t;
static QueueHandle_t requests;
bool ouse_net_available(void){return requests!=NULL;}
static bool stop(void *context){return ouse_task_cancel_requested(((job_t *)context)->id);}
static esp_err_t collect(void *context,const unsigned char *data,size_t size)
{
    job_t *job=context;
    memcpy(job->result->body+job->result->response.body_size,data,size);return ESP_OK;
}
void ouse_net_result_free(ouse_net_result_t *result)
{if(result){free(result->body);free(result);}}
static void worker(void *unused)
{
    (void)unused;job_t *job;
    for(;;){
        if(xQueueReceive(requests,&job,portMAX_DELAY)!=pdTRUE)continue;
        ouse_net_result_t *result=job->result;
        bool claimed=ouse_task_start(job->id,true);
        result->error=claimed ? ouse_http_transfer(&job->request,job->deadline,OUSE_NET_RESPONSE_MAX,
            &result->response,collect,stop,job) : ESP_OK;
        result->cancelled=(!claimed || (result->error==ESP_ERR_INVALID_STATE && stop(job)));
        if(result->cancelled)result->error=ESP_OK;
        // The buffer is for the Promise channel only, never task history.
        if(result->body)result->body[result->response.body_size]=0;
        ouse_task_result_t summary={.present=true,.value.network={.status=result->response.status,.body_bytes=result->response.body_size}};
        ouse_task_finish_result(job->id,result->error,result->cancelled,&summary);
        job->complete(job->context,job->id,result);free(job->body);free(job);
    }
}
esp_err_t ouse_net_init(void)
{
    esp_err_t channel_error=ouse_channels_init();if(channel_error!=ESP_OK)return channel_error;
    if(requests)return ESP_ERR_INVALID_STATE;
    requests=xQueueCreate(OUSE_NET_PENDING_MAX,sizeof(job_t *));if(!requests)return ESP_ERR_NO_MEM;
    if(xTaskCreate(worker,"ouse_net",12288,NULL,2,NULL)!=pdPASS){vQueueDelete(requests);requests=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t ouse_net_submit(const ouse_task_owner_t *owner,const ouse_http_request_t *request,
    uint32_t *id,ouse_net_complete_t complete,void *context)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    *id=0;
    esp_err_t error=ouse_http_validate(request);if(error!=ESP_OK || !owner || !complete)return ESP_ERR_INVALID_ARG;
    if(!requests)return ESP_ERR_INVALID_STATE;
    job_t *job=calloc(1,sizeof(*job));if(!job)return ESP_ERR_NO_MEM;
    job->result=calloc(1,sizeof(*job->result));
    if(job->result)job->result->body=malloc(OUSE_NET_RESPONSE_MAX+1);
    if(request->body_size)job->body=malloc(request->body_size);
    if(!job->result || !job->result->body || (request->body_size && !job->body)){error=ESP_ERR_NO_MEM;goto reject;}
    job->request=*request;strcpy(job->url,request->url);strcpy(job->method,request->method);
    if(request->body_size)memcpy(job->body,request->body,request->body_size);
    if(request->header_count)memcpy(job->headers,request->headers,request->header_count*sizeof(*job->headers));
    job->request.url=job->url;job->request.method=job->method;job->request.headers=job->headers;job->request.body=job->body;
    job->context=context;job->complete=complete;job->deadline=esp_timer_get_time()+(int64_t)request->timeout_ms*1000;
    error=ouse_task_create(owner,OUSE_TASK_NETWORK,request->method,OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error!=ESP_OK)goto reject;
    job->id=*id;ouse_task_hold_result(*id,true);
    if(xQueueSend(requests,&job,0)==pdTRUE)return ESP_OK;
    error=ESP_ERR_NO_MEM;ouse_task_hold_result(*id,false);ouse_task_finish(*id,error,false);
reject:
    ouse_net_result_free(job->result);free(job->body);free(job);return error;
}
