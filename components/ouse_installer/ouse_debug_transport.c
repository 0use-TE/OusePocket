#include "ouse_journal.h"
// SPDX-License-Identifier: MIT
#include "ouse_debug_transport.h"
#include "ouse_debug.h"
#include "ouse_runtime.h"
#include "ouse_ime.h"
#include "ouse_notifications.h"
#include "ouse_storage.h"
#include "ouse_usb.h"
#include "ouse_devices.h"
#include "ouse_files.h"
#include "ouse_ble.h"
#include "ouse_protocol.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_app_desc.h"
#include "esp_freertos_hooks.h"
#include "esp_jpeg_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "unity.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#if CONFIG_OUSE_DEVICE_DEBUG
extern esp_err_t ouse_board_debug_ble_probe(char *out,size_t capacity) __attribute__((weak));
// Optional board diagnostic: the installer remains independent of any BSP.
extern esp_err_t ouse_board_debug_lcd_transfer(int enabled, bool *current_enabled,
                                              uint32_t *skipped) __attribute__((weak));
extern esp_err_t ouse_board_debug_flush_wait(int enabled, bool *current_enabled,
                                            uint32_t *waits) __attribute__((weak));
#endif

// Exclusively owned by the existing serial worker. UI callbacks never outlive
// this static context, including when the caller times out on the PC.
enum { U_STATE,U_TOUCH,U_HOME,U_OPEN,U_TREE,U_SNAPSHOT,U_FREE,U_UNIT,U_LCD,U_FLUSH,U_REPAINT,U_RUNTIME,U_IME,U_CLOSE,U_RESET };
static struct {int action,x,y,down;unsigned contact;const char *id;char text[1024];
    esp_err_t error;ouse_debug_image_t image;int dpi,dp;bool lcd_enabled,flush_enabled;
    uint32_t lcd_skipped,flush_waits; } request;
