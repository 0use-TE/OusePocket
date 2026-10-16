// SPDX-License-Identifier: MIT
#include "ouse_installer.h"
#include "ouse_bpk.h"
#include "ouse_protocol.h"
#include "ouse_core.h"
#include "ouse_storage.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#if CONFIG_OUSE_DEVICE_DEBUG
#include "ouse_debug_transport.h"
#endif

#define DIRECTORY OUSE_STORAGE_PACKAGE_DIRECTORY
#define STAGING DIRECTORY "/upload.tmp"
#define TAG "OuseUSB"
static ouse_bpk_t *installed[OUSE_APP_CAPACITY]; // protocol worker owns packages
static bool storage_ready,started;
static unsigned char *upload;
static size_t received,expected;
static uint32_t upload_crc,owner;
static int64_t activity;
static SemaphoreHandle_t ui_done;
static struct { int action;ouse_bpk_t *package;const char *id;esp_err_t error; } ui;

static void ui_call(void *unused)
{
    (void)unused;
    if(ui.action==0){
        ui.error=ouse_core_user_check(ui.package->id);
        if(ui.error==ESP_OK)ui.error=ouse_script_validate(&ui.package->package);
    }else if(ui.action==1)ui.error=ouse_core_user_apply(&ui.package->definition);
    else if(ui.action==2)ui.error=ouse_core_user_remove(ui.id);
    else ui.error=ouse_app_open(ui.id);
    xSemaphoreGive(ui_done);
}
static esp_err_t on_ui(int action,ouse_bpk_t *package,const char *id)
{
    ui.action=action;ui.package=package;ui.id=id;
    esp_err_t error=ouse_core_post_ui(ui_call,NULL);
    if(error!=ESP_OK)return error;
    // Once dispatched, never expire/free the context while LVGL can still use it.
    xSemaphoreTake(ui_done,portMAX_DELAY);return ui.error;
}
static int find_package(const char *id)
{ for(unsigned i=0;i<OUSE_APP_CAPACITY;++i)if(installed[i] && !strcmp(installed[i]->id,id))return (int)i;return -1; }
static int free_slot(void)
{ for(unsigned i=0;i<OUSE_APP_CAPACITY;++i)if(!installed[i])return (int)i;return -1; }
static void path_for(const char *id,char path[128]){snprintf(path,128,DIRECTORY "/%s.bpk",id);}
static bool valid_id(const char *id)
{
    if(strncmp(id,"user.",5) || !id[5] || strlen(id)>=48)return false;
    for(const char *p=id;*p;++p)if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='.' || *p=='_' || *p=='-'))return false;
    return true;
}
static esp_err_t activate(ouse_bpk_t *p)
{
    int slot=find_package(p->id);if(slot<0)slot=free_slot();if(slot<0)return ESP_ERR_NO_MEM;
    esp_err_t error=on_ui(1,p,NULL);
    if(error==ESP_OK){ouse_bpk_free(installed[slot]);installed[slot]=p;}
    return error;
}
static esp_err_t mount_storage(void)
{
    // Mount and private file operations belong to the storage task. This is
    // called only by the installer task after the desktop has started.
    esp_err_t error=ouse_storage_start();
    storage_ready=error==ESP_OK;return error;
}
static void restore_packages(void)
{
    DIR *dir=opendir(DIRECTORY);if(!dir)return;
    struct dirent *entry;
    while((entry=readdir(dir))!=NULL){
        size_t n=strlen(entry->d_name);if(n<=4 || n>=52 || strcmp(entry->d_name+n-4,".bpk"))continue;
        char id[48];memcpy(id,entry->d_name,n-4);id[n-4]=0;if(!valid_id(id))continue;
        char path[128];path_for(id,path);struct stat info;
        if(stat(path,&info)!=0 || info.st_size<=0 || info.st_size>OUSE_BPK_MAX)continue;
        unsigned char *bytes=heap_caps_malloc((size_t)info.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!bytes)break;
        FILE *file=fopen(path,"rb");ouse_bpk_t *p=NULL;esp_err_t error=ESP_FAIL;
        if(file){
            if(fread(bytes,1,(size_t)info.st_size,file)==(size_t)info.st_size)error=ouse_bpk_parse(bytes,(size_t)info.st_size,&p);
            fclose(file);
        }
        free(bytes);
        if(error==ESP_OK && strcmp(id,p->id))error=ESP_ERR_INVALID_ARG;
        if(error==ESP_OK)error=on_ui(0,p,NULL);
        if(error==ESP_OK)error=activate(p);
        if(error!=ESP_OK){ouse_bpk_free(p);ESP_LOGW(TAG,"Skipped %s: %s (file preserved)",id,esp_err_to_name(error));}
        else ESP_LOGI(TAG,"Restored %s",id);
    }
    closedir(dir);unlink(STAGING);(void)ouse_storage_refresh();
}
static void abort_upload(void)
{ free(upload);upload=NULL;received=expected=0;owner=0; }
static esp_err_t install_upload(char *result,size_t capacity)
{
    if(!upload || received!=expected || ouse_crc32(upload,received)!=upload_crc)return ESP_ERR_INVALID_ARG;
    ouse_bpk_t *p=NULL;esp_err_t error=ouse_bpk_parse(upload,received,&p);
    if(error!=ESP_OK){ESP_LOGE(TAG,"BPK rejected: archive/manifest (%s)",esp_err_to_name(error));return error;}
    if(!valid_id(p->id)){ouse_bpk_free(p);return ESP_ERR_INVALID_ARG;}
    error=on_ui(0,p,NULL); // full schema/syntax/capacity check BEFORE Flash writes
    if(error!=ESP_OK){ESP_LOGE(TAG,"BPK rejected: schema/syntax/capacity (%s)",esp_err_to_name(error));ouse_bpk_free(p);return error;}
    char path[128];path_for(p->id,path);
    FILE *file=fopen(STAGING,"wb");bool ok=false;
    if(file){
        ok=fwrite(upload,1,received,file)==received;
        if(fflush(file)!=0)ok=false;
        if(fsync(fileno(file))!=0)ok=false;
        if(fclose(file)!=0)ok=false;
    }
    if(!ok || rename(STAGING,path)!=0){unlink(STAGING);ouse_bpk_free(p);return ESP_FAIL;}
    // LittleFS atomic rename is the commit point. Interrupted uploads never
    // replace the previous package. Only this worker can mutate user registry.
    error=activate(p);
    (void)ouse_storage_refresh();
    if(error==ESP_OK)snprintf(result,capacity,"%s",p->id);
    else {ouse_bpk_free(p);snprintf(result,capacity,"committed-reboot-to-restore");}
    return error;
}
static bool number8(const char *text,uint32_t *n)
{
    if(strlen(text)!=8)return false;
    for(unsigned i=0;i<8;++i)if(!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f') || (text[i]>='A' && text[i]<='F')))return false;
    *n=(uint32_t)strtoul(text,NULL,16);return true;
}
static esp_err_t command(ouse_frame_t *f,char *result,size_t cap)
{
    if(upload && owner!=f->token)return ESP_ERR_INVALID_STATE;
    activity=esp_timer_get_time();
    if(!strcmp(f->verb,"HELLO")){
        snprintf(result,cap,"core=%s,storage=%s,max=%u,transport=uart0-921600",ouse_core_version(),storage_ready?"ready":"unavailable",OUSE_BPK_MAX);
#if CONFIG_OUSE_DEVICE_DEBUG
        strncat(result,",debug=1",cap-strlen(result)-1);
#endif
        return ESP_OK;
    }
    if(!strcmp(f->verb,"ABORT")){abort_upload();return ESP_OK;}
#if CONFIG_OUSE_DEVICE_DEBUG
    if(!strncmp(f->verb,"DBG_",4))return upload?ESP_ERR_INVALID_STATE:ouse_debug_command(f->verb,f->payload,result,cap);
#endif
    if(!storage_ready)return ESP_ERR_INVALID_STATE;
    if(!strcmp(f->verb,"LIST")){
        size_t pos=0;for(unsigned i=0;i<OUSE_APP_CAPACITY;++i)if(installed[i])pos+=(size_t)snprintf(result+pos,cap-pos,"%s%s",pos?",":"",installed[i]->id);
        return ESP_OK;
    }
    if(!strcmp(f->verb,"BEGIN")){
        if(upload)return ESP_ERR_INVALID_STATE;
        char *colon=strchr(f->payload,':');uint32_t n,crc;
        if(!colon)return ESP_ERR_INVALID_ARG;
        *colon=0;
        if(!number8(f->payload,&n) || !number8(colon+1,&crc) || !n || n>OUSE_BPK_MAX)return ESP_ERR_INVALID_ARG;
        upload=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!upload)return ESP_ERR_NO_MEM;
        expected=n;received=0;upload_crc=crc;owner=f->token;return ESP_OK;
    }
    if(!strcmp(f->verb,"DATA")){
        if(!upload)return ESP_ERR_INVALID_STATE;
        char *colon=strchr(f->payload,':');uint32_t offset;size_t n;unsigned char data[512];
        if(!colon)return ESP_ERR_INVALID_ARG;
        *colon=0;
        if(!number8(f->payload,&offset) || !ouse_hex_decode(colon+1,data,sizeof(data),&n) || !n ||
           offset!=received || n>expected-received)return ESP_ERR_INVALID_ARG;
        memcpy(upload+received,data,n);received+=n;snprintf(result,cap,"%u",(unsigned)received);return ESP_OK;
    }
    if(!strcmp(f->verb,"END")){
        esp_err_t error=install_upload(result,cap);abort_upload();return error;
    }
    if(upload)return ESP_ERR_INVALID_STATE;
    if(!strcmp(f->verb,"OPEN")){
        if(!valid_id(f->payload) || find_package(f->payload)<0)return ESP_ERR_NOT_FOUND;
        return on_ui(3,NULL,f->payload);
    }
    if(!strcmp(f->verb,"REMOVE")){
        int slot=valid_id(f->payload)?find_package(f->payload):-1;if(slot<0)return ESP_ERR_NOT_FOUND;
        char path[128];path_for(f->payload,path);
        // A prior attempt may have removed the file before UI dispatch failed.
        // Keep the live package until removal completes; a fresh retry must be
        // able to finish this already-committed deletion. Other I/O errors still
        // preserve the registration and report failure.
        if(unlink(path)!=0 && errno!=ENOENT)return ESP_FAIL;
        esp_err_t error=on_ui(2,NULL,f->payload);
        // A failed purge can be retried after the UI registration is gone.
        // Closing first prevents the old script from submitting another write
        // after the storage worker's FIFO deletion barrier.
        if(error==ESP_OK || error==ESP_ERR_NOT_FOUND)error=ouse_storage_purge(f->payload);
        if(error==ESP_OK){ouse_bpk_free(installed[slot]);installed[slot]=NULL;}
        (void)ouse_storage_refresh();
        return error;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
static void send_response(const char *response)
{
    // One stdio write, with a leading newline: logs can be discarded by the PC
    // without treating console text as binary protocol or toggling DTR/RTS.
    printf("\n%s\n",response);fflush(stdout);
}
static void process_line(char *line)
{
    static uint32_t token,sequence,checksum;static bool cached;
    static char response[OUSE_WIRE_MAX];
    ouse_frame_t frame;if(!ouse_frame_parse(line,&frame))return;
    if(cached && frame.token==token && frame.sequence==sequence){
        if(frame.checksum==checksum){if(upload)activity=esp_timer_get_time();send_response(response);}
        return; // conflicting retransmission never executes
    }
    if(cached && frame.token==token && frame.sequence<sequence)return;
    char result[1080]="-";esp_err_t error=command(&frame,result,sizeof(result));
    int n=snprintf(response,sizeof(response),"OPK1 %08lx %08lx %s %s",(unsigned long)frame.token,(unsigned long)frame.sequence,
        error==ESP_OK?"OK":"ERR",error==ESP_OK?(result[0]?result:"-"):esp_err_to_name(error));
    uint32_t crc=ouse_crc32(response,(size_t)n);
    snprintf(response+n,sizeof(response)-(size_t)n," %08lx",(unsigned long)crc);
    token=frame.token;sequence=frame.sequence;checksum=frame.checksum;cached=true;send_response(response);
}
static char *rx_line;
static size_t rx_length;
static bool rx_dropping;
static void consume_bytes(const unsigned char *bytes,int n)
{
        for(int i=0;i<n;++i){
            char c=(char)bytes[i];
            if(c=='\r')continue;
            if(c=='\n'){
                if(!rx_dropping && rx_length){rx_line[rx_length]=0;process_line(rx_line);}
                rx_length=0;rx_dropping=false;
            }else if(!rx_dropping){
                if(c<32 || c>126 || rx_length>=OUSE_WIRE_MAX-1){rx_length=0;rx_dropping=true;}
                else rx_line[rx_length++]=c;
            }
        }
        if(upload && esp_timer_get_time()-activity>30000000){abort_upload();rx_length=0;rx_dropping=false;ESP_LOGW(TAG,"Upload timeout; previous app preserved");}
}
static void worker(void *context)
{
    (void)context;
    esp_err_t error=mount_storage();
    if(error==ESP_OK)restore_packages();else ESP_LOGE(TAG,"Storage unavailable: %s; existing data preserved",esp_err_to_name(error));
    ESP_LOGI(TAG,"READY; UART0 921600; protocol=OPK1; storage=%s",storage_ready?"ready":"unavailable");
    unsigned char bytes[256];
    for(;;){
        int n=uart_read_bytes(UART_NUM_0,bytes,1,upload?pdMS_TO_TICKS(1000):portMAX_DELAY);
        if(n>0){size_t buffered=0;uart_get_buffered_data_len(UART_NUM_0,&buffered);
            if(buffered>sizeof(bytes)-1)buffered=sizeof(bytes)-1;
            if(buffered){int more=uart_read_bytes(UART_NUM_0,bytes+1,buffered,0);if(more>0)n+=more;}
        }
        consume_bytes(bytes,n);
    }
}
esp_err_t ouse_installer_start(void)
{
    if(started || !ouse_core_is_started())return ESP_ERR_INVALID_STATE;
    // Refuse to share RX ownership with a console REPL or another installer.
    if(uart_is_driver_installed(UART_NUM_0))return ESP_ERR_INVALID_STATE;
    rx_line=malloc(OUSE_WIRE_MAX);if(!rx_line)return ESP_ERR_NO_MEM;
    ui_done=xSemaphoreCreateBinary();if(!ui_done){free(rx_line);rx_line=NULL;return ESP_ERR_NO_MEM;}
    uart_config_t config={.baud_rate=921600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t error=uart_param_config(UART_NUM_0,&config);
    if(error==ESP_OK)error=uart_driver_install(UART_NUM_0,16384,0,0,NULL,0);
    if(error!=ESP_OK){vSemaphoreDelete(ui_done);ui_done=NULL;free(rx_line);rx_line=NULL;return error;}
    if(xTaskCreate(worker,"ouse_installer",16384,NULL,3,NULL)!=pdPASS){uart_driver_delete(UART_NUM_0);vSemaphoreDelete(ui_done);ui_done=NULL;free(rx_line);rx_line=NULL;return ESP_ERR_NO_MEM;}
    started=true;return ESP_OK;
}
