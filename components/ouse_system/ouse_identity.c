// SPDX-License-Identifier: MIT
#include "ouse_identity.h"
#include "ouse_tasks.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint32_t version;
    char name[25];
    uint8_t step,enabled,failures,salt[16],hash[32];
} record_t;
typedef struct {unsigned op,step;uint32_t serial,task;char name[25],old_pin[7],pin[7];} job_t;
static record_t saved;
static ouse_identity_state_t state;
static QueueHandle_t jobs;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static int64_t retry_at;
static uint32_t serial;
static void wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
bool ouse_identity_valid_pin(const char *p)
{if(!p || strlen(p)!=6)return false;for(unsigned i=0;i<6;++i)if(p[i]<'0'||p[i]>'9')return false;return true;}
bool ouse_identity_valid_name(const char *p)
{
    if(!p){return false;}size_t n=strlen(p);if(!n||n>24||p[0]==' '||p[n-1]==' ')return false;
    for(size_t i=0;i<n;){unsigned char c=p[i++];if(c<32||c==127)return false;if(c<128)continue;
        unsigned count;uint32_t code,min;
        if(c>=0xc2&&c<=0xdf){count=1;code=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;code=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;code=c&7;min=0x10000;}else return false;
        while(count--){if(i>=n||((unsigned char)p[i]&0xc0)!=0x80)return false;code=(code<<6)|((unsigned char)p[i++]&63);}
        if(code<min||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }return true;
}
void ouse_identity_get(ouse_identity_state_t *out)
{portENTER_CRITICAL(&lock);*out=state;int64_t remaining=retry_at-esp_timer_get_time();out->retry_seconds=remaining>0?(uint32_t)((remaining+999999)/1000000):0;portEXIT_CRITICAL(&lock);}
static esp_err_t derive(const char *pin,const uint8_t salt[16],uint8_t hash[32])
{
    psa_key_derivation_operation_t op=PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t e=psa_key_derivation_setup(&op,PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if(e==PSA_SUCCESS)e=psa_key_derivation_input_integer(&op,PSA_KEY_DERIVATION_INPUT_COST,10000);
    if(e==PSA_SUCCESS)e=psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_SALT,salt,16);
    if(e==PSA_SUCCESS)e=psa_key_derivation_input_bytes(&op,PSA_KEY_DERIVATION_INPUT_PASSWORD,(const uint8_t *)pin,6);
    if(e==PSA_SUCCESS)e=psa_key_derivation_output_bytes(&op,hash,32);
    psa_key_derivation_abort(&op);return e==PSA_SUCCESS?ESP_OK:ESP_FAIL;
}
static esp_err_t persist(const record_t *record)
{
    nvs_handle_t h;esp_err_t e=nvs_open("ouse_identity",NVS_READWRITE,&h);
    if(e==ESP_OK){e=nvs_set_blob(h,"profile",record,sizeof(*record));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}return e;
}
static esp_err_t verify(const char *pin,record_t *next)
{
    if(!next->enabled)return ESP_ERR_INVALID_STATE;
    if(esp_timer_get_time()<retry_at)return ESP_ERR_TIMEOUT;
    uint8_t hash[32]={0};esp_err_t e=derive(pin,next->salt,hash);unsigned difference=0;
    for(unsigned i=0;i<32;++i){difference|=hash[i]^next->hash[i];}wipe(hash,sizeof(hash));
    if(e!=ESP_OK)return e;
    if(difference){if(next->failures<5)++next->failures;if(next->failures>=5){portENTER_CRITICAL(&lock);retry_at=esp_timer_get_time()+30000000;portEXIT_CRITICAL(&lock);}return ESP_ERR_INVALID_ARG;}
    next->failures=0;return ESP_OK;
}
static void worker(void *unused)
{
    (void)unused;job_t job;
    for(;;){job_t *owned=NULL;if(xQueueReceive(jobs,&owned,portMAX_DELAY)!=pdTRUE)continue;
        job=*owned;wipe(owned,sizeof(*owned));free(owned);
        (void)ouse_task_start(job.task,false);record_t next=saved;esp_err_t e=ESP_OK;bool checked=false;
        if(job.op==1){strcpy(next.name,job.name);if(!next.step)next.step=1;}
        else if(job.op==2){if(!next.name[0]||job.step<next.step)e=ESP_ERR_INVALID_STATE;else next.step=job.step;}
        else if(job.op==3||job.op==4){
            if(next.enabled){e=verify(job.old_pin,&next);checked=true;}
            else if(job.op==4)e=ESP_ERR_INVALID_STATE;
            if(e==ESP_OK && job.op==3){next.enabled=job.pin[0]!=0;next.failures=0;wipe(next.hash,32);wipe(next.salt,16);
                if(next.enabled){esp_fill_random(next.salt,16);e=derive(job.pin,next.salt,next.hash);}}
        }
        // Failed attempts also persist; reboot does not clear the failure counter.
        if(e==ESP_OK || (checked && e==ESP_ERR_INVALID_ARG)){
            esp_err_t write=persist(&next);if(write==ESP_OK)saved=next;else e=write;
        }
        portENTER_CRITICAL(&lock);
        strcpy(state.name,saved.name);state.pin_enabled=saved.enabled;state.setup_step=saved.step;state.setup_done=saved.step==4;
        state.pending=false;state.completed=job.serial;state.verified=job.op==4&&e==ESP_OK;state.error=e;++state.revision;
        portEXIT_CRITICAL(&lock);
        uint32_t task=job.task;wipe(&job,sizeof(job));wipe(&next,sizeof(next));ouse_task_finish(task,e,false);
    }
}
esp_err_t ouse_identity_init(void)
{
    if(jobs)return ESP_ERR_INVALID_STATE;
    memset(&state,0,sizeof(state));
    saved.version=1;nvs_handle_t h;esp_err_t e=nvs_open("ouse_identity",NVS_READONLY,&h);
    if(e==ESP_OK){size_t n=sizeof(saved);e=nvs_get_blob(h,"profile",&saved,&n);nvs_close(h);
        if(e==ESP_OK&&(n!=sizeof(saved)||saved.version!=1||saved.step>4||saved.enabled>1||!memchr(saved.name,0,sizeof(saved.name))||
            (saved.step&&!ouse_identity_valid_name(saved.name))))e=ESP_ERR_INVALID_STATE;
    }
    if(e==ESP_ERR_NVS_NOT_FOUND){memset(&saved,0,sizeof(saved));saved.version=1;e=ESP_OK;}
    if(e!=ESP_OK)return e;
    if(psa_crypto_init()!=PSA_SUCCESS)return ESP_FAIL;
    jobs=xQueueCreate(1,sizeof(job_t *));if(!jobs)return ESP_ERR_NO_MEM;
    state=(ouse_identity_state_t){.ready=true,.pin_enabled=saved.enabled,.setup_step=saved.step,.setup_done=saved.step==4};strcpy(state.name,saved.name);
    if(saved.failures>=5)retry_at=esp_timer_get_time()+30000000;
    if(xTaskCreate(worker,"ouse_identity",6144,NULL,2,NULL)!=pdPASS){vQueueDelete(jobs);jobs=NULL;state.ready=false;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
static esp_err_t submit(job_t *job,uint32_t *request)
{
    if(!jobs||!request)return ESP_ERR_INVALID_STATE;
    if(!ouse_tasks_submission_begin())return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);bool busy=state.pending;if(!busy){state.pending=true;state.verified=false;job->serial=++serial;}portEXIT_CRITICAL(&lock);
    esp_err_t e=busy?ESP_ERR_INVALID_STATE:ESP_OK;
    const ouse_task_owner_t owner={.app_id="ouse.system"};
    if(e==ESP_OK)e=ouse_task_create(&owner,OUSE_TASK_SETTINGS,"identitySave",OUSE_TASK_FINISH_ON_CLOSE,false,&job->task);
    if(e==ESP_OK){
        job_t *owned=malloc(sizeof(*owned));
        if(!owned)e=ESP_ERR_NO_MEM;
        else {*owned=*job;*request=job->serial;if(xQueueSend(jobs,&owned,0)!=pdTRUE){wipe(owned,sizeof(*owned));free(owned);e=ESP_ERR_TIMEOUT;}}
        if(e!=ESP_OK)ouse_task_finish(job->task,e,false);
    }
    if(e!=ESP_OK&&!busy){portENTER_CRITICAL(&lock);state.pending=false;state.error=e;portEXIT_CRITICAL(&lock);}
    wipe(job,sizeof(*job));ouse_tasks_submission_end();return e;
}
esp_err_t ouse_identity_name(const char *name,uint32_t *request)
{if(!ouse_identity_valid_name(name))return ESP_ERR_INVALID_ARG;job_t j={.op=1};strcpy(j.name,name);return submit(&j,request);}
esp_err_t ouse_identity_step(unsigned step,uint32_t *request)
{if(step<1||step>4)return ESP_ERR_INVALID_ARG;job_t j={.op=2,.step=step};return submit(&j,request);}
esp_err_t ouse_identity_pin(const char *old,const char *pin,uint32_t *request)
{if(!old||!pin||(old[0]&&!ouse_identity_valid_pin(old))||(pin[0]&&!ouse_identity_valid_pin(pin)))return ESP_ERR_INVALID_ARG;
 job_t j={.op=3};strcpy(j.old_pin,old);strcpy(j.pin,pin);return submit(&j,request);}
esp_err_t ouse_identity_verify(const char *pin,uint32_t *request)
{if(!ouse_identity_valid_pin(pin))return ESP_ERR_INVALID_ARG;job_t j={.op=4};strcpy(j.old_pin,pin);return submit(&j,request);}