static SemaphoreHandle_t done;
static unsigned char *resource;
static size_t resource_size;
// Diagnostic A/B control only: return false through IDF's public idle hook to
// bypass WFI, while keeping both cores, interrupts and every service running.
// Normal idle is restored on reboot and when the test releases this control.
static bool busy_idle;
static bool IRAM_ATTR diagnostic_busy_idle(void) { return false; }
static esp_err_t idle_wait(bool enabled)
{
    if(enabled && busy_idle){
        for(unsigned i=0;i<CONFIG_FREERTOS_NUMBER_OF_CORES;++i)
            esp_deregister_freertos_idle_hook_for_cpu(diagnostic_busy_idle,i);
        busy_idle=false;
    } else if(!enabled && !busy_idle){
        for(unsigned i=0;i<CONFIG_FREERTOS_NUMBER_OF_CORES;++i){
            esp_err_t error=esp_register_freertos_idle_hook_for_cpu(diagnostic_busy_idle,i);
            if(error!=ESP_OK){
                while(i)esp_deregister_freertos_idle_hook_for_cpu(diagnostic_busy_idle,--i);
                return error;
            }
        }
        busy_idle=true;
    }
    return ESP_OK;
}
static void run_ui(void *unused)
{
    (void)unused;request.error=ESP_OK;
    if(request.action!=U_RESET && !ouse_system_debug_enabled()){request.error=ESP_ERR_INVALID_STATE;xSemaphoreGive(done);return;}
    switch(request.action){
    case U_RESET:
        (void)ouse_debug_touch(0,0,false);(void)ouse_debug_touch_contact(1,0,0,false);
        if(ouse_board_debug_lcd_transfer)(void)ouse_board_debug_lcd_transfer(1,&request.lcd_enabled,&request.lcd_skipped);
        if(ouse_board_debug_flush_wait)(void)ouse_board_debug_flush_wait(1,&request.flush_enabled,&request.flush_waits);
        break;
    case U_STATE:request.error=ouse_debug_state(request.text,sizeof(request.text));break;
    case U_TOUCH:request.error=ouse_debug_touch_contact(request.contact,request.x,request.y,request.down!=0);break;
    case U_HOME:request.error=ouse_debug_home();break;
    case U_OPEN:request.error=ouse_debug_open(request.id);break;
    case U_CLOSE:request.error=ouse_app_close_id(request.id);break;
    case U_TREE:request.error=ouse_debug_tree((char *)resource,65536);break;
    case U_SNAPSHOT:request.error=ouse_debug_snapshot(&request.image);break;
    case U_FREE:ouse_debug_snapshot_free(&request.image);break;
    case U_UNIT:request.dpi=ouse_ui_density();request.dp=ouse_ui_dp(160);break;
    case U_REPAINT:lv_obj_invalidate(lv_screen_active());break;
    case U_RUNTIME: {
        ouse_runtime_stats_t s={0};ouse_runtime_get_stats(&s);
        snprintf(request.text,sizeof(request.text),
            "instances=%u,contexts=%u,widgets=%u,timers=%u,errors=%u,js_heap_bytes=%zu,"
            "background=%u,runnable=%u,queued_events=%u,dropped_events=%u,worker_stack_min=%u,storage_jobs=%u,storage_pending=%u",
            s.instances,s.contexts,s.widgets,s.timers,s.errors,s.js_heap_bytes,
            s.background,s.runnable,s.queued_events,s.dropped_events,s.worker_stack_min,s.storage_jobs,s.storage_pending);
        break;
    }
    case U_IME: {
        ouse_ime_state_t s={0};ouse_ime_get_state(&s);
        snprintf(request.text,sizeof(request.text),
            "visible=%u,mode=%u,keyboard_type=%u,attachments=%u,keyboard_height=%d,viewport_height=%d,"
            "input_x1=%ld,input_y1=%ld,input_x2=%ld,input_y2=%ld,viewport_y1=%ld,viewport_y2=%ld,keyboard_y1=%ld,keyboard_y2=%ld",
            s.visible?1u:0u,(unsigned)s.mode,(unsigned)s.keyboard_type,s.attachments,s.keyboard_height,s.viewport_height,
            (long)s.input_bounds.x1,(long)s.input_bounds.y1,(long)s.input_bounds.x2,(long)s.input_bounds.y2,
            (long)s.viewport_bounds.y1,(long)s.viewport_bounds.y2,(long)s.keyboard_bounds.y1,(long)s.keyboard_bounds.y2);
        break;
    }
#if CONFIG_OUSE_DEVICE_DEBUG
    case U_LCD:
        request.error=ouse_board_debug_lcd_transfer ?
            ouse_board_debug_lcd_transfer(request.down,&request.lcd_enabled,&request.lcd_skipped) : ESP_ERR_NOT_SUPPORTED;
        break;
    case U_FLUSH:
        request.error=ouse_board_debug_flush_wait ?
            ouse_board_debug_flush_wait(request.down,&request.flush_enabled,&request.flush_waits) : ESP_ERR_NOT_SUPPORTED;
        break;
#endif
    default:request.error=ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreGive(done);
}
static esp_err_t call_ui(int action)
{
    if(!done)done=xSemaphoreCreateBinary();
    if(!done)return ESP_ERR_NO_MEM;
    request.action=action;esp_err_t error=ouse_core_post_ui(run_ui,NULL);
    if(error!=ESP_OK)return error;
    xSemaphoreTake(done,portMAX_DELAY);return request.error;
}
static void hex(const unsigned char *bytes,size_t n,char *out)
{static const char digits[]="0123456789abcdef";for(size_t i=0;i<n;++i){out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}out[n*2]=0;}
static bool decimal(const char **p,unsigned *value,char end)
{
    if(**p<'0' || **p>'9')return false;
    unsigned n=0;
    do{unsigned d=(unsigned)(*(*p)++-'0');if(n>(UINT_MAX-d)/10)return false;n=n*10+d;}while(**p>='0' && **p<='9');
    if(**p!=end)return false;
    if(end)++*p;
    *value=n;return true;
}
static esp_err_t screenshot(void)
{
    esp_err_t error=call_ui(U_SNAPSHOT);if(error!=ESP_OK)return error;
    ouse_debug_image_t *image=&request.image;
    // LVGL RGB888 stores B,G,R; the encoder expects R,G,B.
    for(size_t i=0;i<image->size;i+=3){uint8_t b=image->pixels[i];image->pixels[i]=image->pixels[i+2];image->pixels[i+2]=b;}
    jpeg_enc_config_t config=DEFAULT_JPEG_ENC_CONFIG();config.width=image->width;config.height=image->height;
    config.src_type=JPEG_PIXEL_FORMAT_RGB888;config.subsampling=JPEG_SUBSAMPLE_420;config.quality=65;config.task_enable=false;
    jpeg_enc_handle_t encoder=NULL;int length=0;const size_t capacity=256*1024;
    resource=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    jpeg_error_t result=resource ? jpeg_enc_open(&config,&encoder) : JPEG_ERR_NO_MEM;
    if(result==JPEG_ERR_OK)result=jpeg_enc_process(encoder,image->pixels,(int)image->size,resource,(int)capacity,&length);
    if(encoder)jpeg_enc_close(encoder);
    esp_err_t release_error=call_ui(U_FREE);
    if(result!=JPEG_ERR_OK || release_error!=ESP_OK || length<=0){free(resource);resource=NULL;return ESP_FAIL;}
    resource_size=(size_t)length;return ESP_OK;
}

// Unity runs on the serial task; assertions never longjmp across the UI task.
// These exercise the real board firmware, input device and shell, not doubles.
void setUp(void) {}
void tearDown(void)
{
    request.x=160;request.y=470;request.down=0;(void)call_ui(U_TOUCH);(void)call_ui(U_HOME);
}
static void pointer(int x,int y,bool pressed)
{
    request.x=x;request.y=y;request.down=pressed;
    TEST_ASSERT_EQUAL(ESP_OK,call_ui(U_TOUCH));vTaskDelay(pdMS_TO_TICKS(35));
}
static void open_counter(void)
{ request.id="ouse.settings";TEST_ASSERT_EQUAL(ESP_OK,call_ui(U_OPEN));vTaskDelay(pdMS_TO_TICKS(100)); }
static void read_state(void) {TEST_ASSERT_EQUAL(ESP_OK,call_ui(U_STATE));}
static void test_density_and_heap(void)
{
    TEST_ASSERT_EQUAL(ESP_OK,call_ui(U_UNIT));TEST_ASSERT_EQUAL(request.dpi,request.dp);
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(false));
}
static void test_input_bounds(void)
{
    request.x=-1;request.y=50;request.down=1;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,call_ui(U_TOUCH));
    request.x=320;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,call_ui(U_TOUCH));
}
static void test_swipe_home(void)
{
    open_counter();pointer(160,474,true);pointer(160,450,true);pointer(160,422,true);pointer(160,390,true);pointer(160,390,false);
    vTaskDelay(pdMS_TO_TICKS(220));read_state();
    TEST_ASSERT_NOT_NULL(strstr(request.text,"\"active\":\"\""));TEST_ASSERT_NOT_NULL(strstr(request.text,"\"recents\":false"));
}
static void test_hold_overview_with_jitter(void)
{
    open_counter();pointer(160,474,true);pointer(160,448,true);pointer(160,420,true);
    for(unsigned i=0;i<7;++i)pointer(160+(int)(i%3),420-(int)(i%2),true);
    read_state();TEST_ASSERT_NOT_NULL(strstr(request.text,"\"recents\":true"));
    pointer(160,420,false);read_state();TEST_ASSERT_NOT_NULL(strstr(request.text,"\"recents\":true"));
}
static void test_short_swipe_cancels(void)
{
    open_counter();pointer(160,474,true);pointer(160,456,true);pointer(160,456,false);
    vTaskDelay(pdMS_TO_TICKS(180));read_state();TEST_ASSERT_NOT_NULL(strstr(request.text,"\"active\":\"ouse.settings\""));
}
static void test_overview_exit(void)
{
    test_hold_overview_with_jitter();
    pointer(160,474,true);pointer(160,440,true);pointer(160,390,true);pointer(160,390,false);
    vTaskDelay(pdMS_TO_TICKS(160));read_state();
    TEST_ASSERT_NOT_NULL(strstr(request.text,"\"recents\":false"));
    TEST_ASSERT_NOT_NULL(strstr(request.text,"\"active\":\"\""));
    TEST_ASSERT_NOT_NULL(strstr(request.text,"ouse.settings"));
    TEST_ASSERT_NOT_NULL(strstr(request.text,"\"per_page\":12"));
}
static void test_developer_gate(void)
{
    esp_err_t disabled=ouse_system_request_debug(false);
    bool blocked=!ouse_system_debug_enabled();
    char result[64];esp_err_t command=ouse_debug_command("DBG_HEAP","-",result,sizeof(result));
    esp_err_t restored=ouse_system_request_debug(true);
    TEST_ASSERT_EQUAL(ESP_OK,restored);
    TEST_ASSERT_EQUAL(ESP_OK,disabled);
    TEST_ASSERT_TRUE(blocked);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,command);
}
static esp_err_t selftest(char *out,size_t cap)
{
    UnityBegin("ouse-device");
    RUN_TEST(test_density_and_heap);RUN_TEST(test_input_bounds);RUN_TEST(test_swipe_home);
    RUN_TEST(test_hold_overview_with_jitter);RUN_TEST(test_short_swipe_cancels);
    RUN_TEST(test_developer_gate);
    RUN_TEST(test_overview_exit);
    int failures=UnityEnd();snprintf(out,cap,"tests=7,failures=%d",failures);return ESP_OK;
}
static esp_err_t heap_state(char *out,size_t cap)
{
    const uint32_t capabilities[2]={MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT};
    size_t free_bytes[2],largest[2],minimum[2],total[2];
    for(unsigned i=0;i<2;++i){
        free_bytes[i]=heap_caps_get_free_size(capabilities[i]);
        largest[i]=heap_caps_get_largest_free_block(capabilities[i]);
        minimum[i]=heap_caps_get_minimum_free_size(capabilities[i]);
        total[i]=heap_caps_get_total_size(capabilities[i]);
    }
    int length=snprintf(out,cap,
        "internal_free=%zu,internal_largest=%zu,internal_minimum=%zu,internal_total=%zu,"
        "psram_free=%zu,psram_largest=%zu,psram_minimum=%zu,psram_total=%zu,tasks=%u",
        free_bytes[0],largest[0],minimum[0],total[0],free_bytes[1],largest[1],minimum[1],total[1],
        (unsigned)uxTaskGetNumberOfTasks());
    return length<0 || (size_t)length>=cap ? ESP_ERR_NO_MEM : ESP_OK;
}
typedef struct {char *data;size_t used,capacity;bool failed;} notice_writer_t;
static void notice_write(notice_writer_t *writer,const char *format,...)
{
    if(writer->failed)return;
    va_list args;va_start(args,format);
    int length=vsnprintf(writer->data+writer->used,writer->capacity-writer->used,format,args);
    va_end(args);
    if(length<0 || (size_t)length>=writer->capacity-writer->used)writer->failed=true;
    else writer->used+=(size_t)length;
}
static void notice_string(notice_writer_t *writer,const char *value)
{
    notice_write(writer,"\"");
    for(const unsigned char *p=(const unsigned char *)value;*p && !writer->failed;++p){
        if(*p=='"' || *p=='\\')notice_write(writer,"\\%c",*p);
        else if(*p<32)notice_write(writer,"\\u%04x",*p);
        else notice_write(writer,"%c",*p);
    }
    notice_write(writer,"\"");
}
// Store operations are thread-safe and contain no LVGL, so diagnostics do not
// serialize potentially long JSON under the display lock.
static esp_err_t notices_snapshot(void)
{
    const size_t capacity=128*1024;
    ouse_notification_t *items=heap_caps_calloc(OUSE_NOTIFICATION_CAPACITY,sizeof(*items),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    resource=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!items || !resource){free(items);free(resource);resource=NULL;return ESP_ERR_NO_MEM;}
    uint32_t revision=ouse_notifications_generation();
    unsigned count=ouse_notifications_list(NULL,items,OUSE_NOTIFICATION_CAPACITY),ongoing=0;
    for(unsigned i=0;i<count;++i)if(items[i].ongoing)++ongoing;
    notice_writer_t writer={.data=(char *)resource,.capacity=capacity};
    notice_write(&writer,"{\"count\":%u,\"ordinary\":%u,\"ongoing\":%u,\"revision\":%lu,\"items\":[",
                 count,count-ongoing,ongoing,(unsigned long)revision);
    for(unsigned i=0;i<count;++i){
        const ouse_notification_t *item=&items[i];
        notice_write(&writer,"%s{\"serial\":%lu,\"id\":",i?",":"",(unsigned long)item->serial);
        notice_string(&writer,item->id);notice_write(&writer,",\"appId\":");notice_string(&writer,item->app_id);
        notice_write(&writer,",\"title\":");notice_string(&writer,item->title);
        notice_write(&writer,",\"body\":");notice_string(&writer,item->body);
        notice_write(&writer,",\"payload\":");notice_string(&writer,item->payload);
        notice_write(&writer,",\"ongoing\":%s,\"createdMs\":%llu,\"updatedMs\":%llu}",
                     item->ongoing?"true":"false",(unsigned long long)item->created_ms,(unsigned long long)item->updated_ms);
    }
    notice_write(&writer,"]}");free(items);
    if(writer.failed){free(resource);resource=NULL;return ESP_ERR_NO_MEM;}
    resource_size=writer.used;return ESP_OK;
}
esp_err_t ouse_debug_command(const char *verb,const char *payload,char *out,size_t cap)
{
    if(!ouse_system_debug_enabled())return ESP_ERR_INVALID_STATE;
    if(!strcmp(verb,"DBG_STORAGE")){
        ouse_storage_stats_t s={0};(void)ouse_storage_get_stats(&s);char json[384];
        int n=snprintf(json,sizeof(json),"{\"ready\":%s,\"revision\":%lu,\"totalBytes\":%zu,\"usedBytes\":%zu,\"freeBytes\":%zu,\"packageBytes\":%zu,\"privateBytes\":%zu,\"packageCount\":%u,\"privateApps\":%u,\"lastError\":\"%s\"}",
            s.ready ? "true" : "false",(unsigned long)s.revision,s.total_bytes,s.used_bytes,s.free_bytes,
            s.package_bytes,s.private_bytes,s.package_count,s.private_apps,esp_err_to_name(s.last_error));
        if(n<0 || (size_t)n>=sizeof(json) || cap<(size_t)n*2+1)return ESP_ERR_NO_MEM;
        hex((const unsigned char *)json,(size_t)n,out);return ESP_OK;
    }
    if(!strcmp(verb,"DBG_BUILD")){
        if(cap<65)return ESP_ERR_NO_MEM;
        hex(esp_app_get_description()->app_elf_sha256,32,out);return ESP_OK;
    }
    if(!strcmp(verb,"DBG_RUNTIME") || !strcmp(verb,"DBG_IME")){
        esp_err_t error=call_ui(!strcmp(verb,"DBG_RUNTIME")?U_RUNTIME:U_IME);if(error!=ESP_OK)return error;
        int n=snprintf(out,cap,"%s",request.text);
        return n<0 || (size_t)n>=cap ? ESP_ERR_NO_MEM : ESP_OK;
    }
    if(!strcmp(verb,"DBG_HEAP"))return heap_state(out,cap);
    if(!strcmp(verb,"DBG_SDLAYOUT")){
        extern esp_err_t ouse_board_sd_layout(char *,size_t) __attribute__((weak));
        return ouse_board_sd_layout ? ouse_board_sd_layout(out,cap) : ESP_ERR_NOT_SUPPORTED;
    }
    if(!strcmp(verb,"DBG_SDFORMAT")){
        extern esp_err_t ouse_board_sd_format(const char *) __attribute__((weak));
        esp_err_t error=ouse_board_sd_format ? ouse_board_sd_format(payload) : ESP_ERR_NOT_SUPPORTED;
        if(error==ESP_OK)snprintf(out,cap,"formatted=FAT32");
        return error;
    }
    if(!strcmp(verb,"DBG_SDRETRY"))return ouse_files_recover();
    if(!strcmp(verb,"DBG_SDEJECT"))return ouse_files_eject();
    if(!strcmp(verb,"DBG_SD")){
        ouse_sd_state_t state;esp_err_t error=ouse_sd_get(&state);if(error!=ESP_OK)return error;
        ouse_files_state_t files;ouse_files_get(&files);
        int length=snprintf(out,cap,"probing=%u,detected=%u,readable=%u,capacity=%llu,sector=%lu,name=%.7s,error=%s,mounted=%u,ready=%u,files=%u,free=%llu,fs_error=%s,mount_result=%d",
            state.probing,state.detected,state.readable,(unsigned long long)state.capacity_bytes,
            (unsigned long)state.sector_bytes,state.name,esp_err_to_name(state.error),files.mounted,files.ready,files.count,(unsigned long long)files.free_bytes,esp_err_to_name(files.error),files.mount_result);
        return length<0 || (size_t)length>=cap ? ESP_ERR_NO_MEM : ESP_OK;
    }
    if(!strcmp(verb,"DBG_USB")){
        ouse_usb_state_t s;ouse_usb_get(&s);ouse_audio_state_t a;ouse_audio_get(&a);
        extern void ouse_usb_driver_diag(uint32_t*,uint32_t*,uint32_t*);uint32_t packets,bytes,feedback;ouse_usb_driver_diag(&packets,&bytes,&feedback);
        int n=snprintf(out,cap,"mounted=%u,suspended=%u,audio_enabled=%u,speaker=%u,microphone=%u,running=%u,task=%lu,error=%d,volume=%u,muted=%u,samples=%lu,nonzero=%lu,peak=%lu,underruns=%lu,overruns=%lu,rxirqs=%lu,rxbytes=%lu,feedback=%lu",
            s.mounted,s.suspended,s.audio_enabled,s.speaker,s.microphone,s.audio_running,(unsigned long)s.audio_task,s.error,a.volume,a.muted,
            (unsigned long)s.speaker_samples,(unsigned long)s.speaker_nonzero,(unsigned long)s.speaker_peak,(unsigned long)s.underruns,(unsigned long)s.overruns,(unsigned long)packets,(unsigned long)bytes,(unsigned long)feedback);
        return n<0 || (size_t)n>=cap ? ESP_ERR_NO_MEM : ESP_OK;
    }
    if(!strcmp(verb,"DBG_FINFO")){
        size_t size;uint32_t crc;esp_err_t error=ouse_files_checksum(payload,&size,&crc);
        if(error==ESP_OK)snprintf(out,cap,"%u:%08lx",(unsigned)size,(unsigned long)crc);
        return error;
    }
    if(!strcmp(verb,"DBG_FBEGIN")){
        char path[128];unsigned size;unsigned long crc;int used=0;
        if(sscanf(payload,"%127[^:]:%u:%lx%n",path,&size,&crc,&used)!=3 || payload[used])return ESP_ERR_INVALID_ARG;
        esp_err_t error=ouse_files_upload_begin(path,size,(uint32_t)crc);if(error==ESP_OK)snprintf(out,cap,"ready=1");return error;
    }
    if(!strcmp(verb,"DBG_FDATA")){
        unsigned offset;int used=0;unsigned char data[512];size_t length;
        if(sscanf(payload,"%x:%n",&offset,&used)!=1 || !used || !ouse_hex_decode(payload+used,data,sizeof(data),&length))return ESP_ERR_INVALID_ARG;
        esp_err_t error=ouse_files_upload_data(offset,data,length);if(error==ESP_OK)snprintf(out,cap,"%u",offset+(unsigned)length);return error;
    }
    if(!strcmp(verb,"DBG_FEND")){esp_err_t error=ouse_files_upload_end();if(error==ESP_OK)snprintf(out,cap,"saved=1");return error;}
    if(!strcmp(verb,"DBG_FABORT")){
        ouse_files_upload_abort();
        esp_err_t error=!strcmp(payload,"-") ? ESP_OK : ouse_files_discard_partial(payload);
        if(error==ESP_OK)snprintf(out,cap,"aborted=1");
        return error;
    }
    if(!strcmp(verb,"DBG_CHECK")){
        if(!heap_caps_check_integrity_all(true))return ESP_FAIL;
        snprintf(out,cap,"ok=1");return ESP_OK;
    }
#if CONFIG_OUSE_DEVICE_DEBUG
    if(!strcmp(verb,"DBG_BLE")){
        ouse_ble_state_t state;ouse_ble_get(&state);
        snprintf(out,cap,"enabled=%u,ready=%u,starting=%u,scan=%u,advertising=%u,connected=%u,encrypted=%u,bonded=%u,pending=%u,pair_id=%lu,number=%06lu,peers=%u,bonds=%u,address=%s,peer=%s,error=%d",state.enabled,state.ready,state.starting,state.scanning,state.advertising,state.connected,state.encrypted,state.bonded,state.pair_pending,(unsigned long)state.pair_id,(unsigned long)state.number,state.count,state.bonds,state.address,state.peer,state.error);return ESP_OK;
    }
    if(!strcmp(verb,"DBG_BLEON")){if(strcmp(payload,"0") && strcmp(payload,"1"))return ESP_ERR_INVALID_ARG;return ouse_ble_enable(payload[0]=='1');}
    if(!strcmp(verb,"DBG_BLESCAN"))return ouse_ble_scan();
    if(!strcmp(verb,"DBG_BLEADV"))return ouse_ble_discoverable();
    if(!strcmp(verb,"DBG_BLEPAIR")){const char *cursor=payload;unsigned id,accept;if(!decimal(&cursor,&id,':') || !decimal(&cursor,&accept,0) || accept>1)return ESP_ERR_INVALID_ARG;return ouse_ble_confirm(id,accept!=0);}
#endif
    if(!strcmp(verb,"DBG_LOG") || !strcmp(verb,"DBG_LOGSTATE")){
        ouse_journal_view_t *view=heap_caps_malloc(sizeof(*view),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!view)return ESP_ERR_NO_MEM;
        ouse_journal_get(view);
        if(!strcmp(verb,"DBG_LOGSTATE")){
            snprintf(out,cap,"ready=%u,busy=%u,page=%u,pages=%u,bytes=%zu,dropped=%u,error=%d,exporting=%u,export_error=%d",view->ready,view->busy,view->page,view->pages,view->bytes,view->dropped,view->error,view->exporting,view->export_error);free(view);return ESP_OK;
        }
        free(resource);resource=NULL;resource_size=0;
        const char *text=*view->text?view->text:"No logs\n";resource_size=strlen(text);
        resource=heap_caps_malloc(resource_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!resource){free(view);resource_size=0;return ESP_ERR_NO_MEM;}
        memcpy(resource,text,resource_size);free(view);
        snprintf(out,cap,"%u:%08lx",(unsigned)resource_size,(unsigned long)ouse_crc32(resource,resource_size));return ESP_OK;
    }
    if(!strcmp(verb,"DBG_ERROR")){
        free(resource);resource=heap_caps_calloc(1,2048,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!resource)return ESP_ERR_NO_MEM;
        ouse_runtime_get_error((char *)resource,2048);resource_size=strlen((char *)resource);
        snprintf(out,cap,"%u:%08lx",(unsigned)resource_size,(unsigned long)ouse_crc32(resource,resource_size));return ESP_OK;
    }
    if(!strcmp(verb,"DBG_REPAINT")){
        esp_err_t error=call_ui(U_REPAINT);
        if(error==ESP_OK)snprintf(out,cap,"queued=1");
        return error;
    }
#if CONFIG_OUSE_DEVICE_DEBUG
    if(!strcmp(verb,"DBG_FLUSH")){
        if(!strcmp(payload,"status"))request.down=-1;
        else if(!strcmp(payload,"0") || !strcmp(payload,"1"))request.down=payload[0]-'0';
        else return ESP_ERR_INVALID_ARG;
        esp_err_t error=call_ui(U_FLUSH);
        if(error==ESP_OK)snprintf(out,cap,"enabled=%d,waits=%lu",request.flush_enabled ? 1 : 0,(unsigned long)request.flush_waits);
        return error;
    }
    if(!strcmp(verb,"DBG_LCD")){
        if(!strcmp(payload,"status"))request.down=-1;
        else if(!strcmp(payload,"0") || !strcmp(payload,"1"))request.down=payload[0]-'0';
        else return ESP_ERR_INVALID_ARG;
        esp_err_t error=call_ui(U_LCD);
        if(error==ESP_OK)snprintf(out,cap,"enabled=%d,skipped=%lu",request.lcd_enabled ? 1 : 0,(unsigned long)request.lcd_skipped);
        return error;
    }
#endif
    if(!strcmp(verb,"DBG_IDLE")){
        if(strcmp(payload,"0") && strcmp(payload,"1"))return ESP_ERR_INVALID_ARG;
        esp_err_t error=idle_wait(payload[0]=='1');
        if(error==ESP_OK)snprintf(out,cap,"wait=%d",busy_idle ? 0 : 1);
        return error;
    }
    if(!strcmp(verb,"DBG_STATE")){
        esp_err_t e=call_ui(U_STATE);if(e!=ESP_OK)return e;
        size_t n=strlen(request.text);if(n*2+1>cap)return ESP_ERR_NO_MEM;hex((unsigned char *)request.text,n,out);return ESP_OK;
    }
    if(!strcmp(verb,"DBG_TOUCH")||!strcmp(verb,"DBG_TOUCHES")){
        const char *p=payload;unsigned x,y,down;request.contact=0;
        if(!strcmp(verb,"DBG_TOUCHES")&&(!decimal(&p,&request.contact,':')||request.contact>1))return ESP_ERR_INVALID_ARG;
        if(!decimal(&p,&x,':')||!decimal(&p,&y,':')||!decimal(&p,&down,0)||x>INT_MAX||y>INT_MAX||down>1)return ESP_ERR_INVALID_ARG;
        request.x=(int)x;request.y=(int)y;request.down=(int)down;return call_ui(U_TOUCH);
    }
    if(!strcmp(verb,"DBG_HOME"))return call_ui(U_HOME);
    if(!strcmp(verb,"DBG_OPEN") || !strcmp(verb,"DBG_CLOSE")){
        if(strlen(payload)>=48)return ESP_ERR_INVALID_ARG;
        request.id=payload;return call_ui(!strcmp(verb,"DBG_OPEN")?U_OPEN:U_CLOSE);
    }
    if(!strcmp(verb,"DBG_FREE")){free(resource);resource=NULL;resource_size=0;return ESP_OK;}
    if(!strcmp(verb,"DBG_TREE") || !strcmp(verb,"DBG_SHOT") || !strcmp(verb,"DBG_NOTICES")){
        free(resource);resource=NULL;resource_size=0;
        esp_err_t e;
        if(!strcmp(verb,"DBG_TREE")){
            resource=heap_caps_malloc(65536,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!resource)return ESP_ERR_NO_MEM;
            e=call_ui(U_TREE);if(e==ESP_OK)resource_size=strlen((char *)resource);
        }else if(!strcmp(verb,"DBG_NOTICES"))e=notices_snapshot();
        else e=screenshot();
        if(e!=ESP_OK){free(resource);resource=NULL;return e;}
        snprintf(out,cap,"%u:%08lx",(unsigned)resource_size,(unsigned long)ouse_crc32(resource,resource_size));return ESP_OK;
    }
    if(!strcmp(verb,"DBG_READ")){
        const char *p=payload;unsigned offset,n;
        if(!decimal(&p,&offset,':')||!decimal(&p,&n,0)||!resource||n>256||offset>resource_size||n>resource_size-offset||n*2u+1>cap)return ESP_ERR_INVALID_ARG;
        hex(resource+offset,n,out);return ESP_OK;
    }
    if(!strcmp(verb,"DBG_TEST"))return selftest(out,cap);
    return ESP_ERR_NOT_SUPPORTED;
}
void ouse_debug_poll(void)
{
    ouse_files_upload_poll();
    static bool disabled;
    if(ouse_system_debug_enabled()){disabled=false;return;}
    if(disabled)return;
    if(call_ui(U_RESET)!=ESP_OK)return;
    ouse_files_upload_abort();
    (void)idle_wait(true);
    free(resource);resource=NULL;resource_size=0;
    disabled=true;
}
