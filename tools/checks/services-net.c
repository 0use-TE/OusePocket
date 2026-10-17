#include "platform.h"
#include "ouse_net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned calls,completions;static bool actual_stopped;static uint32_t current_id;static ouse_net_result_t *delivered;
esp_err_t ouse_http_validate(const ouse_http_request_t *r){return r&&r->url&&r->method ? ESP_OK : ESP_ERR_INVALID_ARG;}
esp_err_t ouse_http_transfer(const ouse_http_request_t *r,int64_t d,size_t limit,ouse_http_response_t *response,ouse_http_sink_t sink,ouse_http_stop_t stop,void *context)
{
    (void)d;assert(limit==32768);++calls;assert(!strcmp(r->url,"https://example.test/")&&!strcmp(r->method,"GET"));
    if(stop(context)){actual_stopped=true;return ESP_ERR_INVALID_STATE;}
    response->status=200;assert(sink(context,(const unsigned char*)"hello",5)==ESP_OK);response->body_size=5;actual_stopped=true;return ESP_OK;
}
static void receive_result(void *context,uint32_t id,ouse_net_result_t *result)
{(void)context;assert(actual_stopped&&id==current_id);++completions;delivered=result;}
#include "NET_SOURCE"
static void pump(void){break_on_empty=true;if(!setjmp(worker_exit))worker(NULL);break_on_empty=false;}
static void flood(void)
{ouse_task_owner_t s={.app_id="ouse.test"};for(unsigned i=0;i<100;++i){uint32_t id;assert(ouse_task_create(&s,OUSE_TASK_SETTINGS,"save",OUSE_TASK_FINISH_ON_CLOSE,false,&id)==ESP_OK);ouse_task_finish(id,ESP_OK,false);}}
int main(void)
{
    assert(ouse_net_init()==ESP_OK);ouse_task_owner_t owner;assert(ouse_task_owner_open("user.network",&owner)==ESP_OK);
    char url[64]="https://example.test/";ouse_http_request_t q={.url=url,.method="GET",.timeout_ms=1000};
    assert(ouse_net_submit(&owner,&q,&current_id,receive_result,NULL)==ESP_OK);strcpy(url,"changed-after-submit");pump();
    assert(completions==1&&calls==1&&delivered->response.body_size==5&&!strcmp((const char*)delivered->body,"hello"));
    ouse_task_snapshot_t t;assert(ouse_task_get(&owner,current_id,&t)&&t.state==OUSE_TASK_SUCCEEDED&&t.result_held);
    ouse_task_ack(&owner,current_id);flood();assert(ouse_task_get(&owner,current_id,&t)&&t.result_held); // event ack cannot recycle payload history
    ouse_net_result_free(delivered);delivered=NULL;ouse_task_hold_result(current_id,false);flood();assert(!ouse_task_get(&owner,current_id,&t));
    strcpy(url,"https://example.test/");assert(ouse_net_submit(&owner,&q,&current_id,receive_result,NULL)==ESP_OK);
    assert(ouse_task_cancel(&owner,current_id,"test")==ESP_OK);actual_stopped=true;pump();assert(calls==1&&delivered->cancelled);
    ouse_net_result_free(delivered);ouse_task_hold_result(current_id,false);ouse_task_ack(&owner,current_id);
    assert(ouse_net_submit(&owner,&q,&current_id,receive_result,NULL)==ESP_OK);ouse_task_owner_close(&owner);pump();
    assert(ouse_task_get(NULL,current_id,&t)&&t.detached&&!t.result_held&&!t.delivery_pending&&t.state==OUSE_TASK_CANCELLED);ouse_net_result_free(delivered);
    puts("PASS net worker: real bounded submission copies, callback after native stop, payload pin beyond event ack, recycling, queued cancel and owner-close detachment");
}
