#include "platform.h"
#include "ouse_media.h"
#include "ouse_device_driver.h"
#include "ouse_net.h"
#include "ouse_notifications.h"
#include "services-wave-fixture.h"
#include <assert.h>
#include <stdio.h>
static bool notice_active;
esp_err_t ouse_notifications_publish(const char *app,const ouse_notification_request_t *r)
{assert(app&&r&&r->ongoing&&!strcmp(r->id,"ouse.media"));notice_active=true;return ESP_OK;}
esp_err_t ouse_notifications_cancel(const char *app,const char *id)
{assert(app&&!strcmp(id,"ouse.media"));notice_active=false;return ESP_OK;}
static unsigned transport_calls;static int transport_mode;static bool native_cleaned;
esp_err_t ouse_http_validate(const ouse_http_request_t *r){return r&&r->url&&!strncmp(r->url,"https://",8) ? ESP_OK : ESP_ERR_INVALID_ARG;}
esp_err_t ouse_http_transfer(const ouse_http_request_t *r,int64_t deadline,size_t limit,ouse_http_response_t *response,ouse_http_sink_t sink,ouse_http_stop_t stop,void *context)
{
    (void)r;(void)deadline;(void)limit;(void)sink;++transport_calls;response->status=200;
    ouse_media_state_t state;assert(ouse_media_get(NULL,&state)&&state.active);
    assert(ouse_media_stop(state.app_id)==ESP_OK);ouse_media_consumer_done(state.task_id,transport_mode==2 ? ESP_FAIL : ESP_OK,true);
    ouse_task_snapshot_t task;assert(ouse_task_get(NULL,state.task_id,&task)&&task.state==OUSE_TASK_CANCELLING);
    assert(ouse_media_get(NULL,&state)&&state.active&&!native_cleaned&&stop(context));
    native_cleaned=true;return ESP_ERR_INVALID_STATE;
}
#include "MEDIA_SOURCE"
static void pump(void){break_on_empty=true;if(!setjmp(worker_exit))worker(NULL);break_on_empty=false;}
static void claim(uint32_t id)
{ouse_audio_request_t r;assert(ouse_devices_take_audio(&r)&&r.id==id&&r.action==OUSE_AUDIO_MEDIA);assert(ouse_task_start(id,true));ouse_media_consumer_started(id);}
static void drain(uint32_t id)
{int16_t samples[256];bool done=false;esp_err_t error;while(!done){unsigned n=ouse_media_read(id,samples,256,&done,&error);assert(error==ESP_OK);ouse_media_consumed(id,n);}ouse_media_consumer_done(id,ESP_OK,false);}
static void expire_full_buffer(void)
{ouse_media_state_t s;if(ouse_media_get(NULL,&s)&&s.buffered_bytes==8192){platform_hook=NULL;test_clock+=4LL*60*60*1000000+1;}}
int main(void)
{
    ouse_device_port_t port={0};ouse_device_config_t config={.volume=40,.sleep_seconds=60};assert(ouse_devices_init(&port,&config)==ESP_OK);ouse_devices_set_available(1,true,ESP_OK);
    assert(ouse_media_init()==ESP_OK);ouse_task_owner_t owner;assert(ouse_task_owner_open("user.media",&owner)==ESP_OK);
    size_t size;unsigned char *data=make_wave(16000,1,1000,&size);uint32_t id,other;ouse_media_state_t state;ouse_task_snapshot_t task;
    assert(ouse_media_play(&owner,NULL,data,size,&id)==ESP_OK&&notice_active);assert(ouse_media_play(&owner,NULL,data,size,&other)==ESP_ERR_INVALID_STATE);
    assert(ouse_media_pause("user.foreign",true)==ESP_ERR_NOT_FOUND&&!ouse_media_get("user.foreign",&state));
    claim(id);pump();assert(ouse_media_get(owner.app_id,&state)&&state.active&&state.buffered_bytes==2000);
    assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_RUNNING); // producer done is not codec done
    assert(ouse_media_pause(owner.app_id,true)==ESP_OK);int16_t samples[256];bool done;esp_err_t error;
    assert(!ouse_media_read(id,samples,256,&done,&error)&&!done);assert(ouse_media_pause(owner.app_id,false)==ESP_OK);
    ouse_media_interrupted(id,true);assert(!ouse_media_read(id,samples,256,&done,&error)&&!done);ouse_media_interrupted(id,false);
    ouse_task_owner_close(&owner);assert(ouse_media_get("user.media",&state)&&state.active);drain(id);
    assert(ouse_task_get(NULL,id,&task)&&task.state==OUSE_TASK_SUCCEEDED&&task.detached);assert(ouse_media_get("user.media",&state)&&!state.active&&state.position_ms==62);
    assert(ouse_task_owner_open("user.media",&owner)==ESP_OK);
    assert(ouse_media_play(&owner,"https://example.test/stream.wav",NULL,0,&id)==ESP_OK);claim(id);pump();
    assert(native_cleaned&&transport_calls==1&&ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_CANCELLED);
    transport_mode=2;native_cleaned=false;assert(ouse_media_play(&owner,"https://example.test/stream.wav",NULL,0,&id)==ESP_OK);claim(id);pump();
    assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_FAILED&&task.error==ESP_FAIL); // cleanup error beats cancellation
    // A cancelled queued producer must never start transport.
    assert(ouse_media_play(&owner,"https://example.test/stream.wav",NULL,0,&id)==ESP_OK);assert(ouse_media_stop(owner.app_id)==ESP_OK);
    ouse_audio_request_t req;assert(ouse_devices_take_audio(&req)&&!ouse_task_start(id,true));ouse_media_consumer_done(id,ESP_OK,true);pump();assert(transport_calls==2);
    // Two-sided timeout/stop: a paused full producer remains bounded.
    assert(ouse_media_play(&owner,NULL,data,size,&id)==ESP_OK);claim(id);pump();assert(ouse_media_pause(owner.app_id,true)==ESP_OK);assert(ouse_media_stop(owner.app_id)==ESP_OK);ouse_media_consumer_done(id,ESP_OK,true);
    assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_CANCELLED);
    size_t large_size;unsigned char *large=make_wave(16000,1,12000,&large_size);
    assert(ouse_media_play(&owner,NULL,large,large_size,&id)==ESP_OK);claim(id);assert(ouse_media_pause(owner.app_id,true)==ESP_OK);
    platform_hook=expire_full_buffer;pump();assert(!platform_hook);
    assert(ouse_media_get(owner.app_id,&state)&&state.active&&state.error==ESP_ERR_TIMEOUT&&state.buffered_bytes==8192);
    assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_RUNNING); // codec cleanup still outstanding
    ouse_media_consumer_done(id,ESP_ERR_TIMEOUT,false);assert(ouse_task_get(&owner,id,&task)&&task.state==OUSE_TASK_FAILED);free(large);
    unsigned char *clip=make_wave(8000,1,240000,&large_size);assert(large_size==480044);
    assert(ouse_media_play(&owner,NULL,clip,large_size,&id)==ESP_OK&&notice_active);
    free(clip); // native session owns a copy of the 30-second-size package asset
    assert(ouse_media_stop(owner.app_id)==ESP_OK);ouse_media_consumer_done(id,ESP_OK,true);pump();
    assert(!notice_active);free(data);puts("PASS media: real source copies/PCM buffer, single session/owner controls, pause/interruption, close survival, producer+codec cleanup join, late transport cancel, cleanup failure and queued no-I/O");
}
