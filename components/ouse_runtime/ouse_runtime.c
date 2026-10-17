// SPDX-License-Identifier: MIT
// One task owns all QuickJS values. LVGL sees only bounded plain-data models.
#include "ouse_runtime.h"
#include "ouse_budget.h"
#include "ouse_core.h"
#include "ouse_devices.h"
#include "ouse_ime.h"
#include "ouse_notifications.h"
#include "ouse_controls.h"
#include "ouse_storage.h"
#include "ouse_tasks.h"
#include "ouse_net.h"
#include "ouse_alarm.h"
#include "ouse_clock.h"
#include "ouse_media.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "quickjs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define MAX_NODES 96
#define MAX_TIMERS 8
#define MAX_TEXT 256
#define MAX_SOURCE 32768
#define CALL_BUDGET_US 12000
#define BACKGROUND_BUDGET_US 4000
#define STARTUP_BUDGET_US 100000
#define MAX_JOBS 8
#define MAX_EVENTS 16
#define WORKER_STACK_BYTES (64 * 1024)
#define JS_STACK_BYTES (40 * 1024)
#define MAX_ASSET_BYTES OUSE_RUNTIME_IMAGE_BYTES_MAX
#define MAX_STORAGE_PENDING 8
#define MAX_STORAGE_JOBS 64
#define MAX_NET_PENDING 2
#define MAX_NET_JOBS 16
#define MAX_PAGES 16
#define MAX_PAGE_STACK 8

typedef enum { NAV_IDLE, NAV_REQUESTED, NAV_DETACHED, NAV_MODEL_READY } navigation_stage_t;
typedef enum { NAV_OPEN, NAV_PUSH, NAV_BACK, NAV_REPLACE } navigation_action_t;
typedef enum { CONTAINER, LABEL, BUTTON, SLIDER, IMAGE, TEXT_INPUT, SWITCH,
    CHECKBOX, DROPDOWN, PROGRESS, SPINNER, ARC, CARD, LIST, LIST_ITEM, TOOLBAR,
    DIVIDER, DIALOG, BOTTOM_SHEET } node_type_t;
typedef enum { EVENT_CLICK, EVENT_CHANGE, EVENT_FOCUS, EVENT_BLUR, EVENT_SUBMIT, EVENT_DISMISS, EVENT_COUNT } widget_event_t;
typedef struct {
    bool bg_set, color_set, border_set, opacity_set, accent_set, thumb_set;
    uint32_t bg, color, border, accent, thumb;
    int opacity;
} node_state_style_t;
typedef struct {
    bool bg_set, color_set, border_set, hidden, disabled, accent_set, thumb_set;
    uint32_t bg, color, border, accent, thumb;
    int radius, border_width, opacity, align, flex_grow;
    node_state_style_t states[3]; // pressed, checked, disabled
} node_style_t;
// This is the only node representation exchanged with the UI task.
typedef struct {
    char id[32], text[MAX_TEXT+1], image[32], text_key[64], placeholder[129], options[513];
    char translated[2][MAX_TEXT+1];
    char subtitle[MAX_TEXT+1], subtitle_key[64], subtitle_translated[2][MAX_TEXT+1];
    unsigned font_sp, padding, gap, flow, max_length;
    bool fill_w, fill_h, content_h, scroll, multiline;
    int parent, x,y,w,h,min,max,value,input_type,keyboard_mode;
    node_type_t type;
    node_style_t style;
    uint32_t text_revision, value_revision, style_revision, focus_revision, image_revision;
    bool focus;
} node_model_t;
typedef struct {
    node_model_t model;
    JSValue handlers[EVENT_COUNT]; // worker only
    lv_obj_t *object, *caption, *subtitle, *overlay; // UI only; never dereferenced by worker
    uint32_t shown_text, shown_value, shown_style, shown_focus, shown_image;
} node_t;
typedef struct {
    JSValue handler;
    uint32_t id, period, due, remaining;
    bool interval;
} script_timer_t;
typedef struct {
    unsigned node, event;
    uint32_t generation;
    int value;
    char text[MAX_TEXT+1];
} input_event_t;
typedef struct {
    unsigned locale, font_percent, density, brightness;
    bool dark;
    int width, height, app_width, app_height;
    bool keyboard_visible;
    int keyboard_height, keyboard_inset;
    uint32_t revisions[3];
    ouse_theme_colors_t colors;
} ui_snapshot_t;
typedef struct storage_job storage_job_t;
typedef struct net_job net_job_t;
typedef struct instance {
    struct instance *next;        // worker only
    atomic_uint references;
    SemaphoreHandle_t model_lock, validated;
    QueueHandle_t events;
    char id[48], *page, *script;
    ouse_script_image_t images[16];
    lv_image_dsc_t image_descriptors[16];
    char image_ids[16][32];
    unsigned image_count;
    ouse_script_asset_t assets[OUSE_RUNTIME_ASSETS_MAX];
    char asset_ids[OUSE_RUNTIME_ASSETS_MAX][32];
    unsigned asset_count;
    storage_job_t *storage[MAX_STORAGE_PENDING]; // JS task owns slots/resolvers
    net_job_t *network[MAX_NET_PENDING];
    ouse_task_owner_t owner;
    JSValue task_handler;
    char background_reason[64];
    int64_t background_since_us;
    bool background_allowed, notifications_allowed, network_allowed, media_allowed, alarms_allowed;
    JSRuntime *runtime;
    JSContext *context;
    lv_obj_t *root, *viewport;    // UI only
    lv_timer_t *pump;            // UI only
    bool applying_model;         // UI-only synthetic event suppression
    uint32_t display_revision, core_display_revision, applied_theme_revision, applied_display_revision; // UI-only revision tracking
    node_t nodes[MAX_NODES];
    script_timer_t timers[MAX_TIMERS];
    unsigned node_count;
    bool dp_units, parsing_page;
    JSValue translations, changes[6], lifecycle_handler, notification_handler, navigation_handler;
    JSValue page_roots[MAX_PAGES]; // worker only; static JSON, never application objects
    char page_ids[MAX_PAGES][32];
    unsigned page_count, page_stack[MAX_PAGE_STACK], page_depth;
    uint32_t revisions[6], next_timer;
    ouse_budget_t budget;
    size_t sampled_js_bytes;
    int64_t sampled_memory_us;
    bool interrupted, rejection_overflow;
    unsigned jobs_run; // bounded Promise work shared by this execution slice
    JSValue rejected_promises[MAX_JOBS], rejection_reasons[MAX_JOBS];
    // Shared model fields below are protected by model_lock.
    bool visible, closed, ready, built, faulted, fault_shown, background_active, home_requested;
    bool validation_only;
    navigation_stage_t navigation_stage;
    navigation_action_t navigation_action;
    unsigned navigation_target, navigation_previous;
    bool navigation_event_pending;
    uint32_t generation, lifecycle_revision;
    unsigned pending_commands, dropped_events;
    int command_values[4];
    bool brightness_save;
    char fault_message[240];
    ui_snapshot_t snapshot;
    esp_err_t validation_result;
    // Worker-local execution policy and cached UI snapshot.
    ui_snapshot_t worker_ui;
    bool worker_visible, worker_runnable, worker_background, policy_known;
    bool timer_clock_frozen, policy_dirty;
    uint32_t seen_lifecycle;
} instance_t;
struct storage_job {
    instance_t *app;
    unsigned slot;
    uint32_t token;
    JSValue resolve, reject; // JS task only
    char *result;            // native callback transfers ownership through queue
    esp_err_t error;
    bool completed;         // JS task only, after receiving the completion
};
struct net_job {
    instance_t *app;
    unsigned slot;
    uint32_t token;
    JSValue resolve,reject;
    ouse_net_result_t *result;
    bool completed,bytes;
};
static QueueHandle_t net_completions;
static unsigned net_job_count;
static QueueHandle_t storage_completions;
static unsigned storage_job_count; // JS task only, includes closed in-flight jobs
static instance_t *instances; // worker-owned list
static QueueHandle_t launches;
static TaskHandle_t worker_task;
static portMUX_TYPE stats_lock=portMUX_INITIALIZER_UNLOCKED;
static ouse_runtime_stats_t runtime_stats;
static ouse_runtime_diagnostics_t runtime_diagnostics;
static unsigned error_count;
static uint32_t now_ms(void) {return (uint32_t)(esp_timer_get_time()/1000);}
static void model_lock(instance_t *app) {xSemaphoreTake(app->model_lock,portMAX_DELAY);}
static void model_unlock(instance_t *app) {xSemaphoreGive(app->model_lock);}
static bool navigation_pending(instance_t *app)
{model_lock(app);bool pending=app->navigation_stage!=NAV_IDLE;model_unlock(app);return pending;}
static void wake_worker(void) {if(worker_task)xTaskNotifyGive(worker_task);}
static void release_instance(instance_t *app)
{
    if(atomic_fetch_sub(&app->references,1)!=1)return;
    ouse_task_owner_close(&app->owner);
    for(unsigned i=0;i<app->image_count;++i)free((void *)app->image_descriptors[i].data);
    for(unsigned i=0;i<app->asset_count;++i)free((void *)app->assets[i].data);
    free(app->page);free(app->script);
    if(app->events)vQueueDelete(app->events);
    if(app->validated)vSemaphoreDelete(app->validated);
    if(app->model_lock)vSemaphoreDelete(app->model_lock);
    memset(app,0,sizeof(*app));free(app);
}
static int interrupt(JSRuntime *runtime,void *opaque)
{
    (void)runtime;instance_t *app=opaque;
    if(ouse_budget_exceeded(&app->budget)){app->interrupted=true;return 1;}
    return 0;
}
static void begin_call(instance_t *app)
{
    app->interrupted=false;app->jobs_run=0;
    ouse_budget_begin(&app->budget,app->worker_visible ? CALL_BUDGET_US : BACKGROUND_BUDGET_US,false);
    JS_UpdateStackTop(app->runtime);
}
static void begin_startup(instance_t *app)
{
    app->interrupted=false;app->jobs_run=0;
    ouse_budget_begin(&app->budget,STARTUP_BUDGET_US,true);JS_UpdateStackTop(app->runtime);
}
static void log_execution_timeout(instance_t *app,const char *kind,const char *name,int64_t started,int64_t remaining)
{
    ESP_LOGE("OuseJS","%s timeout: kind=%s; entry=%s; clock=%s; scheduled=%lld us; limit=%lld us; elapsed=%lld us; wall_guard=%u; checkpoints=%u",
        app->id,kind,name ? name : "-",ouse_budget_clock_name(),(long long)app->budget.scheduled_us,
        (long long)app->budget.limit_us,(long long)(esp_timer_get_time()-started),app->budget.wall_exceeded,app->budget.samples);
    (void)remaining;
}
static JSValue call_js(instance_t *app,const char *kind,const char *name,JSValueConst function,JSValueConst self,int argc,JSValueConst *argv)
{
    int64_t started=esp_timer_get_time(),remaining=app->budget.limit_us-app->budget.scheduled_us;
    JSValue value=JS_Call(app->context,function,self,argc,argv);
    if(JS_IsException(value) && app->interrupted)log_execution_timeout(app,kind,name,started,remaining);
    return value;
}
static int run_promise_job(instance_t *app,JSContext **context)
{
    int64_t started=esp_timer_get_time(),remaining=app->budget.limit_us-app->budget.scheduled_us;
    int result=JS_ExecutePendingJob(app->runtime,context);
    if(result<0 && app->interrupted)log_execution_timeout(app,"promise",NULL,started,remaining);
    return result;
}
static void promise_rejected(JSContext *context,JSValueConst promise,JSValueConst reason,bool handled,void *opaque)
{
    instance_t *app=opaque;
    if (handled) {
        for (unsigned i=0;i<MAX_JOBS;++i) if (!JS_IsUndefined(app->rejected_promises[i]) &&
            JS_IsStrictEqual(context,app->rejected_promises[i],promise)) {
            JS_FreeValue(context,app->rejected_promises[i]); JS_FreeValue(context,app->rejection_reasons[i]);
            app->rejected_promises[i]=app->rejection_reasons[i]=JS_UNDEFINED;
            break;
        }
    } else {
        for (unsigned i=0;i<MAX_JOBS;++i) if (JS_IsUndefined(app->rejected_promises[i])) {
            app->rejected_promises[i]=JS_DupValue(context,promise);
            app->rejection_reasons[i]=JS_DupValue(context,reason);
            return;
        }
        app->rejection_overflow=true;
    }
}
static instance_t *from_js(JSContext *context) { return JS_GetContextOpaque(context); }
static void cancel_storage_jobs(instance_t *app);
static void cancel_net_jobs(instance_t *app);
static void fault(instance_t *app,const char *message)
{
    model_lock(app);
    bool first=!app->faulted;
    app->faulted=true;app->background_active=false;
    snprintf(app->fault_message,sizeof(app->fault_message),"%s",message);
    model_unlock(app);
    if(!first)return;
    ++error_count;
    for(unsigned i=0;i<MAX_TIMERS;++i){
        if(app->context)JS_FreeValue(app->context,app->timers[i].handler);
        app->timers[i].handler=JS_UNDEFINED;app->timers[i].id=0;
    }
    ouse_task_owner_close(&app->owner);
    cancel_storage_jobs(app);
    cancel_net_jobs(app);
    ESP_LOGE("OuseJS","%s: %s",app->id,message);
}
static void exception(instance_t *app)
{
    JSValue value=JS_GetException(app->context);
    // Disable the throwing handler only for formatting, while keeping a fresh
    // deadline. Custom toString implementations are still interrupted.
    bool timed_out=app->interrupted;
    begin_call(app);
    const char *message=JS_ToCString(app->context,value);
    char copy[240];
    snprintf(copy,sizeof(copy),"%s",timed_out ? "JavaScript execution time exceeded" : message ? message : "JavaScript exception");
    if (message) JS_FreeCString(app->context,message);
    else { JSValue nested=JS_GetException(app->context); JS_FreeValue(app->context,nested); }
    JS_FreeValue(app->context,value);
    fault(app,copy);
}
static bool result_ok(instance_t *app,JSValue result)
{
    bool success=!JS_IsException(result);
    JS_FreeValue(app->context,result);
    if (!success) { exception(app); return false; }
    JSContext *context=NULL;
    model_lock(app);bool runnable=!app->closed && !app->faulted && app->navigation_stage==NAV_IDLE && (app->visible || app->background_active);model_unlock(app);
    if(!runnable)return true;
    while (app->jobs_run<MAX_JOBS && JS_IsJobPending(app->runtime)) {
        model_lock(app);runnable=!app->closed && !app->faulted && app->navigation_stage==NAV_IDLE && (app->visible || app->background_active);model_unlock(app);
        if(!runnable || ouse_budget_slice_done(&app->budget))return true;
        ++app->jobs_run;
        if (run_promise_job(app,&context)<0) { exception(app); return false; }
    }
    // A bounded queue slice is a yield, not an application failure. Pending
    // jobs may install handlers for rejections in a later runnable slice.
    if (JS_IsJobPending(app->runtime)) return true;
    if (app->rejection_overflow) { fault(app,"Too many unhandled promise rejections"); return false; }
    for (unsigned i=0;i<MAX_JOBS;++i) if (!JS_IsUndefined(app->rejected_promises[i])) {
        JSValue rejection=app->rejection_reasons[i]; app->rejection_reasons[i]=JS_UNDEFINED;
        JS_FreeValue(app->context,app->rejected_promises[i]); app->rejected_promises[i]=JS_UNDEFINED;
        JS_Throw(app->context,rejection); exception(app); return false;
    }
    return true;
}
static bool copy_string(JSContext *context,JSValueConst value,char *out,size_t capacity)
{
    if (!JS_IsString(value)) return false;
    size_t size=0; const char *text=JS_ToCStringLen(context,&size,value);
    if (!text) return false;
    bool valid=size<capacity && memchr(text,0,size)==NULL;
    if (valid) { memcpy(out,text,size); out[size]=0; }
    JS_FreeCString(context,text);
    return valid;
}
static JSValue json_property(JSContext *context,JSValueConst object,const char *key)
{
    // Retained JSON is private to the worker. Removing its prototype lazily
    // prevents a later application Object.prototype getter from turning page
    // parsing or translations into reentrant application JavaScript.
    if(JS_IsObject(object) && JS_SetPrototype(context,object,JS_NULL)<0)return JS_EXCEPTION;
    return JS_GetPropertyStr(context,object,key);
}
static JSValue source_property(instance_t *app,JSValueConst object,const char *key)
{return app->parsing_page ? json_property(app->context,object,key) : JS_GetPropertyStr(app->context,object,key);}
static node_t *find_node(instance_t *app,JSValueConst id)
{
    char key[32];if(!copy_string(app->context,id,key,sizeof(key)))return NULL;
    for(unsigned i=0;i<app->node_count;++i)if(!strcmp(app->nodes[i].model.id,key))return &app->nodes[i];
    return NULL;
}
static const lv_image_dsc_t *find_image(instance_t *app,const char *id);
static bool modal_widget(node_type_t type) {return type==DIALOG || type==BOTTOM_SHEET;}
static bool container_widget(node_type_t type) {return type==CONTAINER || type==CARD || type==LIST || type==LIST_ITEM || type==TOOLBAR || modal_widget(type);}
static bool text_widget(node_type_t type) {return type==LABEL || type==BUTTON || type==TEXT_INPUT || type==CHECKBOX || type==CARD || type==LIST_ITEM || type==TOOLBAR || modal_widget(type);}
static bool node_visible_locked(instance_t *app,unsigned index,bool enabled)
{
    if(!app->visible || app->closed)return false;
    for(int n=(int)index;n>=0;n=app->nodes[n].model.parent)
        if(app->nodes[n].model.style.hidden || (enabled && app->nodes[n].model.style.disabled))return false;
    return true;
}
static void set_node_hidden_locked(instance_t *app,node_t *node,bool hidden)
{
    if(!hidden && modal_widget(node->model.type))for(unsigned i=0;i<app->node_count;++i){
        node_t *other=&app->nodes[i];if(other!=node && modal_widget(other->model.type) && !other->model.style.hidden){other->model.style.hidden=true;++other->model.style_revision;}
    }
    node->model.style.hidden=hidden;++node->model.style_revision;
}
static JSValue ui_visibility(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc ? find_node(app,argv[0]) : NULL;
    if(!node || argc!=(action==3 ? 2 : 1) || (action==3 && !JS_IsBool(argv[1])))return JS_ThrowTypeError(context,"Expected widget id and optional enabled boolean");
    model_lock(app);bool visible=app->visible && !app->closed;
    if(action==2)visible=node_visible_locked(app,(unsigned)(node-app->nodes),false);
    else if(action==3){node->model.style.disabled=!JS_ToBool(context,argv[1]);++node->model.style_revision;}
    else if(action==0 || visible || !modal_widget(node->model.type))set_node_hidden_locked(app,node,action==0);
    else {model_unlock(app);return JS_FALSE;}
    model_unlock(app);return action==2 ? JS_NewBool(context,visible) : JS_TRUE;
}
static bool value_widget(node_type_t type) {return type==SLIDER || type==SWITCH || type==CHECKBOX || type==DROPDOWN || type==PROGRESS || type==ARC;}
static JSValue ui_text(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int setter)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc ? find_node(app,argv[0]) : NULL;
    if(!node || !text_widget(node->model.type))return JS_ThrowTypeError(context,"Expected text widget id");
    char text[MAX_TEXT+1];
    if(setter){
        if(argc!=2 || !copy_string(context,argv[1],text,sizeof(text)))return JS_ThrowTypeError(context,"Expected text <=256 UTF-8 bytes");
        model_lock(app);strcpy(node->model.text,text);node->model.text_key[0]=0;++node->model.text_revision;model_unlock(app);
        return JS_UNDEFINED;
    }
    model_lock(app);strcpy(text,node->model.text);model_unlock(app);return JS_NewString(context,text);
}
static JSValue ui_set_image(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc==2 ? find_node(app,argv[0]) : NULL;char id[32];
    if(!node || node->model.type!=IMAGE || !copy_string(context,argv[1],id,sizeof(id)) || !find_image(app,id))return JS_ThrowTypeError(context,"setImage expects image widget and packaged image id");
    model_lock(app);strcpy(node->model.image,id);++node->model.image_revision;model_unlock(app);return JS_UNDEFINED;
}
static JSValue assets_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);
    if(action==0){
        JSValue array=JS_NewArray(context);unsigned at=0;
        for(unsigned i=0;i<app->image_count;++i){const lv_image_dsc_t *image=app->images[i].image;JSValue object=JS_NewObject(context);
            JS_SetPropertyStr(context,object,"id",JS_NewString(context,app->images[i].id));JS_SetPropertyStr(context,object,"type",JS_NewString(context,"image"));
            JS_SetPropertyStr(context,object,"bytes",JS_NewFloat64(context,(double)image->data_size));
            JS_SetPropertyStr(context,object,"width",JS_NewInt32(context,image->header.w));JS_SetPropertyStr(context,object,"height",JS_NewInt32(context,image->header.h));
            JS_SetPropertyUint32(context,array,at++,object);
        }
        for(unsigned i=0;i<app->asset_count;++i){const ouse_script_asset_t *asset=&app->assets[i];JSValue object=JS_NewObject(context);
            JS_SetPropertyStr(context,object,"id",JS_NewString(context,asset->id));JS_SetPropertyStr(context,object,"type",JS_NewString(context,asset->type==OUSE_SCRIPT_ASSET_TEXT ? "text" : "binary"));
            JS_SetPropertyStr(context,object,"bytes",JS_NewFloat64(context,(double)asset->size));JS_SetPropertyUint32(context,array,at++,object);
        }
        return array;
    }
    char id[32];if(argc!=1 || !copy_string(context,argv[0],id,sizeof(id)))return JS_ThrowTypeError(context,"Expected packaged text/binary asset id");
    const ouse_script_asset_t *asset=NULL;
    for(unsigned i=0;i<app->asset_count;++i)if(!strcmp(id,app->assets[i].id)){asset=&app->assets[i];break;}
    if(!asset || (action==1 && asset->type!=OUSE_SCRIPT_ASSET_TEXT))return JS_ThrowTypeError(context,"Asset not found or incorrect resource type");
    return action==1 ? JS_NewStringLen(context,(const char *)asset->data,asset->size) : JS_NewArrayBufferCopy(context,asset->data,asset->size);
}
static JSValue ui_value(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int setter)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc ? find_node(app,argv[0]) : NULL;
    if(!node || !value_widget(node->model.type))return JS_ThrowTypeError(context,"Expected value widget id");
    bool boolean=node->model.type==SWITCH || node->model.type==CHECKBOX;int32_t value;
    if(setter){
        if(argc!=2 || (boolean ? !JS_IsBool(argv[1]) : !JS_IsNumber(argv[1])))return JS_ThrowTypeError(context,"Expected widget value");
        if(boolean)value=JS_ToBool(context,argv[1]);
        else {double exact;if(JS_ToFloat64(context,&exact,argv[1])<0 || !isfinite(exact) || floor(exact)!=exact || exact<node->model.min || exact>node->model.max)return JS_ThrowRangeError(context,"Widget value out of range");value=(int32_t)exact;}
        model_lock(app);node->model.value=value;++node->model.value_revision;model_unlock(app);
    }
    model_lock(app);value=node->model.value;model_unlock(app);
    return boolean ? JS_NewBool(context,value) : JS_NewInt32(context,value);
}
static JSValue ui_focus(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int focus)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc==1 ? find_node(app,argv[0]) : NULL;
    if(!node || node->model.type!=TEXT_INPUT)return JS_ThrowTypeError(context,"Expected textInput id");
    model_lock(app);bool visible=node_visible_locked(app,(unsigned)(node-app->nodes),true);
    if(visible){node->model.focus=focus;++node->model.focus_revision;}
    model_unlock(app);return JS_NewBool(context,visible);
}
static int event_index(const char *event)
{
    const char *names[]={"click","change","focus","blur","submit","dismiss"};
    for(int i=0;i<EVENT_COUNT;++i)if(!strcmp(event,names[i]))return i;
    return -1;
}
static JSValue ui_on(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc==3 ? find_node(app,argv[0]) : NULL;char event[16];
    int index=-1;if(node && copy_string(context,argv[1],event,sizeof(event)))index=event_index(event);
    if(index<0 || (!JS_IsNull(argv[2]) && !JS_IsFunction(context,argv[2])))return JS_ThrowTypeError(context,"on(id, event, function|null): invalid binding");
    JS_FreeValue(context,node->handlers[index]);node->handlers[index]=JS_DupValue(context,argv[2]);return JS_UNDEFINED;
}
static JSValue native_home(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;instance_t *app=from_js(context);
    model_lock(app);bool visible=app->visible;if(visible)app->home_requested=true;model_unlock(app);
    return JS_NewBool(context,visible);
}
static JSValue lifecycle_snapshot(instance_t *app)
{
    model_lock(app);bool visible=app->visible,active=app->background_active,closed=app->closed,faulted=app->faulted;model_unlock(app);
    bool runnable=!closed && !faulted && (visible || active);
    JSContext *context=app->context;JSValue object=JS_NewObject(context);
    JS_SetPropertyStr(context,object,"state",JS_NewString(context,closed ? "closed" : faulted ? "faulted" : visible ? "foreground" : runnable ? "background" : "suspended"));
    JS_SetPropertyStr(context,object,"visible",JS_NewBool(context,visible));JS_SetPropertyStr(context,object,"runnable",JS_NewBool(context,runnable));
    JS_SetPropertyStr(context,object,"backgroundAllowed",JS_NewBool(context,app->background_allowed));JS_SetPropertyStr(context,object,"backgroundActive",JS_NewBool(context,active));
    JS_SetPropertyStr(context,object,"instanceId",JS_NewUint32(context,app->owner.instance));
    JS_SetPropertyStr(context,object,"alive",JS_NewBool(context,!closed));
    JS_SetPropertyStr(context,object,"reason",JS_NewString(context,active ? app->background_reason : ""));
    JS_SetPropertyStr(context,object,"sinceMs",JS_NewFloat64(context,active ? app->background_since_us/1000.0 : 0));return object;
}
static JSValue lifecycle_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int subscribe)
{
    (void)self;instance_t *app=from_js(context);
    if(!subscribe)return lifecycle_snapshot(app);
    if(argc!=1 || (!JS_IsNull(argv[0]) && !JS_IsFunction(context,argv[0])))return JS_ThrowTypeError(context,"onChange expects function or null");
    JS_FreeValue(context,app->lifecycle_handler);app->lifecycle_handler=JS_DupValue(context,argv[0]);return JS_UNDEFINED;
}
static JSValue background_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);
    if(action==3)return lifecycle_snapshot(app);
    char reason[64]="application";
    if(action==1 && (argc>1 || (argc==1 && (!copy_string(context,argv[0],reason,sizeof(reason)) || !reason[0]))))
        return JS_ThrowTypeError(context,"begin(reason): nonempty string up to 63 UTF-8 bytes");
    if(action==1 && !app->background_allowed)return JS_FALSE;
    model_lock(app);
    if(app->closed || app->faulted){model_unlock(app);return JS_FALSE;}
    if(action!=2){bool next=action==1;
        bool changed=app->background_active!=next || (next && strcmp(reason,app->background_reason));
        if(next){if(!app->background_active)app->background_since_us=esp_timer_get_time();strcpy(app->background_reason,reason);}
        else {app->background_reason[0]=0;app->background_since_us=0;}
        app->background_active=next;if(changed)++app->lifecycle_revision;
    }
    bool active=app->background_active;model_unlock(app);return JS_NewBool(context,active);
}
static JSValue task_snapshot(JSContext *context,const ouse_task_snapshot_t *task)
{
    JSValue object=JS_NewObject(context);
#define TNUM(name,value) JS_SetPropertyStr(context,object,name,JS_NewFloat64(context,(double)(value)))
#define TSTR(name,value) JS_SetPropertyStr(context,object,name,JS_NewString(context,value))
#define TBOOL(name,value) JS_SetPropertyStr(context,object,name,JS_NewBool(context,value))
    TNUM("id",task->id);TNUM("instanceId",task->owner.instance);TNUM("revision",task->revision);
    TSTR("appId",task->owner.app_id);TSTR("kind",ouse_task_kind_name(task->kind));TSTR("operation",task->operation);
    TSTR("state",ouse_task_state_name(task->state));TSTR("closePolicy",task->policy==OUSE_TASK_STOP_ON_CLOSE ? "stop" : "finish");
    TBOOL("cancelRequested",task->cancel_requested);TBOOL("cancellable",task->cancellable);TBOOL("detached",task->detached);
    TBOOL("resultPending",task->result_held);
    TSTR("cancelReason",task->cancel_reason);TNUM("errorCode",task->error);TSTR("error",esp_err_to_name(task->error));
    TNUM("createdMs",task->created_us/1000);TNUM("startedMs",task->started_us/1000);TNUM("finishedMs",task->finished_us/1000);
    JSValue result=JS_NULL;
    if(task->state>=OUSE_TASK_SUCCEEDED){
        result=JS_NewObject(context);JS_SetPropertyStr(context,result,"ok",JS_NewBool(context,task->state==OUSE_TASK_SUCCEEDED));
        if(task->result.present){
            if(task->kind==OUSE_TASK_WIFI){
                JS_SetPropertyStr(context,result,"enabled",JS_NewBool(context,task->result.value.wifi.enabled));
                JS_SetPropertyStr(context,result,"connected",JS_NewBool(context,task->result.value.wifi.connected));
                JS_SetPropertyStr(context,result,"networks",JS_NewUint32(context,task->result.value.wifi.networks));
                JS_SetPropertyStr(context,result,"reason",JS_NewInt32(context,task->result.value.wifi.reason));
                JS_SetPropertyStr(context,result,"ssid",JS_NewString(context,task->result.value.wifi.ssid));
                JS_SetPropertyStr(context,result,"ip",JS_NewString(context,task->result.value.wifi.ip));
            }else if(task->kind==OUSE_TASK_AUDIO){
                JS_SetPropertyStr(context,result,"recordedMs",JS_NewUint32(context,task->result.value.audio.recorded_ms));
                JS_SetPropertyStr(context,result,"positionMs",JS_NewUint32(context,task->result.value.audio.position_ms));
            }else if(task->kind==OUSE_TASK_STORAGE)JS_SetPropertyStr(context,result,"jsonBytes",JS_NewFloat64(context,(double)task->result.value.storage.json_bytes));
            else if(task->kind==OUSE_TASK_NETWORK){
                JS_SetPropertyStr(context,result,"status",JS_NewInt32(context,task->result.value.network.status));
                JS_SetPropertyStr(context,result,"bodyBytes",JS_NewFloat64(context,(double)task->result.value.network.body_bytes));
            }else if(task->kind==OUSE_TASK_ALARM)JS_SetPropertyStr(context,result,"alarmId",JS_NewString(context,task->result.value.alarm.id));
        }
    }
    JS_SetPropertyStr(context,object,"result",result);
#undef TNUM
#undef TSTR
#undef TBOOL
    return object;
}
static JSValue tasks_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);
    if(action==3){
        if(argc!=1 || (!JS_IsNull(argv[0]) && !JS_IsFunction(context,argv[0])))return JS_ThrowTypeError(context,"onComplete expects function or null");
        JS_FreeValue(context,app->task_handler);app->task_handler=JS_DupValue(context,argv[0]);return JS_UNDEFINED;
    }
    ouse_task_snapshot_t task;
    if(action==0 || action==4){
        double after_value=0,limit_value=16;
        if(argc>2 || (argc>0 && (!JS_IsNumber(argv[0]) || JS_ToFloat64(context,&after_value,argv[0])<0)) ||
           (argc>1 && (!JS_IsNumber(argv[1]) || JS_ToFloat64(context,&limit_value,argv[1])<0)) ||
           !isfinite(after_value) || after_value<0 || after_value>UINT32_MAX || floor(after_value)!=after_value ||
           !isfinite(limit_value) || limit_value<1 || limit_value>16 || floor(limit_value)!=limit_value)
            return JS_ThrowTypeError(context,"list(afterId=0, limit=16): nonnegative ID, limit 1..16");
        JSValue result=JS_NewArray(context);unsigned count=0;uint32_t after=(uint32_t)after_value;
        while(count<(unsigned)limit_value && ouse_task_next(action==4 ? NULL : &app->owner,after,&task,false)){
            after=task.id;JS_SetPropertyUint32(context,result,count++,task_snapshot(context,&task));
        }
        return result;
    }
    double exact;
    if(argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(context,&exact,argv[0])<0 || !isfinite(exact) || exact<1 || exact>UINT32_MAX || floor(exact)!=exact)
        return JS_ThrowTypeError(context,"Expected task ID");
    if(action==2)return JS_NewBool(context,ouse_task_cancel(&app->owner,(uint32_t)exact,"application")==ESP_OK);
    return ouse_task_get(&app->owner,(uint32_t)exact,&task) ? task_snapshot(context,&task) : JS_NULL;
}
static void task_completion_events(instance_t *app)
{
    for(unsigned i=0;i<OUSE_TASK_OWNER_LIMIT && !app->faulted;++i){
        if(ouse_budget_slice_done(&app->budget))return;
        model_lock(app);bool runnable=!app->closed && (app->visible || app->background_active) && app->navigation_stage==NAV_IDLE;model_unlock(app);
        if(!runnable)return;
        ouse_task_snapshot_t task;if(!ouse_task_next(&app->owner,0,&task,true))return;
        // No subscriber still consumes the delivery slot; queryable history is
        // retained until pressure reclaims it. Suspended instances never ack.
        ouse_task_ack(&app->owner,task.id);
        if(JS_IsFunction(app->context,app->task_handler)){
            JSValue handler=JS_DupValue(app->context,app->task_handler),value=task_snapshot(app->context,&task);
            JSValue result=call_js(app,"tasks","onComplete",handler,JS_UNDEFINED,1,&value);
            JS_FreeValue(app->context,handler);JS_FreeValue(app->context,value);if(!result_ok(app,result))return;
        }
    }
}
static JSValue system_info(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;
    ouse_system_info_t info;
    if (ouse_system_get_info(&info)!=ESP_OK) return JS_ThrowInternalError(context,"System info unavailable");
    JSValue object=JS_NewObject(context);
#define FIELD(name,value) JS_SetPropertyStr(context,object,name,JS_NewFloat64(context,(double)(value)))
    FIELD("internalFree",info.internal_free); FIELD("internalTotal",info.internal_total);
    FIELD("psramFree",info.psram_free); FIELD("psramTotal",info.psram_total);
    FIELD("flashBytes",info.flash_bytes); FIELD("cpuMHz",info.cpu_mhz);
    FIELD("uptimeMs",info.uptime_ms); FIELD("resetReason",info.reset_reason);
#undef FIELD
    JS_SetPropertyStr(context,object,"coreVersion",JS_NewString(context,ouse_core_version()));
    JS_SetPropertyStr(context,object,"idfVersion",JS_NewString(context,info.idf_version));
    return object;
}
static JSValue console_log(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;
    // Accept strings only, so logging cannot execute user-defined toString.
    char text[MAX_TEXT+1];
    if (argc!=1 || !copy_string(context,argv[0],text,sizeof(text))) return JS_ThrowTypeError(context,"log expects one short string");
    ESP_LOGI("OuseJS","%s: %s",from_js(context)->id,text);
    return JS_UNDEFINED;
}
static JSValue timer_create(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int interval)
{
    (void)self; instance_t *app=from_js(context); int32_t period;
    if (argc!=2 || !JS_IsFunction(context,argv[0]) || !JS_IsNumber(argv[1]) ||
        JS_ToInt32(context,&period,argv[1])<0 || period<100 || period>60000)
        return JS_ThrowRangeError(context,"Timer expects function and period 100..60000ms");
    for (unsigned i=0;i<MAX_TIMERS;++i) if (!app->timers[i].id) {
        if (app->next_timer>=INT32_MAX) return JS_ThrowRangeError(context,"Timer id exhausted");
        script_timer_t *timer=&app->timers[i];
        timer->id=++app->next_timer; timer->period=(uint32_t)period;
        timer->due=now_ms()+(uint32_t)period;timer->remaining=(uint32_t)period; timer->interval=interval!=0;
        timer->handler=JS_DupValue(context,argv[0]);
        return JS_NewUint32(context,timer->id);
    }
    return JS_ThrowRangeError(context,"At most 8 timers per app");
}
static JSValue timer_clear(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self; uint32_t id;
    if (argc!=1 || JS_ToUint32(context,&id,argv[0])<0) return JS_ThrowTypeError(context,"clear expects timer id");
    instance_t *app=from_js(context);
    for (unsigned i=0;i<MAX_TIMERS;++i) if (app->timers[i].id==id) {
        JS_FreeValue(context,app->timers[i].handler); app->timers[i].handler=JS_UNDEFINED; app->timers[i].id=0;
    }
    return JS_UNDEFINED;
}

static JSValue theme_snapshot(JSContext *context)
{
    JSValue object=JS_NewObject(context),colors=JS_NewObject(context);
    bool dark=from_js(context)->worker_ui.dark;
    JS_SetPropertyStr(context,object,"mode",JS_NewString(context,dark ? "dark" : "light"));
    ouse_theme_colors_t c=from_js(context)->worker_ui.colors;
    const char *names[]={"background","desktop","surface","text","textSecondary","accent","button","pressed","icon","border","disabled","error"};
    uint32_t values[]={c.background,c.desktop,c.surface,c.text,c.secondary,c.accent,c.button,c.pressed,c.icon,c.border,c.disabled,c.error};
    for(unsigned i=0;i<12;++i){char color[8];snprintf(color,sizeof(color),"#%06lX",(unsigned long)values[i]);JS_SetPropertyStr(context,colors,names[i],JS_NewString(context,color));}
    JS_SetPropertyStr(context,object,"colors",colors); return object;
}
static JSValue display_snapshot(JSContext *context)
{
    ui_snapshot_t *ui=&from_js(context)->worker_ui;JSValue object=JS_NewObject(context);
    int w=ui->width,h=ui->height;double density=ui->density/160.0;
#define NUMBER(key,val) JS_SetPropertyStr(context,object,key,JS_NewFloat64(context,(double)(val)))
    NUMBER("widthPx",w); NUMBER("heightPx",h); NUMBER("widthDp",w/density); NUMBER("heightDp",h/density);
    NUMBER("contentWidthDp",ui->app_width/density); NUMBER("contentHeightDp",ui->app_height/density);
    NUMBER("densityDpi",ui->density); NUMBER("density",ui->density/160.0);
    NUMBER("fontPercent",ui->font_percent); NUMBER("fontScale",ui->font_percent/100.0);
    NUMBER("brightness",ui->brightness);
    NUMBER("keyboardHeightDp",ui->keyboard_height/density);NUMBER("keyboardInsetDp",ui->keyboard_inset/density);
    JS_SetPropertyStr(context,object,"keyboardVisible",JS_NewBool(context,ui->keyboard_visible));
#undef NUMBER
    return object;
}
static JSValue device_snapshot(JSContext *context,int group)
{
    JSValue object=JS_NewObject(context);
#define NUM(key,value) JS_SetPropertyStr(context,object,key,JS_NewFloat64(context,(double)(value)))
#define BOOL(key,value) JS_SetPropertyStr(context,object,key,JS_NewBool(context,value))
#define STR(key,value) JS_SetPropertyStr(context,object,key,JS_NewString(context,value))
    if(group==3) {
        ouse_power_state_t p={0};ouse_power_get(&p);
        BOOL("available",p.available);BOOL("valid",p.valid);BOOL("present",p.present);BOOL("external",p.external);
        BOOL("charging",p.charging);BOOL("full",p.full);BOOL("asleep",p.asleep);
        NUM("percent",p.valid ? p.percent : -1);NUM("millivolts",p.millivolts);NUM("sleepSeconds",p.sleep_seconds);NUM("error",p.error);
    } else if(group==4) {
        ouse_audio_state_t a={0};ouse_audio_get(&a);
        const char *modes[]={"idle","starting","playing","recording","playback","error"};
        BOOL("available",a.available);BOOL("muted",a.muted);NUM("volume",a.volume);NUM("peak",a.peak);
        NUM("recordedMs",a.recorded_ms);NUM("positionMs",a.position_ms);STR("state",modes[a.mode]);NUM("error",a.error);
        NUM("requestId",a.request_id);NUM("completedId",a.completed_id);
    } else {
        ouse_wifi_state_t w={0};ouse_wifi_get(&w);
        BOOL("available",w.available);BOOL("enabled",w.enabled);BOOL("ready",w.ready);BOOL("scanning",w.scanning);
        BOOL("connecting",w.connecting);BOOL("connected",w.connected);STR("ssid",w.ssid);STR("ip",w.ip);
        NUM("rssi",w.rssi);NUM("reason",w.reason);NUM("error",w.error);NUM("requestId",w.request_id);NUM("completedId",w.completed_id);
        JSValue aps=JS_NewArray(context);
        for(unsigned i=0;i<w.count && i<OUSE_WIFI_AP_MAX;++i) {
            JSValue ap=JS_NewObject(context);
            JS_SetPropertyStr(context,ap,"ssid",JS_NewString(context,w.aps[i].ssid));
            JS_SetPropertyStr(context,ap,"rssi",JS_NewInt32(context,w.aps[i].rssi));
            JS_SetPropertyStr(context,ap,"channel",JS_NewInt32(context,w.aps[i].channel));
            JS_SetPropertyStr(context,ap,"secured",JS_NewBool(context,w.aps[i].secured));
            JS_SetPropertyUint32(context,aps,i,ap);
        }
        JS_SetPropertyStr(context,object,"networks",aps);
    }
#undef NUM
#undef BOOL
#undef STR
    return object;
}
static JSValue device_command(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int command)
{
    (void)self;esp_err_t error=ESP_ERR_INVALID_ARG;uint32_t task_id=0;instance_t *app=from_js(context);
    if(command==11) {
        char ssid[33],password[65];
        if(argc!=2 || !copy_string(context,argv[0],ssid,sizeof(ssid)) || !copy_string(context,argv[1],password,sizeof(password)))return JS_ThrowTypeError(context,"connect(ssid, password) expects strings <=32/64 UTF-8 bytes");
        error=ouse_wifi_submit(&app->owner,OUSE_WIFI_CONNECT,false,ssid,password,&task_id);memset(password,0,sizeof(password));
    } else if(command==4 || command==9) {
        if(argc!=1 || !JS_IsBool(argv[0]))return JS_ThrowTypeError(context,"Expected boolean");
        error=command==4 ? ouse_audio_set_muted(JS_ToBool(context,argv[0])) : ouse_wifi_submit(&app->owner,OUSE_WIFI_ENABLE,JS_ToBool(context,argv[0]),NULL,NULL,&task_id);
    } else if(command==0 || command==3 || command==6) {
        double exact;
        if(argc!=1 || !JS_IsNumber(argv[0]) || JS_ToFloat64(context,&exact,argv[0])<0 || !isfinite(exact) || floor(exact)!=exact || exact<0 || exact>300)return JS_ThrowTypeError(context,"Expected a bounded positive integer");
        unsigned n=(unsigned)exact;
        error=command==0 ? ouse_power_set_timeout(n) : command==3 ? ouse_audio_set_volume(n) : ouse_audio_submit(&app->owner,OUSE_AUDIO_CAPTURE,n,&task_id);
    } else {
        if(argc)return JS_ThrowTypeError(context,"Expected no arguments");
        switch(command) {
        case 1:model_lock(from_js(context));from_js(context)->pending_commands|=1u<<4;model_unlock(from_js(context));error=ESP_OK;break;
        case 2:model_lock(from_js(context));from_js(context)->pending_commands|=1u<<5;model_unlock(from_js(context));error=ESP_OK;break;
        case 5:error=ouse_audio_submit(&app->owner,OUSE_AUDIO_TEST,0,&task_id);break;
        case 7:error=ouse_audio_submit(&app->owner,OUSE_AUDIO_REPLAY,0,&task_id);break;
        case 8:error=ouse_audio_submit(&app->owner,OUSE_AUDIO_STOP,0,&task_id);break;
        case 10:error=ouse_wifi_submit(&app->owner,OUSE_WIFI_SCAN,false,NULL,NULL,&task_id);break;
        case 12:error=ouse_wifi_submit(&app->owner,OUSE_WIFI_DISCONNECT,false,NULL,NULL,&task_id);break;
        case 13:error=ouse_wifi_submit(&app->owner,OUSE_WIFI_FORGET,false,NULL,NULL,&task_id);break;
        }
    }
    return error==ESP_OK && task_id ? JS_NewUint32(context,task_id) : JS_NewBool(context,error==ESP_OK);
}
static uint32_t group_revision(instance_t *app,unsigned group) {return group<3 ? app->worker_ui.revisions[group] : ouse_devices_revision(group-3);}
static JSValue preference_snapshot(JSContext *context,int group)
{
    if(group==0)return theme_snapshot(context);
    if(group==1)return JS_NewString(context,from_js(context)->worker_ui.locale ? "en" : "zh_CN");
    return group==2 ? display_snapshot(context) : device_snapshot(context,group);
}
static JSValue preference_get(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int group)
{(void)self;(void)argc;(void)argv;return preference_snapshot(context,group);}
static JSValue preference_on(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int group)
{
    (void)self;instance_t *app=from_js(context);
    if(argc!=1 || (!JS_IsNull(argv[0]) && !JS_IsFunction(context,argv[0])))return JS_ThrowTypeError(context,"onChange expects function or null");
    JS_FreeValue(context,app->changes[group]);app->changes[group]=JS_DupValue(context,argv[0]);return JS_UNDEFINED;
}
static JSValue preference_set(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int group)
{
    (void)self;instance_t *app=from_js(context);char text[16];int number;
    if(argc!=1 && !(group==3 && argc==2 && JS_IsBool(argv[1])))return JS_ThrowTypeError(context,"Expected value and optional brightness save flag");
    if(group<2){
        if(!copy_string(context,argv[0],text,sizeof(text)))return JS_ThrowTypeError(context,"Expected string");
        if(group==0){if(strcmp(text,"dark") && strcmp(text,"light"))return JS_ThrowRangeError(context,"Expected light/dark");number=!strcmp(text,"dark");}
        else {if(strcmp(text,"zh_CN") && strcmp(text,"en"))return JS_ThrowRangeError(context,"Expected zh_CN/en");number=!strcmp(text,"en");}
    }else{
        double exact;if(!JS_IsNumber(argv[0]) || JS_ToFloat64(context,&exact,argv[0])<0 || !isfinite(exact) || floor(exact)!=exact || exact<0 || exact>130)return JS_ThrowTypeError(context,"Expected bounded integer");
        number=(int)exact;
        if(group==2 && number!=100 && number!=115 && number!=130)return JS_ThrowRangeError(context,"Expected font percent 100/115/130");
        if(group==3 && (number<10 || number>100))return JS_ThrowRangeError(context,"Expected brightness 10..100");
    }
    // Returns acceptance. Applied state and onChange arrive in later snapshots.
    model_lock(app);app->command_values[group]=number;app->pending_commands|=1u<<group;
    if(group==3)app->brightness_save=argc!=2 || JS_ToBool(context,argv[1]);
    model_unlock(app);
    return JS_TRUE;
}
static JSValue translated(instance_t *app,const char *key,unsigned locale)
{
    JSContext *context=app->context;
    JSValue dictionary=json_property(context,app->translations,locale ? "en" : "zh_CN");
    JSValue value=JS_IsObject(dictionary) ? json_property(context,dictionary,key) : JS_UNDEFINED;
    JS_FreeValue(context,dictionary);
    if(JS_IsString(value)){char checked[MAX_TEXT+1];if(copy_string(context,value,checked,sizeof(checked)))return value;}
    JS_FreeValue(context,value);
    return JS_NewString(context,key);
}
static JSValue locale_text(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self; char key[64];
    if(argc!=1 || !copy_string(context,argv[0],key,sizeof(key))) return JS_ThrowTypeError(context,"t expects a short translation key");
    return translated(from_js(context),key,from_js(context)->worker_ui.locale);
}
static void preference_notifications(instance_t *app)
{
    for(unsigned i=0;i<6 && !app->faulted;++i) {
        if(ouse_budget_slice_done(&app->budget) || navigation_pending(app))return;
        uint32_t current=group_revision(app,i);
        if(current==app->revisions[i])continue;
        if(!JS_IsFunction(app->context,app->changes[i])){app->revisions[i]=current;continue;}
        JSValue handler=JS_DupValue(app->context,app->changes[i]),value=preference_snapshot(app->context,(int)i);
        if(ouse_budget_slice_done(&app->budget)){JS_FreeValue(app->context,value);JS_FreeValue(app->context,handler);return;}
        app->revisions[i]=current;
        const char *groups[]={"theme","locale","display","power","audio","wifi"};
        JSValue result=call_js(app,"preference",groups[i],handler,JS_UNDEFINED,1,&value);
        JS_FreeValue(app->context,value);JS_FreeValue(app->context,handler);
        if(!result_ok(app,result))break;
    }
}
// Notification records and pending open intents are native plain data. The
// worker never hands the service JS values, callbacks, or pointers into a BPK.
static bool notification_string(JSContext *context,JSValueConst object,const char *key,char *out,size_t capacity,bool required)
{
    JSValue value=JS_GetPropertyStr(context,object,key);
    bool valid=JS_IsUndefined(value) && !required;
    if(!valid)valid=copy_string(context,value,out,capacity);
    JS_FreeValue(context,value);return valid && (!required || out[0]);
}
static JSValue notification_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int action)
{
    (void)self;instance_t *app=from_js(context);
    if(action==3){
        if(argc!=1 || (!JS_IsNull(argv[0]) && !JS_IsFunction(context,argv[0])))return JS_ThrowTypeError(context,"onOpen expects function or null");
        JS_FreeValue(context,app->notification_handler);app->notification_handler=JS_DupValue(context,argv[0]);
        return JS_NewBool(context,app->notifications_allowed);
    }
    if(action==2){
        JSValue array=JS_NewArray(context);if(!app->notifications_allowed)return array;
        ouse_notification_t records[OUSE_NOTIFICATION_APP_CAPACITY];
        unsigned count=ouse_notifications_list(app->id,records,OUSE_NOTIFICATION_APP_CAPACITY);
        for(unsigned i=0;i<count;++i){ouse_notification_t *record=&records[i];JSValue object=JS_NewObject(context);
            JS_SetPropertyStr(context,object,"id",JS_NewString(context,record->id));
            JS_SetPropertyStr(context,object,"title",JS_NewString(context,record->title));
            JS_SetPropertyStr(context,object,"body",JS_NewString(context,record->body));
            JS_SetPropertyStr(context,object,"payload",JS_NewString(context,record->payload));
            JS_SetPropertyStr(context,object,"ongoing",JS_NewBool(context,record->ongoing));
            JS_SetPropertyStr(context,object,"createdMs",JS_NewFloat64(context,(double)record->created_ms));
            JS_SetPropertyStr(context,object,"updatedMs",JS_NewFloat64(context,(double)record->updated_ms));
            JS_SetPropertyUint32(context,array,i,object);
        }
        return array;
    }
    if(!app->notifications_allowed)return JS_FALSE;
    if(action==1){
        char id[OUSE_NOTIFICATION_ID_SIZE];if(argc!=1 || !copy_string(context,argv[0],id,sizeof(id)) || !id[0])return JS_ThrowTypeError(context,"cancel expects notification id");
        model_lock(app);esp_err_t result=app->closed ? ESP_ERR_INVALID_STATE : ouse_notifications_cancel(app->id,id);model_unlock(app);
        return JS_NewBool(context,result==ESP_OK);
    }
    char id[OUSE_NOTIFICATION_ID_SIZE]={0},title[OUSE_NOTIFICATION_TITLE_SIZE]={0};
    char body[OUSE_NOTIFICATION_BODY_SIZE]={0},payload[OUSE_NOTIFICATION_PAYLOAD_SIZE]={0};
    if(argc!=1 || !JS_IsObject(argv[0]) || JS_IsArray(argv[0]) ||
        !notification_string(context,argv[0],"id",id,sizeof(id),true) ||
        !notification_string(context,argv[0],"title",title,sizeof(title),true) ||
        !notification_string(context,argv[0],"body",body,sizeof(body),false) ||
        !notification_string(context,argv[0],"payload",payload,sizeof(payload),false))return JS_ThrowTypeError(context,"publish expects bounded id, title, body and payload strings");
    JSValue ongoing=JS_GetPropertyStr(context,argv[0],"ongoing");
    if(!JS_IsUndefined(ongoing) && !JS_IsBool(ongoing)){JS_FreeValue(context,ongoing);return JS_ThrowTypeError(context,"ongoing must be boolean");}
    ouse_notification_request_t request={.id=id,.title=title,.body=body,.payload=payload,.ongoing=JS_ToBool(context,ongoing)>0};JS_FreeValue(context,ongoing);
    // Close/update/uninstall waits for this short store operation before removing
    // ownership; an old worker cannot resurrect records after registry removal.
    model_lock(app);esp_err_t result=app->closed ? ESP_ERR_INVALID_STATE : ouse_notifications_publish(app->id,&request);model_unlock(app);
    return JS_NewBool(context,result==ESP_OK);
}
static void notification_open_events(instance_t *app)
{
    if(!app->notifications_allowed || !app->worker_visible || !JS_IsFunction(app->context,app->notification_handler) || app->faulted)return;
    if(ouse_budget_slice_done(&app->budget))return;
    model_lock(app);bool eligible=app->visible && !app->closed && !app->faulted && app->navigation_stage==NAV_IDLE;model_unlock(app);
    if(!eligible)return;
    ouse_notification_open_t intent;
    if(!ouse_notifications_peek_open(app->id,&intent))return;
    JSValue handler=JS_DupValue(app->context,app->notification_handler),event=JS_NewObject(app->context);
    JS_SetPropertyStr(app->context,event,"id",JS_NewString(app->context,intent.id));
    JS_SetPropertyStr(app->context,event,"payload",JS_NewString(app->context,intent.payload));
    if(ouse_budget_slice_done(&app->budget)){JS_FreeValue(app->context,event);JS_FreeValue(app->context,handler);return;}
    JSValue result=call_js(app,"notification",intent.id,handler,JS_UNDEFINED,1,&event);
    JS_FreeValue(app->context,event);JS_FreeValue(app->context,handler);
    if(result_ok(app,result)){model_lock(app);if(!app->closed)(void)ouse_notifications_ack_open(app->id,intent.token);model_unlock(app);}
}

static void storage_job_release(storage_job_t *job)
{
    ouse_task_hold_result(job->token,false);
    instance_t *app=job->app;
    if(app && app->context){JS_FreeValue(app->context,job->resolve);JS_FreeValue(app->context,job->reject);}
    free(job->result);free(job);--storage_job_count;if(app)release_instance(app);
}
static void cancel_storage_jobs(instance_t *app)
{
    // Revoke all JS ownership now. The native completion only needs its plain
    // job, not a reference keeping an entire closed UI/model/resource set alive.
    for(unsigned i=0;i<MAX_STORAGE_PENDING;++i){storage_job_t *job=app->storage[i];if(!job)continue;
        app->storage[i]=NULL;job->slot=MAX_STORAGE_PENDING;
        if(app->context){JS_FreeValue(app->context,job->resolve);JS_FreeValue(app->context,job->reject);}
        job->resolve=job->reject=JS_UNDEFINED;
        job->app=NULL;release_instance(app);
        if(job->completed)storage_job_release(job);
    }
}
static void storage_completed(void *opaque,uint32_t token,esp_err_t error,char *result)
{
    storage_job_t *job=opaque;job->result=result;job->error=token==job->token ? error : ESP_ERR_INVALID_STATE;
    // Every live job contributes at most one queue item; no more than 64 jobs
    // can be accepted, including jobs whose JS instance has already closed.
    BaseType_t queued=xQueueSend(storage_completions,&job,0);
    configASSERT(queued==pdTRUE);(void)queued;wake_worker();
}
static void drain_storage_completions(void)
{
    storage_job_t *job;
    while(xQueueReceive(storage_completions,&job,0)==pdTRUE){instance_t *app=job->app;job->completed=true;
        if(!app){storage_job_release(job);continue;}
        model_lock(app);bool discard=app->closed || app->faulted;model_unlock(app);
        if(discard || !app->context || job->slot>=MAX_STORAGE_PENDING){
            if(job->slot<MAX_STORAGE_PENDING && app->storage[job->slot]==job)app->storage[job->slot]=NULL;
            storage_job_release(job);
        }
        // A suspended instance keeps this plain result in its slot. It does not
        // settle a Promise or execute callbacks until it is runnable again.
    }
}
static JSValue storage_error(JSContext *context,const char *name,const char *message,const char *code)
{
    JSValue error=JS_NewError(context);if(JS_IsException(error))return error;
    JS_SetPropertyStr(context,error,"name",JS_NewString(context,name));
    JS_SetPropertyStr(context,error,"message",JS_NewString(context,message));JS_SetPropertyStr(context,error,"code",JS_NewString(context,code));return error;
}
static JSValue rejected_storage_promise(JSContext *context,JSValue promise,JSValue *handlers,JSValue error)
{
    if(JS_IsException(error))error=JS_GetException(context);
    JSValue result=call_js(from_js(context),"storage","invalid",handlers[1],JS_UNDEFINED,1,&error);JS_FreeValue(context,error);
    JS_FreeValue(context,handlers[0]);JS_FreeValue(context,handlers[1]);
    if(JS_IsException(result)){JS_FreeValue(context,result);JS_FreeValue(context,promise);return JS_EXCEPTION;}
    JS_FreeValue(context,result);return promise;
}
static bool storage_key(const char *key)
{
    if(!key[0])return false;
    for(unsigned i=0;key[i];++i){char c=key[i];bool alnum=(c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');
        if(!alnum && (!i || (c!='.' && c!='_' && c!='-')))return false;
    }
    return true;
}
static JSValue storage_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);JSValue handlers[2]={JS_UNDEFINED,JS_UNDEFINED};
    JSValue promise=JS_NewPromiseCapability(context,handlers);if(JS_IsException(promise))return promise;
    char key[OUSE_STORAGE_KEY_MAX+1]={0};int arity=operation==OUSE_STORAGE_SET ? 2 : operation<=OUSE_STORAGE_REMOVE ? 1 : 0;
    if(argc!=arity || (arity && (!copy_string(context,argv[0],key,sizeof(key)) || !storage_key(key))))
        return rejected_storage_promise(context,promise,handlers,storage_error(context,"TypeError","Expected private storage key: 1..31 ASCII letters, digits, dot, underscore or hyphen; first character alphanumeric","INVALID_ARGUMENT"));
    JSValue encoded=JS_UNDEFINED;const char *json=NULL;size_t size=0;
    if(operation==OUSE_STORAGE_SET){
        encoded=JS_JSONStringify(context,argv[1],JS_UNDEFINED,JS_UNDEFINED);
        if(JS_IsException(encoded)){JSValue error=JS_GetException(context);return rejected_storage_promise(context,promise,handlers,error);}
        if(!JS_IsString(encoded)){JS_FreeValue(context,encoded);return rejected_storage_promise(context,promise,handlers,storage_error(context,"TypeError","Value must serialize to JSON","INVALID_VALUE"));}
        json=JS_ToCStringLen(context,&size,encoded);
        if(!json){JS_FreeValue(context,encoded);return rejected_storage_promise(context,promise,handlers,JS_GetException(context));}
        if(!size || size>OUSE_STORAGE_VALUE_MAX || memchr(json,0,size)){
            JS_FreeCString(context,json);JS_FreeValue(context,encoded);
            return rejected_storage_promise(context,promise,handlers,storage_error(context,"RangeError","Serialized JSON must be 1..8192 bytes","VALUE_TOO_LARGE"));
        }
    }
    // JSON.stringify may invoke toJSON/getters which submit storage requests
    // recursively. Reserve a slot only after all application JS has returned.
    unsigned slot=0;while(slot<MAX_STORAGE_PENDING && app->storage[slot])++slot;
    if(slot==MAX_STORAGE_PENDING || storage_job_count>=MAX_STORAGE_JOBS){
        if(json)JS_FreeCString(context,json);
        JS_FreeValue(context,encoded);
        return rejected_storage_promise(context,promise,handlers,storage_error(context,"Error","Storage request limit reached","BUSY"));
    }
    storage_job_t *job=calloc(1,sizeof(*job));
    if(!job){if(json)JS_FreeCString(context,json);JS_FreeValue(context,encoded);return rejected_storage_promise(context,promise,handlers,storage_error(context,"Error","Storage request memory unavailable","ESP_ERR_NO_MEM"));}
    job->app=app;job->slot=slot;job->resolve=handlers[0];job->reject=handlers[1];
    ++storage_job_count;atomic_fetch_add(&app->references,1);
    model_lock(app);
    esp_err_t error=app->closed || app->faulted ? ESP_ERR_INVALID_STATE : ESP_OK;
    if(error==ESP_OK){app->storage[slot]=job;error=ouse_storage_submit((ouse_storage_operation_t)operation,&app->owner,arity ? key : NULL,json,&job->token,storage_completed,job);}
    if(error!=ESP_OK)app->storage[slot]=NULL;
    model_unlock(app);
    if(json)JS_FreeCString(context,json);
    JS_FreeValue(context,encoded);
    if(job->token)JS_DefinePropertyValueStr(context,promise,"taskId",JS_NewUint32(context,job->token),JS_PROP_ENUMERABLE);
    if(error!=ESP_OK){
        JSValue reason=storage_error(context,"Error",esp_err_to_name(error),esp_err_to_name(error));
        if(JS_IsException(reason))reason=JS_GetException(context);
        JSValue result=call_js(app,"storage","submit-reject",job->reject,JS_UNDEFINED,1,&reason);JS_FreeValue(context,reason);storage_job_release(job);
        if(JS_IsException(result)){JS_FreeValue(context,result);JS_FreeValue(context,promise);return JS_EXCEPTION;}JS_FreeValue(context,result);
    }
    return promise;
}
static void process_storage_results(instance_t *app)
{
    for(unsigned i=0;i<MAX_STORAGE_PENDING && !app->faulted;++i){storage_job_t *job=app->storage[i];
        if(!job || !job->completed)continue;
        if(ouse_budget_slice_done(&app->budget))return;
        model_lock(app);bool runnable=!app->closed && !app->faulted && app->navigation_stage==NAV_IDLE && (app->visible || app->background_active);model_unlock(app);
        if(!runnable)return;
        app->storage[i]=NULL;job->slot=MAX_STORAGE_PENDING;
        bool reject=job->error!=ESP_OK || !job->result;
        JSValue value=reject ? storage_error(app->context,"Error",esp_err_to_name(job->error!=ESP_OK ? job->error : ESP_FAIL),esp_err_to_name(job->error!=ESP_OK ? job->error : ESP_FAIL)) : JS_ParseJSON(app->context,job->result,strlen(job->result),"private-storage");
        if(JS_IsException(value)){value=JS_GetException(app->context);reject=true;}
        if(ouse_budget_slice_done(&app->budget)){JS_FreeValue(app->context,value);app->storage[i]=job;job->slot=i;return;}
        JSValue result=call_js(app,"storage",reject ? "reject" : "resolve",reject ? job->reject : job->resolve,JS_UNDEFINED,1,&value);JS_FreeValue(app->context,value);
        storage_job_release(job);
        if(!result_ok(app,result))return;
    }
}
static JSValue system_storage(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;(void)argc;(void)argv;ouse_storage_stats_t stats={0};(void)ouse_storage_get_stats(&stats);JSValue object=JS_NewObject(context);
    JS_SetPropertyStr(context,object,"ready",JS_NewBool(context,stats.ready));JS_SetPropertyStr(context,object,"revision",JS_NewUint32(context,stats.revision));
    JS_SetPropertyStr(context,object,"totalBytes",JS_NewFloat64(context,(double)stats.total_bytes));JS_SetPropertyStr(context,object,"usedBytes",JS_NewFloat64(context,(double)stats.used_bytes));
    JS_SetPropertyStr(context,object,"freeBytes",JS_NewFloat64(context,(double)stats.free_bytes));JS_SetPropertyStr(context,object,"packageBytes",JS_NewFloat64(context,(double)stats.package_bytes));
    JS_SetPropertyStr(context,object,"privateBytes",JS_NewFloat64(context,(double)stats.private_bytes));JS_SetPropertyStr(context,object,"packageCount",JS_NewUint32(context,stats.package_count));
    JS_SetPropertyStr(context,object,"privateApps",JS_NewUint32(context,stats.private_apps));return object;
}

static bool request_navigation(instance_t *app,navigation_action_t action,unsigned target)
{
    model_lock(app);
    bool accepted=app->ready && app->visible && !app->closed && !app->faulted && app->navigation_stage==NAV_IDLE;
    if(accepted){
        unsigned current=app->page_stack[app->page_depth-1];
        if(action==NAV_BACK){accepted=app->page_depth>1;if(accepted)target=app->page_stack[app->page_depth-2];}
        else accepted=target<app->page_count && target!=current && (action!=NAV_PUSH || app->page_depth<MAX_PAGE_STACK);
        if(accepted){app->navigation_previous=current;app->navigation_target=target;app->navigation_action=action;
            app->navigation_event_pending=false;app->navigation_stage=NAV_REQUESTED;++app->generation;}
    }
    model_unlock(app);if(accepted)wake_worker();return accepted;
}
static JSValue navigation_snapshot(instance_t *app,bool event)
{
    model_lock(app);unsigned depth=app->page_depth,current=depth ? app->page_stack[depth-1] : 0;
    unsigned previous=app->navigation_previous;navigation_action_t action=app->navigation_action;
    bool pending=app->navigation_stage!=NAV_IDLE;model_unlock(app);
    JSContext *context=app->context;JSValue object=JS_NewObject(context);
    JS_SetPropertyStr(context,object,"page",JS_NewString(context,app->page_ids[current]));
    JS_SetPropertyStr(context,object,"depth",JS_NewUint32(context,depth));
    JS_SetPropertyStr(context,object,"canGoBack",JS_NewBool(context,depth>1));
    if(event){const char *actions[]={"open","push","back","replace"};
        JS_SetPropertyStr(context,object,"previous",JS_NewString(context,action==NAV_OPEN ? "" : app->page_ids[previous]));
        JS_SetPropertyStr(context,object,"action",JS_NewString(context,actions[action]));
    }else JS_SetPropertyStr(context,object,"pending",JS_NewBool(context,pending));
    return object;
}
static JSValue navigation_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int operation)
{
    (void)self;instance_t *app=from_js(context);
    if(operation==3)return navigation_snapshot(app,false);
    if(operation==4){
        if(argc!=1 || (!JS_IsNull(argv[0]) && !JS_IsFunction(context,argv[0])))return JS_ThrowTypeError(context,"onChange expects function or null");
        JS_FreeValue(context,app->navigation_handler);app->navigation_handler=JS_DupValue(context,argv[0]);return JS_UNDEFINED;
    }
    unsigned target=0;
    if(operation!=1){
        char id[32];if(argc!=1 || !copy_string(context,argv[0],id,sizeof(id)))return JS_ThrowTypeError(context,"navigation expects page id");
        for(target=0;target<app->page_count;++target)if(!strcmp(id,app->page_ids[target]))break;
        if(target==app->page_count)return JS_ThrowRangeError(context,"Unknown page id");
    }
    return JS_NewBool(context,request_navigation(app,operation==0 ? NAV_PUSH : operation==1 ? NAV_BACK : NAV_REPLACE,target));
}
static void navigation_changed(instance_t *app)
{
    if(ouse_budget_slice_done(&app->budget) || !JS_IsFunction(app->context,app->navigation_handler))return;
    model_lock(app);bool send=app->navigation_event_pending && app->navigation_stage==NAV_IDLE && app->visible && !app->closed && !app->faulted;
    if(send)app->navigation_event_pending=false;
    model_unlock(app);if(!send)return;
    JSValue handler=JS_DupValue(app->context,app->navigation_handler),value=navigation_snapshot(app,true);
    JSValue result=call_js(app,"navigation",app->page_ids[app->page_stack[app->page_depth-1]],handler,JS_UNDEFINED,1,&value);JS_FreeValue(app->context,handler);JS_FreeValue(app->context,value);result_ok(app,result);
}

static JSValue ui_set_style(JSContext *context,JSValueConst self,int argc,JSValueConst *argv);
#include "ouse_runtime_diagnostics.inc"
#include "ouse_runtime_services.inc"

static void install_api(instance_t *app)
{
    JSContext *context=app->context;
    JSValue global=JS_GetGlobalObject(context), api=JS_NewObject(context), ui=JS_NewObject(context);
    JS_SetPropertyStr(context,ui,"setText",JS_NewCFunctionMagic(context,ui_text,"setText",2,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,ui,"setStyle",JS_NewCFunction(context,ui_set_style,"setStyle",2));
    JS_SetPropertyStr(context,ui,"setImage",JS_NewCFunction(context,ui_set_image,"setImage",2));
    JS_SetPropertyStr(context,ui,"on",JS_NewCFunction(context,ui_on,"on",3));
    const char *visibility[]={"hide","show","isVisible","setEnabled"};
    for(int i=0;i<4;++i)JS_SetPropertyStr(context,ui,visibility[i],JS_NewCFunctionMagic(context,ui_visibility,visibility[i],i==3 ? 2 : 1,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,ui,"getValue",JS_NewCFunctionMagic(context,ui_value,"getValue",1,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,ui,"setValue",JS_NewCFunctionMagic(context,ui_value,"setValue",2,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,ui,"getText",JS_NewCFunctionMagic(context,ui_text,"getText",1,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,ui,"focus",JS_NewCFunctionMagic(context,ui_focus,"focus",1,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,ui,"blur",JS_NewCFunctionMagic(context,ui_focus,"blur",1,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,api,"ui",ui);
    JSValue assets=JS_NewObject(context);
    const char *asset_methods[]={"list","readText","readBytes"};
    for(int i=0;i<3;++i)JS_SetPropertyStr(context,assets,asset_methods[i],JS_NewCFunctionMagic(context,assets_api,asset_methods[i],i ? 1 : 0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"assets",assets);
    JSValue storage=JS_NewObject(context);
    const char *storage_methods[]={"get","set","remove","keys","clear","info"};
    for(int i=0;i<6;++i)JS_SetPropertyStr(context,storage,storage_methods[i],JS_NewCFunctionMagic(context,storage_api,storage_methods[i],i<3 ? i==1 ? 2 : 1 : 0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"storage",storage);
    JSValue net=JS_NewObject(context);
    JS_SetPropertyStr(context,net,"request",JS_NewCFunction(context,net_request,"request",1));
    JS_SetPropertyStr(context,api,"net",net);
    JSValue clock=JS_NewObject(context);JS_SetPropertyStr(context,clock,"get",JS_NewCFunction(context,clock_get,"get",0));
    JS_SetPropertyStr(context,api,"clock",clock);
    JSValue alarms=JS_NewObject(context);const char *alarm_methods[]={"set","remove","dismiss","snooze","list"};
    for(int i=0;i<5;++i)JS_SetPropertyStr(context,alarms,alarm_methods[i],JS_NewCFunctionMagic(context,alarms_api,alarm_methods[i],i==4 ? 0 : i==3 ? 2 : 1,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"alarms",alarms);
    JSValue media=JS_NewObject(context);const char *media_methods[]={"play","pause","resume","stop","get"};
    for(int i=0;i<5;++i)JS_SetPropertyStr(context,media,media_methods[i],JS_NewCFunctionMagic(context,media_api,media_methods[i],i==0 ? 1 : 0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"media",media);
    JSValue navigation=JS_NewObject(context);
    const char *navigation_methods[]={"push","back","replace","get","onChange"};
    for(int i=0;i<5;++i)JS_SetPropertyStr(context,navigation,navigation_methods[i],JS_NewCFunctionMagic(context,navigation_api,navigation_methods[i],i==1 || i==3 ? 0 : 1,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"navigation",navigation);
    JSValue tasks=JS_NewObject(context);const char *task_methods[]={"list","get","cancel","onComplete"};
    for(int i=0;i<4;++i)JS_SetPropertyStr(context,tasks,task_methods[i],JS_NewCFunctionMagic(context,tasks_api,task_methods[i],i ? 1 : 0,JS_CFUNC_generic_magic,i));
    JS_SetPropertyStr(context,api,"tasks",tasks);
    JSValue diagnostics=JS_NewObject(context);
    JS_SetPropertyStr(context,diagnostics,"get",JS_NewCFunctionMagic(context,diagnostics_api,"get",0,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,api,"diagnostics",diagnostics);
    JSValue background=JS_NewObject(context),life=JS_NewObject(context);
    JS_SetPropertyStr(context,background,"begin",JS_NewCFunctionMagic(context,background_api,"begin",0,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,background,"end",JS_NewCFunctionMagic(context,background_api,"end",0,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,background,"get",JS_NewCFunctionMagic(context,background_api,"get",0,JS_CFUNC_generic_magic,3));
    JS_SetPropertyStr(context,background,"isActive",JS_NewCFunctionMagic(context,background_api,"isActive",0,JS_CFUNC_generic_magic,2));
    JS_SetPropertyStr(context,life,"get",JS_NewCFunctionMagic(context,lifecycle_api,"get",0,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,life,"onChange",JS_NewCFunctionMagic(context,lifecycle_api,"onChange",1,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,api,"background",background);JS_SetPropertyStr(context,api,"lifecycle",life);
    JSValue notifications=JS_NewObject(context);
    const char *notification_methods[]={"publish","cancel","list","onOpen","update"};
    const int notification_actions[]={0,1,2,3,0};
    for(int i=0;i<5;++i)JS_SetPropertyStr(context,notifications,notification_methods[i],JS_NewCFunctionMagic(context,notification_api,notification_methods[i],i==2 ? 0 : 1,JS_CFUNC_generic_magic,notification_actions[i]));
    JS_SetPropertyStr(context,api,"notifications",notifications);
    JS_SetPropertyStr(context,api,"home",JS_NewCFunction(context,native_home,"home",0));
    JSValue system=JS_NewObject(context);
    JS_SetPropertyStr(context,system,"info",JS_NewCFunction(context,system_info,"info",0));
    JS_SetPropertyStr(context,system,"tasks",JS_NewCFunctionMagic(context,tasks_api,"tasks",0,JS_CFUNC_generic_magic,4));
    JS_SetPropertyStr(context,system,"diagnostics",JS_NewCFunctionMagic(context,diagnostics_api,"diagnostics",0,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,system,"storage",JS_NewCFunction(context,system_storage,"storage",0));
    JS_SetPropertyStr(context,system,"capabilities",JS_NewCFunction(context,system_capabilities,"capabilities",0));
    JS_SetPropertyStr(context,api,"system",system);
    JSValue timers=JS_NewObject(context);
    JS_SetPropertyStr(context,timers,"setInterval",JS_NewCFunctionMagic(context,timer_create,"setInterval",2,JS_CFUNC_generic_magic,1));
    JS_SetPropertyStr(context,timers,"setTimeout",JS_NewCFunctionMagic(context,timer_create,"setTimeout",2,JS_CFUNC_generic_magic,0));
    JS_SetPropertyStr(context,timers,"clear",JS_NewCFunction(context,timer_clear,"clear",1));
    JS_SetPropertyStr(context,api,"timers",timers);
    JS_SetPropertyStr(context,api,"apiVersion",JS_NewInt32(context,OUSE_RUNTIME_API_VERSION));

    const char *groups[]={"theme","locale","display"};
    for(int i=0;i<3;++i) {
        JSValue group=JS_NewObject(context);
        JS_SetPropertyStr(context,group,"get",JS_NewCFunctionMagic(context,preference_get,"get",0,JS_CFUNC_generic_magic,i));
        JS_SetPropertyStr(context,group,"onChange",JS_NewCFunctionMagic(context,preference_on,"onChange",1,JS_CFUNC_generic_magic,i));
        JS_SetPropertyStr(context,group,i==2 ? "setFontPercent" : "set",JS_NewCFunctionMagic(context,preference_set,"set",1,JS_CFUNC_generic_magic,i));
        if(i==1) JS_SetPropertyStr(context,group,"t",JS_NewCFunction(context,locale_text,"t",1));
        if(i==2) JS_SetPropertyStr(context,group,"setBrightness",JS_NewCFunctionMagic(context,preference_set,"setBrightness",1,JS_CFUNC_generic_magic,3));
        JS_SetPropertyStr(context,api,groups[i],group);
    }
    const char *device_groups[]={"power","audio","wifi"};
    const char *methods[]={"setTimeout","sleep","wake","setVolume","setMuted","testTone","record","playRecording","stop","setEnabled","scan","connect","disconnect","forget"};
    const int owners[]={0,0,0,1,1,1,1,1,1,2,2,2,2,2};
    const int arities[]={1,0,0,1,1,0,1,0,0,1,0,2,0,0};
    for(int i=0;i<3;++i) {
        JSValue group=JS_NewObject(context);
        JS_SetPropertyStr(context,group,"get",JS_NewCFunctionMagic(context,preference_get,"get",0,JS_CFUNC_generic_magic,i+3));
        JS_SetPropertyStr(context,group,"onChange",JS_NewCFunctionMagic(context,preference_on,"onChange",1,JS_CFUNC_generic_magic,i+3));
        for(int j=0;j<14;++j)if(owners[j]==i)JS_SetPropertyStr(context,group,methods[j],JS_NewCFunctionMagic(context,device_command,methods[j],arities[j],JS_CFUNC_generic_magic,j));
        JS_SetPropertyStr(context,api,device_groups[i],group);
    }
    JSValue console=JS_NewObject(context);
    JS_SetPropertyStr(context,console,"log",JS_NewCFunction(context,console_log,"log",1));
    JS_SetPropertyStr(context,global,"console",console);
    JS_SetPropertyStr(context,global,"Ouse",api);
    JS_FreeValue(context,global);
}

// JSON parsing uses the pinned engine. No eval/getters run while validating;
// require the entire document to validate before creating any app widgets.
static bool string_property(instance_t *app,JSValueConst object,const char *key,char *out,size_t size,bool required)
{
    JSValue value=source_property(app,object,key);
    bool valid=JS_IsUndefined(value) && !required;
    if (!valid) valid=copy_string(app->context,value,out,size);
    JS_FreeValue(app->context,value); return valid;
}
static bool integer_property(instance_t *app,JSValueConst object,const char *key,int *out,int min,int max)
{
    JSValue value=source_property(app,object,key);
    double number;
    bool valid=JS_IsNumber(value) && JS_ToFloat64(app->context,&number,value)==0 && isfinite(number) && number>=min && number<=max && floor(number)==number;
    if (valid) *out=(int)number;
    JS_FreeValue(app->context,value); return valid;
}
static const lv_image_dsc_t *find_image(instance_t *app,const char *id)
{
    for(unsigned i=0;i<app->image_count;++i)if(!strcmp(app->images[i].id,id))return app->images[i].image;
    return NULL;
}

static bool optional_integer(instance_t *app,JSValueConst object,const char *key,int *out,int fallback,int min,int max)
{
    JSValue value=source_property(app,object,key); bool absent=JS_IsUndefined(value);JS_FreeValue(app->context,value);
    if(absent){*out=fallback;return true;}return integer_property(app,object,key,out,min,max);
}
static bool flag_property(instance_t *app,JSValueConst object,const char *key,bool *out)
{
    JSValue value=source_property(app,object,key);
    bool valid=JS_IsUndefined(value) || JS_IsBool(value); *out=JS_IsBool(value) && JS_ToBool(app->context,value);
    JS_FreeValue(app->context,value);return valid;
}
static bool dimension(instance_t *app,JSValueConst object,const char *key,int *out,bool *fill,bool *content,int parent)
{
    JSValue value=source_property(app,object,key);char unit[16]; bool special=false;
    if(copy_string(app->context,value,unit,sizeof(unit))) {
        if(!strcmp(unit,"fill")){*fill=true;*out=parent;special=true;}
        else if(content && !strcmp(unit,"content")){*content=true;*out=parent;special=true;}
    }
    JS_FreeValue(app->context,value);
    return special || integer_property(app,object,key,out,1,parent);
}

static bool valid_translations(instance_t *app)
{
    const char *locales[]={"zh_CN","en"};
    for(unsigned language=0;language<2;++language) {
        JSValue dictionary=source_property(app,app->translations,locales[language]);
        if(JS_IsUndefined(dictionary)){JS_FreeValue(app->context,dictionary);continue;}
        if(!JS_IsObject(dictionary) || JS_IsArray(dictionary)){JS_FreeValue(app->context,dictionary);return false;}
        JSPropertyEnum *properties=NULL;uint32_t count=0;
        if(JS_GetOwnPropertyNames(app->context,&properties,&count,dictionary,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)<0){JS_FreeValue(app->context,dictionary);return false;}
        bool valid=count<=128;
        for(uint32_t i=0;i<count;++i){
            JSValue value=JS_GetProperty(app->context,dictionary,properties[i].atom);char text[MAX_TEXT+1];
            if(!copy_string(app->context,value,text,sizeof(text)))valid=false;
            JS_FreeValue(app->context,value);JS_FreeAtom(app->context,properties[i].atom);
        }
        js_free(app->context,properties);JS_FreeValue(app->context,dictionary);
        if(!valid)return false;
    }
    return true;
}
enum { COLOR_THEME=0x80000000u, COLOR_BACKGROUND=0, COLOR_SURFACE, COLOR_TEXT,
    COLOR_SECONDARY, COLOR_ACCENT, COLOR_BUTTON, COLOR_PRESSED, COLOR_BORDER,
    COLOR_DISABLED, COLOR_ERROR, COLOR_DESKTOP, COLOR_ICON };
static bool parse_color(instance_t *app,JSValueConst object,const char *key,uint32_t *out,bool *present)
{
    JSValue value=source_property(app,object,key);
    if(JS_IsUndefined(value)){JS_FreeValue(app->context,value);return true;}
    if(JS_IsNull(value)){*present=false;JS_FreeValue(app->context,value);return true;}
    char text[32];bool valid=copy_string(app->context,value,text,sizeof(text));JS_FreeValue(app->context,value);
    if(!valid)return false;
    const char *tokens[]={"$background","$surface","$text","$secondary","$accent","$button","$pressed","$border","$disabled","$error","$desktop","$icon"};
    for(unsigned i=0;i<sizeof(tokens)/sizeof(tokens[0]);++i)if(!strcmp(text,tokens[i])){*out=COLOR_THEME|i;*present=true;return true;}
    if(strlen(text)!=7 || text[0]!='#')return false;
    uint32_t color=0;
    for(unsigned i=1;i<7;++i){char c=text[i];int n=c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : c>='a' && c<='f' ? c-'a'+10 : -1;if(n<0)return false;color=(color<<4)|(unsigned)n;}
    *out=color;*present=true;return true;
}
static bool patch_flag(instance_t *app,JSValueConst object,const char *key,bool *out)
{
    JSValue value=source_property(app,object,key);bool valid=JS_IsUndefined(value) || JS_IsBool(value);
    if(JS_IsBool(value))*out=JS_ToBool(app->context,value);
    JS_FreeValue(app->context,value);return valid;
}
static bool parse_state_styles(instance_t *app,JSValueConst style,node_style_t *out)
{
    JSValue states=source_property(app,style,"states");
    if(JS_IsUndefined(states)){JS_FreeValue(app->context,states);return true;}
    if(!JS_IsObject(states) || JS_IsArray(states)){JS_FreeValue(app->context,states);return false;}
    const char *names[]={"pressed","checked","disabled"};bool valid=true;
    for(unsigned i=0;i<3 && valid;++i){
        JSValue state=source_property(app,states,names[i]);node_state_style_t *target=&out->states[i];
        if(JS_IsNull(state))memset(target,0,sizeof(*target));
        else if(!JS_IsUndefined(state)){
            valid=JS_IsObject(state) && !JS_IsArray(state) &&
                parse_color(app,state,"backgroundColor",&target->bg,&target->bg_set) &&
                parse_color(app,state,"textColor",&target->color,&target->color_set) &&
                parse_color(app,state,"borderColor",&target->border,&target->border_set) &&
                parse_color(app,state,"accentColor",&target->accent,&target->accent_set) &&
                parse_color(app,state,"thumbColor",&target->thumb,&target->thumb_set);
            JSValue opacity=source_property(app,state,"opacity");
            if(JS_IsNull(opacity))target->opacity_set=false;
            else if(!JS_IsUndefined(opacity)){valid=valid && integer_property(app,state,"opacity",&target->opacity,0,255);target->opacity_set=true;}
            JS_FreeValue(app->context,opacity);
        }
        JS_FreeValue(app->context,state);
    }
    JS_FreeValue(app->context,states);return valid;
}
// setStyle patches properties in place. Missing properties retain their current
// value, and a null color/state removes the application's local override.
static bool parse_style(instance_t *app,JSValueConst object,node_style_t *style)
{
    JSValue value=source_property(app,object,"style");if(JS_IsUndefined(value)){JS_FreeValue(app->context,value);return true;}
    char align[16]="";
    bool valid=JS_IsObject(value) && !JS_IsArray(value) &&
        parse_color(app,value,"backgroundColor",&style->bg,&style->bg_set) && parse_color(app,value,"textColor",&style->color,&style->color_set) &&
        parse_color(app,value,"borderColor",&style->border,&style->border_set) &&
        parse_color(app,value,"accentColor",&style->accent,&style->accent_set) && parse_color(app,value,"thumbColor",&style->thumb,&style->thumb_set) &&
        optional_integer(app,value,"radius",&style->radius,style->radius,0,128) && optional_integer(app,value,"borderWidth",&style->border_width,style->border_width,0,8) &&
        optional_integer(app,value,"opacity",&style->opacity,style->opacity,0,255) && optional_integer(app,value,"flexGrow",&style->flex_grow,style->flex_grow,0,16) &&
        patch_flag(app,value,"hidden",&style->hidden) && patch_flag(app,value,"disabled",&style->disabled) &&
        string_property(app,value,"textAlign",align,sizeof(align),false) && parse_state_styles(app,value,style);
    if(align[0] && strcmp(align,"left") && strcmp(align,"center") && strcmp(align,"right"))valid=false;
    if(align[0])style->align=!strcmp(align,"center") ? LV_TEXT_ALIGN_CENTER : !strcmp(align,"right") ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
    JS_FreeValue(app->context,value);return valid;
}
static JSValue ui_set_style(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(context);node_t *node=argc==2 ? find_node(app,argv[0]) : NULL;
    if(!node || !JS_IsObject(argv[1]) || JS_IsArray(argv[1]))return JS_ThrowTypeError(context,"setStyle expects id and style object");
    model_lock(app);node_style_t style=node->model.style;model_unlock(app);
    JSValue wrapper=JS_NewObject(context);JS_SetPropertyStr(context,wrapper,"style",JS_DupValue(context,argv[1]));
    bool valid=parse_style(app,wrapper,&style);JS_FreeValue(context,wrapper);if(!valid)return JS_ThrowTypeError(context,"Invalid style property");
    model_lock(app);
    if(modal_widget(node->model.type) && !style.hidden && !app->visible){model_unlock(app);return JS_FALSE;}
    bool hidden=style.hidden;node->model.style=style;set_node_hidden_locked(app,node,hidden);model_unlock(app);return JS_TRUE;
}
static bool parse_node(instance_t *app,JSValueConst value,int parent,unsigned depth)
{
    if(ouse_budget_exceeded(&app->budget)){app->interrupted=true;return false;}
    if (depth>8 || app->node_count==MAX_NODES || !JS_IsObject(value) || JS_IsArray(value)) return false;
    unsigned index=app->node_count++;
    node_model_t *node=&app->nodes[index].model; node->parent=parent;
    char type[16];
    if (!string_property(app,value,"type",type,sizeof(type),true) ||
        !string_property(app,value,"id",node->id,sizeof(node->id),true) || !node->id[0]) return false;
    for (unsigned i=0;i<index;++i) if (!strcmp(node->id,app->nodes[i].model.id)) return false;
    if (!strcmp(type,"container")) node->type=CONTAINER;
    else if (!strcmp(type,"label")) node->type=LABEL;
    else if (!strcmp(type,"button")) node->type=BUTTON;
    else if (!strcmp(type,"slider")) node->type=SLIDER;
    else if (!strcmp(type,"image")) node->type=IMAGE;
    else if(!strcmp(type,"textInput"))node->type=TEXT_INPUT;
    else if(!strcmp(type,"switch"))node->type=SWITCH;
    else if(!strcmp(type,"checkbox"))node->type=CHECKBOX;
    else if(!strcmp(type,"dropdown"))node->type=DROPDOWN;
    else if(!strcmp(type,"progressBar"))node->type=PROGRESS;
    else if(!strcmp(type,"spinner"))node->type=SPINNER;
    else if(!strcmp(type,"arc"))node->type=ARC;
    else if(!strcmp(type,"card"))node->type=CARD;
    else if(!strcmp(type,"list"))node->type=LIST;
    else if(!strcmp(type,"listItem"))node->type=LIST_ITEM;
    else if(!strcmp(type,"toolbar"))node->type=TOOLBAR;
    else if(!strcmp(type,"divider"))node->type=DIVIDER;
    else if(!strcmp(type,"dialog"))node->type=DIALOG;
    else if(!strcmp(type,"bottomSheet"))node->type=BOTTOM_SHEET;
    else return false;
    if(modal_widget(node->type))for(int ancestor=parent;ancestor>=0;ancestor=app->nodes[ancestor].model.parent)if(modal_widget(app->nodes[ancestor].model.type))return false;
    int parent_w=parent<0 ? app->worker_ui.app_width : app->nodes[parent].model.w;
    int parent_h=parent<0 ? app->worker_ui.app_height : app->nodes[parent].model.h;
    if(app->dp_units && parent<0){parent_w=(parent_w*160/(int)app->worker_ui.density);parent_h=(parent_h*160/(int)app->worker_ui.density);}
    if(app->dp_units) {
        int font,padding,gap;char flow[16]="";
        int default_padding=node->type==TOOLBAR ? 12 : node->type==CARD || node->type==LIST_ITEM ? 16 : node->type==DIALOG ? 24 : node->type==BOTTOM_SHEET ? 20 : 0;
        int default_gap=default_padding ? 12 : 0;
        int default_font=node->type==TOOLBAR || modal_widget(node->type) ? 20 : node->type==CARD ? 18 : 16;
        if(!optional_integer(app,value,"x",&node->x,0,0,parent_w) || !optional_integer(app,value,"y",&node->y,0,0,parent_h) ||
           !dimension(app,value,"width",&node->w,&node->fill_w,NULL,parent_w) ||
           !dimension(app,value,"height",&node->h,&node->fill_h,&node->content_h,16384) ||
           !optional_integer(app,value,"fontSize",&font,default_font,8,84) ||
           !optional_integer(app,value,"padding",&padding,default_padding,0,64) || !optional_integer(app,value,"gap",&gap,default_gap,0,64) ||
           !string_property(app,value,"layout",flow,sizeof(flow),false) || !flag_property(app,value,"scroll",&node->scroll))return false;
        if(node->type==LIST){JSValue scrolling=source_property(app,value,"scroll");if(JS_IsUndefined(scrolling))node->scroll=true;JS_FreeValue(app->context,scrolling);}
        if(node->fill_h)node->h=parent_h;
        if(flow[0] && strcmp(flow,"column") && strcmp(flow,"row"))return false;
        node->flow=!strcmp(flow,"column") ? 1 : !strcmp(flow,"row") ? 2 : 0;
        if(!flow[0])node->flow=node->type==LIST_ITEM || node->type==TOOLBAR ? 2 : container_widget(node->type) && node->type!=CONTAINER ? 1 : 0;
        node->font_sp=(unsigned)font;node->padding=(unsigned)padding;node->gap=(unsigned)gap;
        if((node->flow || node->scroll || padding || gap) && !container_widget(node->type))return false;
        if(node->content_h && !container_widget(node->type) && node->type!=LABEL && node->type!=BUTTON)return false;
        if(!string_property(app,value,"textKey",node->text_key,sizeof(node->text_key),false))return false;
        bool positioned=!modal_widget(node->type) && (parent<0 || !app->nodes[parent].model.flow);
        if(positioned && (node->x+node->w>parent_w || (!node->content_h &&
           (parent<0 || !app->nodes[parent].model.scroll) && node->y+node->h>parent_h)))return false;
        if(node->text_key[0] && (!text_widget(node->type) || node->type==TEXT_INPUT || node->type==CHECKBOX))return false;
    } else if (!integer_property(app,value,"x",&node->x,0,parent_w) || !integer_property(app,value,"y",&node->y,0,parent_h) ||
        !integer_property(app,value,"width",&node->w,1,parent_w) || !integer_property(app,value,"height",&node->h,1,parent_h) ||
        node->x+node->w>parent_w || node->y+node->h>parent_h) return false;
    if (!string_property(app,value,"text",node->text,sizeof(node->text),false) ||
        !string_property(app,value,"subtitle",node->subtitle,sizeof(node->subtitle),false) ||
        !string_property(app,value,"subtitleKey",node->subtitle_key,sizeof(node->subtitle_key),false)) return false;
    if((node->subtitle[0] || node->subtitle_key[0]) && node->type!=LIST_ITEM)return false;
    if(node->type==SLIDER || node->type==PROGRESS || node->type==ARC) {
        if (!integer_property(app,value,"min",&node->min,-32768,32767) ||
            !integer_property(app,value,"max",&node->max,node->min+1,32767) ||
            !integer_property(app,value,"value",&node->value,node->min,node->max)) return false;
    }
    if (node->type==IMAGE && (!string_property(app,value,"src",node->image,sizeof(node->image),true) || !find_image(app,node->image))) return false;
    if(node->type==SWITCH || node->type==CHECKBOX){
        JSValue checked=source_property(app,value,"value");
        if(!JS_IsUndefined(checked) && !JS_IsBool(checked)){JS_FreeValue(app->context,checked);return false;}
        node->value=JS_IsBool(checked) && JS_ToBool(app->context,checked);node->max=1;JS_FreeValue(app->context,checked);
    }
    if(node->type==TEXT_INPUT){
        char input[16]="",mode[16]="";int length;
        if(!string_property(app,value,"inputType",input,sizeof(input),false) ||
            !string_property(app,value,"keyboardMode",mode,sizeof(mode),false) ||
            !string_property(app,value,"placeholder",node->placeholder,sizeof(node->placeholder),false) ||
            !optional_integer(app,value,"maxLength",&length,128,1,MAX_TEXT) || !flag_property(app,value,"multiline",&node->multiline))return false;
        if(input[0] && strcmp(input,"text") && strcmp(input,"number") && strcmp(input,"password"))return false;
        if(mode[0] && strcmp(mode,"resize") && strcmp(mode,"pan") && strcmp(mode,"overlay"))return false;
        node->input_type=!strcmp(input,"number") ? OUSE_IME_NUMBER : !strcmp(input,"password") ? OUSE_IME_PASSWORD : OUSE_IME_TEXT;
        node->keyboard_mode=!strcmp(mode,"pan") ? OUSE_IME_PAN : !strcmp(mode,"overlay") ? OUSE_IME_OVERLAY : OUSE_IME_RESIZE;node->max_length=(unsigned)length;
    }
    if(node->type==DROPDOWN){
        JSValue options=source_property(app,value,"options");
        bool ok=false;
        if(JS_IsString(options)){ok=copy_string(app->context,options,node->options,sizeof(node->options));}
        else if(JS_IsArray(options)){
            JSValue length=source_property(app,options,"length");uint32_t count=0;
            ok=JS_ToUint32(app->context,&count,length)==0 && count>0 && count<=24;JS_FreeValue(app->context,length);
            for(uint32_t j=0;ok && j<count;++j){JSValue item=JS_GetPropertyUint32(app->context,options,j);char text[129];
                ok=copy_string(app->context,item,text,sizeof(text)) && text[0] && !strchr(text,'\n') && strlen(node->options)+strlen(text)+(j ? 1 : 0)<sizeof(node->options);
                if(ok){if(j)strcat(node->options,"\n");strcat(node->options,text);}JS_FreeValue(app->context,item);
            }
        }
        JS_FreeValue(app->context,options);if(!ok || !node->options[0])return false;
        unsigned count=1;for(const char *c=node->options;*c;++c)if(*c=='\n')++count;
        if(count>24 || !optional_integer(app,value,"value",&node->value,0,0,(int)count-1))return false;
        node->min=0;node->max=(int)count-1;
    }
    node->style.hidden=modal_widget(node->type);
    node->style.radius=-1;node->style.border_width=-1;node->style.opacity=255;node->style.align=node->type==BUTTON ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
    if(!parse_style(app,value,&node->style))return false;
    node->text_revision=node->value_revision=node->style_revision=node->image_revision=1;
    JSValue children=source_property(app,value,"children");
    bool valid=JS_IsUndefined(children);
    if (!valid && container_widget(node->type) && JS_IsArray(children)) {
        JSValue length=source_property(app,children,"length"); uint32_t count=0;
        valid=JS_ToUint32(app->context,&count,length)==0 && count<=MAX_NODES;
        JS_FreeValue(app->context,length);
        for (uint32_t i=0;valid && i<count;++i) {
            JSValue child=JS_GetPropertyUint32(app->context,children,i);
            valid=parse_node(app,child,(int)index,depth+1); JS_FreeValue(app->context,child);
        }
    }
    JS_FreeValue(app->context,children); return valid;
}

// Only run before initial readiness, or after the UI has acknowledged that all
// previous page objects have been detached. No LVGL object survives this reset.
static void reset_page_nodes(instance_t *app)
{
    for(unsigned i=0;i<app->node_count;++i){
        for(unsigned e=0;e<EVENT_COUNT;++e)JS_FreeValue(app->context,app->nodes[i].handlers[e]);
        memset(&app->nodes[i],0,sizeof(app->nodes[i]));
        for(unsigned e=0;e<EVENT_COUNT;++e)app->nodes[i].handlers[e]=JS_UNDEFINED;
    }
    app->node_count=0;
}
static bool parse_page(instance_t *app,unsigned page)
{
    reset_page_nodes(app);
    if(page>=app->page_count || !parse_node(app,app->page_roots[page],-1,0))return false;
    for(unsigned i=0;i<app->node_count;++i){node_model_t *model=&app->nodes[i].model;
        if(model->text_key[0])for(unsigned language=0;language<2;++language){JSValue text=translated(app,model->text_key,language);bool valid=copy_string(app->context,text,model->translated[language],sizeof(model->translated[language]));JS_FreeValue(app->context,text);if(!valid)return false;}
        if(model->subtitle_key[0])for(unsigned language=0;language<2;++language){JSValue text=translated(app,model->subtitle_key,language);bool valid=copy_string(app->context,text,model->subtitle_translated[language],sizeof(model->subtitle_translated[language]));JS_FreeValue(app->context,text);if(!valid)return false;}
    }
    if(ouse_budget_exceeded(&app->budget)){app->interrupted=true;return false;}
    return true;
}
static bool parse_page_stage(instance_t *app,unsigned page,const char *phase)
{
    begin_startup(app);int64_t started=esp_timer_get_time();
    bool valid=parse_page(app,page);
    if(!valid)ESP_LOGE("OuseJS","%s %s page=%s: nodes=%u; elapsed=%lld us; timeout=%u",
        app->id,phase,page<app->page_count ? app->page_ids[page] : "?",app->node_count,
        (long long)(esp_timer_get_time()-started),app->interrupted ? 1u : 0u);
    return valid;
}
static bool valid_page_id(const char *id)
{
    if(!id || !((*id>='A' && *id<='Z') || (*id>='a' && *id<='z')))return false;
    for(const char *c=id+1;*c;++c)if(!((*c>='A' && *c<='Z') || (*c>='a' && *c<='z') || (*c>='0' && *c<='9') || *c=='_' || *c=='-'))return false;
    return true;
}
static bool parse_pages(instance_t *app,JSValueConst document)
{
    JSContext *context=app->context;JSValue root=source_property(app,document,"root"),pages=source_property(app,document,"pages");
    bool valid=true;unsigned initial=0;
    if(JS_IsUndefined(pages)){
        app->page_count=1;strcpy(app->page_ids[0],"main");app->page_roots[0]=JS_DupValue(context,root);
    }else{
        valid=JS_IsUndefined(root) && JS_IsArray(pages);
        JSValue length=source_property(app,pages,"length");uint32_t count=0;
        valid=valid && JS_ToUint32(context,&count,length)==0 && count>0 && count<=MAX_PAGES;JS_FreeValue(context,length);
        char initial_id[32]="";valid=valid && string_property(app,document,"initialPage",initial_id,sizeof(initial_id),true);
        for(unsigned i=0;valid && i<count;++i){
            JSValue page=JS_GetPropertyUint32(context,pages,i);
            valid=JS_IsObject(page) && !JS_IsArray(page) && string_property(app,page,"id",app->page_ids[i],sizeof(app->page_ids[i]),true) && valid_page_id(app->page_ids[i]);
            for(unsigned prior=0;valid && prior<i;++prior)if(!strcmp(app->page_ids[prior],app->page_ids[i]))valid=false;
            app->page_roots[i]=source_property(app,page,"root");JS_FreeValue(context,page);app->page_count=i+1;
        }
        bool found=false;for(unsigned i=0;i<app->page_count;++i)if(!strcmp(initial_id,app->page_ids[i])){initial=i;found=true;break;}
        valid=valid && found;
    }
    JS_FreeValue(context,root);JS_FreeValue(context,pages);
    if(ouse_budget_exceeded(&app->budget)){
        ESP_LOGE("OuseJS","%s page catalog: elapsed=%lld us; timeout=1",app->id,
            (long long)(esp_timer_get_time()-(app->budget.started_us)));return false;
    }
    for(unsigned i=0;valid && i<app->page_count;++i){
        valid=parse_page_stage(app,i,"validate");
        vTaskDelay(1); // each independent bounded page gives the UI task a turn
    }
    if(!valid)return false;
    // Each page has been validated independently with the same 96-node buffer.
    // Only the initial page's native model remains allocated.
    if(!parse_page_stage(app,initial,"initial"))return false;
    app->page_stack[0]=initial;app->page_depth=1;app->navigation_action=NAV_OPEN;return true;
}

static bool navigation_worker(instance_t *app)
{
    model_lock(app);navigation_stage_t stage=app->navigation_stage;bool closed=app->closed,faulted=app->faulted;
    unsigned target=app->navigation_target;navigation_action_t action=app->navigation_action;app->worker_ui=app->snapshot;model_unlock(app);
    if(closed || faulted || stage==NAV_IDLE)return true;
    if(stage!=NAV_DETACHED)return false;
    app->parsing_page=true; // bounded schema work, never application JavaScript
    bool parsed=parse_page_stage(app,target,"navigate");app->parsing_page=false;
    if(!parsed){fault(app,"Unable to create application page");return true;}
    xQueueReset(app->events);
    model_lock(app);
    if(action==NAV_PUSH)app->page_stack[app->page_depth++]=target;
    else if(action==NAV_BACK)--app->page_depth;
    else app->page_stack[app->page_depth-1]=target;
    app->ready=true;app->navigation_stage=NAV_MODEL_READY;model_unlock(app);
    return false;
}

// Everything below this point that uses LVGL is called only by the UI task.
static void capture_ui(ui_snapshot_t *snapshot)
{
    lv_display_t *display=lv_display_get_default();
    snapshot->width=lv_display_get_horizontal_resolution(display);snapshot->height=lv_display_get_vertical_resolution(display);
    int32_t app_width,app_height;
    ouse_core_app_size(&app_width,&app_height);
    snapshot->app_width=(int)app_width;snapshot->app_height=(int)app_height;
    snapshot->density=ouse_ui_density();snapshot->locale=ouse_ui_locale();snapshot->font_percent=ouse_ui_font_percent();
    snapshot->brightness=ouse_core_brightness();snapshot->dark=ouse_theme_get()==OUSE_THEME_DARK;snapshot->colors=ouse_theme_colors();
    for(unsigned i=0;i<3;++i)snapshot->revisions[i]=ouse_ui_revision(i);
    snapshot->keyboard_visible=false;snapshot->keyboard_height=snapshot->keyboard_inset=0;
}
static void input_event(lv_event_t *event)
{
    instance_t *app=lv_event_get_user_data(event);if(app->applying_model)return;lv_obj_t *object=lv_event_get_target_obj(event);lv_event_code_t code=lv_event_get_code(event);
    unsigned type=code==LV_EVENT_CLICKED ? EVENT_CLICK : code==LV_EVENT_VALUE_CHANGED ? EVENT_CHANGE : code==LV_EVENT_FOCUSED ? EVENT_FOCUS : code==LV_EVENT_DEFOCUSED ? EVENT_BLUR : EVENT_SUBMIT;
    if(code!=LV_EVENT_CLICKED && code!=LV_EVENT_VALUE_CHANGED && code!=LV_EVENT_FOCUSED && code!=LV_EVENT_DEFOCUSED && code!=LV_EVENT_READY)return;
    model_lock(app);bool accept=app->visible && !app->closed && !app->faulted && app->navigation_stage==NAV_IDLE;uint32_t generation=app->generation;model_unlock(app);
    if(!accept)return;
    for(unsigned i=0;i<app->node_count;++i){node_t *node=&app->nodes[i];if(node->object!=object)continue;
        input_event_t input={.node=i,.event=type,.generation=generation};node_type_t kind=node->model.type;
        if(kind==TEXT_INPUT){
            const char *text=lv_textarea_get_text(object);size_t length=strlen(text);
            if(length>MAX_TEXT){size_t n=MAX_TEXT;while(n && (((unsigned char)text[n]&0xc0)==0x80))--n;memcpy(input.text,text,n);input.text[n]=0;lv_textarea_set_text(object,input.text);return;}
            memcpy(input.text,text,length+1);
        }
        else if(kind==SLIDER)input.value=lv_slider_get_value(object);
        else if(kind==SWITCH || kind==CHECKBOX)input.value=lv_obj_has_state(object,LV_STATE_CHECKED);
        else if(kind==DROPDOWN)input.value=lv_dropdown_get_selected(object);
        else if(kind==ARC)input.value=lv_arc_get_value(object);
        model_lock(app);
        if(app->navigation_stage!=NAV_IDLE || !node_visible_locked(app,i,true)){model_unlock(app);break;}
        if(kind==TEXT_INPUT && type==EVENT_CHANGE)strcpy(node->model.text,input.text);
        else if(value_widget(kind) && type==EVENT_CHANGE)node->model.value=input.value;
        if(xQueueSend(app->events,&input,0)!=pdTRUE)++app->dropped_events;
        model_unlock(app);wake_worker();break;
    }
}
static int units(instance_t *app,int value) {return app->dp_units ? ouse_ui_dp(value) : value;}
static uint32_t resolve_color(uint32_t color)
{
    if(!(color&COLOR_THEME))return color;
    ouse_theme_colors_t theme=ouse_theme_colors();
    switch(color&~COLOR_THEME){
    case COLOR_BACKGROUND:return theme.background;case COLOR_SURFACE:return theme.surface;
    case COLOR_TEXT:return theme.text;case COLOR_SECONDARY:return theme.secondary;
    case COLOR_ACCENT:return theme.accent;case COLOR_BUTTON:return theme.button;
    case COLOR_PRESSED:return theme.pressed;case COLOR_BORDER:return theme.border;
    case COLOR_DISABLED:return theme.disabled;case COLOR_ERROR:return theme.error;
    case COLOR_DESKTOP:return theme.desktop;case COLOR_ICON:return theme.icon;
    default:return theme.text;
    }
}
static bool circular_control(node_type_t type) {return type==SWITCH || type==SLIDER || type==PROGRESS;}
static bool arc_control(node_type_t type) {return type==ARC || type==SPINNER;}
static bool toggle_control(node_type_t type) {return type==SWITCH || type==CHECKBOX;}
static const lv_font_t *control_symbol_font(lv_obj_t *object)
{
    // The UI text font deliberately contains text/CJK, not Font Awesome private
    // characters. Symbol-bearing parts must use a font that includes LV_SYMBOL.
    return lv_font_get_line_height(lv_obj_get_style_text_font(object,LV_PART_MAIN))>=28 ? &lv_font_montserrat_24 : &lv_font_montserrat_16;
}
static void set_control_fill(lv_obj_t *object,lv_part_t part,lv_state_t state,uint32_t color)
{
    lv_obj_set_style_bg_color(object,lv_color_hex(resolve_color(color)),part|state);
    lv_obj_set_style_bg_opa(object,LV_OPA_COVER,part|state);
}
static void clear_control_fill(lv_obj_t *object,lv_part_t part,lv_state_t state)
{
    lv_obj_remove_local_style_prop(object,LV_STYLE_BG_COLOR,part|state);
    lv_obj_remove_local_style_prop(object,LV_STYLE_BG_OPA,part|state);
}
static void refresh_control_skin(instance_t *app,node_t *node,const node_model_t *model)
{
    lv_obj_t *object=node->object;ouse_theme_colors_t colors=ouse_theme_colors();
    lv_color_t on_accent=lv_color_hex(ouse_theme_get()==OUSE_THEME_DARK ? 0x10212a : 0xffffff);
    int font_height=lv_font_get_line_height(lv_obj_get_style_text_font(object,LV_PART_MAIN));
    if(model->type==CHECKBOX || model->type==DROPDOWN || model->type==TEXT_INPUT){
        int minimum=font_height+ouse_ui_dp(16)+(model->type==CHECKBOX ? 0 : 2*ouse_ui_dp(1));
        if(minimum<ouse_ui_dp(40))minimum=ouse_ui_dp(40);
        lv_obj_set_style_min_height(object,minimum,0);
    }else if(model->type==TOOLBAR || model->type==LIST_ITEM){
        int minimum=font_height+2*units(app,(int)model->padding);
        int base=ouse_ui_dp(model->type==TOOLBAR || !node->subtitle ? 56 : 76);
        if(node->subtitle)minimum+=lv_font_get_line_height(lv_obj_get_style_text_font(node->subtitle,LV_PART_MAIN))+ouse_ui_dp(4);
        if(minimum<base)minimum=base;
        lv_obj_set_style_min_height(object,minimum,0);
    }
    if(model->type==SLIDER){
        lv_obj_set_style_border_color(object,lv_color_hex(colors.accent),LV_PART_KNOB);
    }else if(model->type==CHECKBOX){
        lv_obj_set_style_border_color(object,lv_color_hex(colors.border),LV_PART_INDICATOR);
        lv_obj_set_style_border_color(object,lv_color_hex(colors.accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_style_text_font(object,control_symbol_font(object),LV_PART_INDICATOR);
        lv_obj_set_style_text_color(object,on_accent,LV_PART_INDICATOR|LV_STATE_CHECKED);
    }else if(model->type==DROPDOWN){
        lv_obj_set_style_text_font(object,control_symbol_font(object),LV_PART_INDICATOR);
        lv_obj_set_style_text_color(object,lv_color_hex(colors.secondary),LV_PART_INDICATOR);
        lv_obj_t *list=lv_dropdown_get_list(object);
        if(list){
            lv_obj_set_style_border_color(list,lv_color_hex(colors.border),0);
            lv_obj_set_style_shadow_color(list,lv_color_black(),0);
            lv_obj_set_style_bg_color(list,lv_color_hex(colors.accent),LV_PART_SELECTED|LV_STATE_CHECKED);
            lv_obj_set_style_text_color(list,on_accent,LV_PART_SELECTED|LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(list,lv_color_hex(colors.pressed),LV_PART_SELECTED|LV_STATE_PRESSED);
            lv_obj_set_style_text_color(list,lv_color_hex(colors.text),LV_PART_SELECTED|LV_STATE_PRESSED);
            // Font bindings update hidden lists on global font changes as well.
            lv_obj_set_style_text_font(list,ouse_ui_font(model->font_sp ? model->font_sp : 16),0);
        }
    }else if(arc_control(model->type)){
        lv_obj_set_style_arc_color(object,lv_color_hex(colors.border),LV_PART_MAIN);
        lv_obj_set_style_arc_color(object,lv_color_hex(colors.accent),LV_PART_INDICATOR);
        lv_obj_set_style_border_color(object,lv_color_hex(colors.accent),LV_PART_KNOB);
    }else if(model->type==TEXT_INPUT){
        lv_obj_set_style_border_color(object,lv_color_hex(colors.accent),LV_PART_CURSOR|LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(object,lv_color_hex(colors.secondary),LV_PART_TEXTAREA_PLACEHOLDER);
    }
    // Arc width is a drawing property, not a background fill. The color-only
    // system roles cannot supply it when all stock LVGL themes are disabled.
    if(arc_control(model->type)){
        int shorter=units(app,model->w<model->h ? model->w : model->h);
        int stroke=ouse_ui_dp(model->type==SPINNER ? 4 : 8);
        if(stroke>shorter/5)stroke=shorter/5;
        if(stroke<2)stroke=2;
        lv_obj_set_style_arc_width(object,stroke,LV_PART_MAIN);lv_obj_set_style_arc_width(object,stroke,LV_PART_INDICATOR);
    }
}
static void initialize_control_skin(node_t *node,const node_model_t *model)
{
    lv_obj_t *object=node->object;
    if(model->type==SWITCH){ouse_control_skin_switch(object);}
    else if(model->type==SLIDER || model->type==PROGRESS){
        ouse_theme_apply(object,OUSE_STYLE_TRACK,LV_PART_MAIN);ouse_theme_apply(object,OUSE_STYLE_ACCENT,LV_PART_INDICATOR);
        lv_obj_set_style_radius(object,LV_RADIUS_CIRCLE,LV_PART_MAIN);lv_obj_set_style_radius(object,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
        if(model->type==SLIDER){
            lv_obj_set_style_radius(object,LV_RADIUS_CIRCLE,LV_PART_KNOB);lv_obj_set_style_bg_color(object,lv_color_white(),LV_PART_KNOB);
            lv_obj_set_style_bg_opa(object,LV_OPA_COVER,LV_PART_KNOB);lv_obj_set_style_border_width(object,ouse_ui_dp(2),LV_PART_KNOB);
            lv_obj_set_style_pad_all(object,ouse_ui_dp(3),LV_PART_KNOB);lv_obj_set_ext_click_area(object,ouse_ui_dp(10));
        }
    }else if(model->type==CHECKBOX){
        ouse_theme_apply(object,OUSE_STYLE_PANEL,LV_PART_INDICATOR);ouse_theme_apply(object,OUSE_STYLE_ACCENT,LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_style_radius(object,ouse_ui_dp(4),LV_PART_INDICATOR);lv_obj_set_style_border_width(object,ouse_ui_dp(2),LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(object,0,LV_PART_INDICATOR);lv_obj_set_style_pad_column(object,ouse_ui_dp(12),0);
        lv_obj_set_style_pad_ver(object,ouse_ui_dp(8),0);lv_obj_set_style_min_height(object,ouse_ui_dp(40),0);
        lv_obj_set_style_bg_image_src(object,LV_SYMBOL_OK,LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_opa(object,LV_OPA_COVER,LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_ext_click_area(object,ouse_ui_dp(4));
    }else if(model->type==DROPDOWN){
        ouse_theme_apply(object,OUSE_STYLE_BUTTON,0);ouse_theme_apply(object,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(object,ouse_ui_dp(12),0);lv_obj_set_style_pad_ver(object,ouse_ui_dp(8),0);
        lv_obj_set_style_pad_column(object,ouse_ui_dp(12),0);lv_obj_set_style_min_height(object,ouse_ui_dp(40),0);
        lv_dropdown_set_symbol(object,LV_SYMBOL_DOWN);
        lv_obj_t *list=lv_dropdown_get_list(object);
        if(list){
            ouse_theme_apply(list,OUSE_STYLE_PANEL,0);lv_obj_set_style_radius(list,ouse_ui_dp(12),0);
            lv_obj_set_style_border_width(list,ouse_ui_dp(1),0);lv_obj_set_style_pad_hor(list,ouse_ui_dp(12),0);lv_obj_set_style_pad_ver(list,ouse_ui_dp(8),0);
            lv_obj_set_style_text_line_space(list,ouse_ui_dp(16),0);lv_obj_set_style_max_height(list,ouse_ui_dp(240),0);
            lv_obj_set_style_shadow_width(list,ouse_ui_dp(12),0);lv_obj_set_style_shadow_opa(list,LV_OPA_20,0);
            lv_obj_set_style_bg_opa(list,LV_OPA_COVER,LV_PART_SELECTED|LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(list,LV_OPA_COVER,LV_PART_SELECTED|LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(list,LV_OPA_40,LV_PART_SCROLLBAR);lv_obj_set_style_bg_color(list,lv_color_hex(0x888888),LV_PART_SCROLLBAR);
            lv_obj_set_style_width(list,ouse_ui_dp(3),LV_PART_SCROLLBAR);lv_obj_set_style_radius(list,LV_RADIUS_CIRCLE,LV_PART_SCROLLBAR);
            ouse_ui_use_font(list,model->font_sp ? model->font_sp : 16);
        }
    }else if(arc_control(model->type)){
        lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,LV_PART_MAIN);lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(object,true,LV_PART_MAIN);lv_obj_set_style_arc_rounded(object,true,LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(object,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_arc_opa(object,LV_OPA_COVER,LV_PART_INDICATOR);
        lv_obj_set_style_radius(object,LV_RADIUS_CIRCLE,LV_PART_KNOB);
        if(model->type==ARC){
            lv_obj_set_style_bg_color(object,lv_color_white(),LV_PART_KNOB);lv_obj_set_style_bg_opa(object,LV_OPA_COVER,LV_PART_KNOB);
            lv_obj_set_style_border_width(object,ouse_ui_dp(2),LV_PART_KNOB);lv_obj_set_style_pad_all(object,ouse_ui_dp(3),LV_PART_KNOB);
        }else lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,LV_PART_KNOB);
    }else if(model->type==TEXT_INPUT){
        lv_obj_set_style_pad_hor(object,ouse_ui_dp(12),0);lv_obj_set_style_pad_ver(object,ouse_ui_dp(8),0);
        lv_obj_set_style_border_side(object,LV_BORDER_SIDE_LEFT,LV_PART_CURSOR|LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(object,ouse_ui_dp(2),LV_PART_CURSOR|LV_STATE_FOCUSED);
        lv_obj_set_style_anim_duration(object,400,LV_PART_CURSOR);lv_obj_set_style_pad_left(object,-ouse_ui_dp(1),LV_PART_CURSOR);
    }
}
static void apply_style(instance_t *app,node_t *node,const node_model_t *model)
{
    lv_obj_t *object=node->object;const node_style_t *style=&model->style;
    refresh_control_skin(app,node,model);
    if(style->bg_set || model->type==DIVIDER){
        lv_obj_set_style_bg_color(object,lv_color_hex(resolve_color(style->bg_set ? style->bg : COLOR_THEME|COLOR_BORDER)),0);
        lv_obj_set_style_bg_opa(object,arc_control(model->type) ? LV_OPA_TRANSP : LV_OPA_COVER,0);
    }else {lv_obj_remove_local_style_prop(object,LV_STYLE_BG_COLOR,0);lv_obj_remove_local_style_prop(object,LV_STYLE_BG_OPA,0);}
    if(arc_control(model->type)){
        if(style->bg_set)lv_obj_set_style_arc_color(object,lv_color_hex(resolve_color(style->bg)),LV_PART_MAIN);
        // Arc backgrounds remain transparent even when the user changes a color.
        lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,LV_PART_MAIN);
    }
    if(toggle_control(model->type)){
        if(style->bg_set)set_control_fill(object,LV_PART_INDICATOR,0,style->bg);else clear_control_fill(object,LV_PART_INDICATOR,0);
    }
    if(style->color_set)lv_obj_set_style_text_color(object,lv_color_hex(resolve_color(style->color)),0);
    else {
        lv_obj_remove_local_style_prop(object,LV_STYLE_TEXT_COLOR,0);
        // Plain labels/captions must take the theme foreground. With stock LVGL
        // themes disabled the default can stay white, which disappears on the
        // light app background and looks like an empty screen.
        if(model->type==LABEL || model->type==BUTTON || model->type==CHECKBOX
            || model->type==CARD || model->type==LIST_ITEM || model->type==TOOLBAR
            || modal_widget(model->type)){
            lv_color_t fg=lv_color_hex(ouse_theme_colors().text);
            lv_obj_set_style_text_color(object,fg,0);
            if(node->caption && node->caption!=object)lv_obj_set_style_text_color(node->caption,fg,0);
        }
    }
    uint32_t default_border=COLOR_THEME|COLOR_BORDER;
    if(style->border_set || model->type==TEXT_INPUT || model->type==DROPDOWN || model->type==BUTTON)
        lv_obj_set_style_border_color(object,lv_color_hex(resolve_color(style->border_set ? style->border : default_border)),0);
    else lv_obj_remove_local_style_prop(object,LV_STYLE_BORDER_COLOR,0);
    lv_obj_set_style_border_width(object,units(app,style->border_width>=0 ? style->border_width
        : (model->type==TEXT_INPUT || model->type==DROPDOWN || model->type==BUTTON) ? 1 : 0),0);
    int radius;
    if(style->radius>=0)radius=units(app,style->radius);
    else if(circular_control(model->type))radius=LV_RADIUS_CIRCLE;
    else radius=units(app,model->type==BUTTON ? 14 : model->type==TEXT_INPUT ? 10 : model->type==DROPDOWN ? 12 : model->type==CARD ? 20 : modal_widget(model->type) ? 24 : 0);
    lv_obj_set_style_radius(object,radius,0);
    if(circular_control(model->type)){
        lv_obj_set_style_radius(object,radius,LV_PART_INDICATOR);
        if(model->type!=PROGRESS)lv_obj_set_style_radius(object,radius,LV_PART_KNOB);
    }else if(model->type==CHECKBOX){
        lv_obj_set_style_radius(object,style->radius>=0 ? radius : ouse_ui_dp(4),LV_PART_INDICATOR);
        if(style->border_set)lv_obj_set_style_border_color(object,lv_color_hex(resolve_color(style->border)),LV_PART_INDICATOR);
        lv_obj_set_style_border_width(object,style->border_width>=0 ? units(app,style->border_width) : ouse_ui_dp(2),LV_PART_INDICATOR);
    }
    if(arc_control(model->type)){
        if(style->accent_set)lv_obj_set_style_arc_color(object,lv_color_hex(resolve_color(style->accent)),LV_PART_INDICATOR);
    }else if(!toggle_control(model->type)){
        if(style->accent_set)set_control_fill(object,LV_PART_INDICATOR,0,style->accent);else clear_control_fill(object,LV_PART_INDICATOR,0);
    }
    if(model->type==SWITCH || model->type==SLIDER || model->type==ARC)
        lv_obj_set_style_bg_color(object,lv_color_hex(style->thumb_set ? resolve_color(style->thumb) : 0xffffff),LV_PART_KNOB);
    lv_obj_set_style_opa(object,(lv_opa_t)style->opacity,0);
    // MAIN retains the public background semantics (and diagnostic values).
    // Compound widgets also map these colors to the part that is actually drawn.
    const lv_state_t selectors[]={LV_STATE_PRESSED,LV_STATE_CHECKED,LV_STATE_DISABLED};
    for(unsigned i=0;i<3;++i){const node_state_style_t *state=&style->states[i];lv_state_t selector=selectors[i];
        if(state->bg_set){lv_obj_set_style_bg_color(object,lv_color_hex(resolve_color(state->bg)),selector);lv_obj_set_style_bg_opa(object,arc_control(model->type) ? LV_OPA_TRANSP : LV_OPA_COVER,selector);}
        else clear_control_fill(object,LV_PART_MAIN,selector);
        if(arc_control(model->type)){
            if(state->bg_set)lv_obj_set_style_arc_color(object,lv_color_hex(resolve_color(state->bg)),LV_PART_MAIN|selector);
            else lv_obj_remove_local_style_prop(object,LV_STYLE_ARC_COLOR,LV_PART_MAIN|selector);
            if(state->accent_set)lv_obj_set_style_arc_color(object,lv_color_hex(resolve_color(state->accent)),LV_PART_INDICATOR|selector);
            else lv_obj_remove_local_style_prop(object,LV_STYLE_ARC_COLOR,LV_PART_INDICATOR|selector);
        }else if(toggle_control(model->type)){
            // The checked background wins over the generic accent override. A
            // missing checked override must retain the base accentColor value.
            bool filled=state->bg_set || state->accent_set || (selector==LV_STATE_CHECKED && style->accent_set);
            uint32_t color=state->bg_set ? state->bg : state->accent_set ? state->accent : style->accent;
            if(filled)set_control_fill(object,LV_PART_INDICATOR,selector,color);else clear_control_fill(object,LV_PART_INDICATOR,selector);
        }else {
            if(state->accent_set)set_control_fill(object,LV_PART_INDICATOR,selector,state->accent);else clear_control_fill(object,LV_PART_INDICATOR,selector);
        }
        if(model->type==SWITCH || model->type==SLIDER || model->type==ARC){
            if(state->thumb_set)set_control_fill(object,LV_PART_KNOB,selector,state->thumb);else clear_control_fill(object,LV_PART_KNOB,selector);
        }
        if(state->color_set)lv_obj_set_style_text_color(object,lv_color_hex(resolve_color(state->color)),selector);else lv_obj_remove_local_style_prop(object,LV_STYLE_TEXT_COLOR,selector);
        if(state->border_set)lv_obj_set_style_border_color(object,lv_color_hex(resolve_color(state->border)),selector);else lv_obj_remove_local_style_prop(object,LV_STYLE_BORDER_COLOR,selector);
        if(model->type==CHECKBOX){
            if(state->border_set)lv_obj_set_style_border_color(object,lv_color_hex(resolve_color(state->border)),LV_PART_INDICATOR|selector);
            else if(selector!=LV_STATE_CHECKED)lv_obj_remove_local_style_prop(object,LV_STYLE_BORDER_COLOR,LV_PART_INDICATOR|selector);
        }
        if(state->opacity_set || i==2)lv_obj_set_style_opa(object,(lv_opa_t)(state->opacity_set ? state->opacity : 128),selector);else lv_obj_remove_local_style_prop(object,LV_STYLE_OPA,selector);
    }
    lv_obj_t *visible_object=node->overlay ? node->overlay : object;
    if(style->hidden){ouse_ime_hide_root(object);lv_obj_add_flag(visible_object,LV_OBJ_FLAG_HIDDEN);}else lv_obj_remove_flag(visible_object,LV_OBJ_FLAG_HIDDEN);
    if(style->disabled)lv_obj_add_state(object,LV_STATE_DISABLED);else lv_obj_remove_state(object,LV_STATE_DISABLED);
    lv_obj_set_flex_grow(object,(uint8_t)style->flex_grow);
    lv_obj_set_style_text_align(object,(lv_text_align_t)style->align,0);
    if(node->caption)lv_obj_set_style_text_align(node->caption,(lv_text_align_t)style->align,0);
}
static void sync_modal_visibility(instance_t *app)
{
    bool disabled[MAX_NODES]={0},hidden[MAX_NODES]={0};model_lock(app);
    for(unsigned i=0;i<app->node_count;++i)for(int n=(int)i;n>=0;n=app->nodes[n].model.parent){
        if(app->nodes[n].model.style.hidden)hidden[i]=true;
        if(app->nodes[n].model.style.disabled)disabled[i]=true;
    }
    model_unlock(app);
    for(unsigned i=0;i<app->node_count;++i){node_t *node=&app->nodes[i];
        // A disabled group also disables native input of its descendants. Merely
        // dropping their JS events would still let a textarea open the keyboard.
        if(disabled[i]){ouse_ime_hide_root(node->object);if(!lv_obj_has_state(node->object,LV_STATE_DISABLED))lv_obj_add_state(node->object,LV_STATE_DISABLED);}
        else if(lv_obj_has_state(node->object,LV_STATE_DISABLED))lv_obj_remove_state(node->object,LV_STATE_DISABLED);
        if(node->model.type==DROPDOWN && (hidden[i] || disabled[i]))lv_dropdown_close(node->object);
        if(!node->overlay)continue;
        bool was_hidden=lv_obj_has_flag(node->overlay,LV_OBJ_FLAG_HIDDEN);
        if(hidden[i]){ouse_ime_hide_root(node->object);lv_obj_add_flag(node->overlay,LV_OBJ_FLAG_HIDDEN);}
        else {lv_obj_remove_flag(node->overlay,LV_OBJ_FLAG_HIDDEN);if(was_hidden)lv_obj_move_foreground(node->overlay);}
    }
}
static bool dismiss_modal(instance_t *app,unsigned index,const char *reason)
{
    if(index>=app->node_count)return false;
    node_t *node=&app->nodes[index];if(!node->overlay)return false;
    model_lock(app);
    if(!app->visible || app->closed || node->model.style.hidden){model_unlock(app);return false;}
    set_node_hidden_locked(app,node,true);
    input_event_t input={.node=index,.event=EVENT_DISMISS,.generation=app->generation};snprintf(input.text,sizeof(input.text),"%s",reason);
    if(xQueueSend(app->events,&input,0)!=pdTRUE)++app->dropped_events;
    model_unlock(app);ouse_ime_hide_root(node->object);lv_obj_add_flag(node->overlay,LV_OBJ_FLAG_HIDDEN);wake_worker();return true;
}
static void modal_backdrop_event(lv_event_t *event)
{
    instance_t *app=lv_event_get_user_data(event);lv_obj_t *target=lv_event_get_target_obj(event);
    for(unsigned i=0;i<app->node_count;++i)if(app->nodes[i].overlay==target){(void)dismiss_modal(app,i,"backdrop");break;}
}
static void show_text(instance_t *app,node_t *node,const node_model_t *model)
{
    app->applying_model=true;
    if(model->type==TEXT_INPUT){
        // User keystrokes already changed the widget; avoid moving its cursor.
        if(strcmp(lv_textarea_get_text(node->object),model->text))lv_textarea_set_text(node->object,model->text);
    }else if(model->type==CHECKBOX)lv_checkbox_set_text(node->object,model->text);
    else if(node->caption){
        if(model->text_key[0])ouse_ui_text_pair(node->caption,model->translated[0],model->translated[1]);
        else {ouse_ui_unbind_text(node->caption);lv_label_set_text(node->caption,model->text);}
    }
    if(node->subtitle){
        if(model->subtitle_key[0])ouse_ui_text_pair(node->subtitle,model->subtitle_translated[0],model->subtitle_translated[1]);
        else {ouse_ui_unbind_text(node->subtitle);lv_label_set_text(node->subtitle,model->subtitle);}
    }
    if(node->caption && container_widget(model->type)){
        if(model->text[0] || model->text_key[0])lv_obj_remove_flag(node->caption,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(node->caption,LV_OBJ_FLAG_HIDDEN);
    }
    app->applying_model=false;
}
static void show_value(instance_t *app,node_t *node,const node_model_t *model)
{
    app->applying_model=true;
    switch(model->type){
    case SLIDER:lv_slider_set_value(node->object,model->value,LV_ANIM_OFF);break;
    case SWITCH:case CHECKBOX:if(model->value)lv_obj_add_state(node->object,LV_STATE_CHECKED);else lv_obj_remove_state(node->object,LV_STATE_CHECKED);break;
    case DROPDOWN:lv_dropdown_set_selected(node->object,(uint32_t)model->value);break;
    case PROGRESS:lv_bar_set_value(node->object,model->value,LV_ANIM_OFF);break;
    case ARC:lv_arc_set_value(node->object,model->value);break;
    default:break;
    }
    app->applying_model=false;
}
static void build_nodes(instance_t *app)
{
    for(unsigned i=0;i<app->node_count;++i){
        node_t *node=&app->nodes[i];node_model_t model;model_lock(app);model=node->model;model_unlock(app);
        lv_obj_t *parent=model.parent<0 ? app->viewport : app->nodes[model.parent].object;
        if(modal_widget(model.type)){
            // The modal owns an app-local backdrop, never a global LVGL layer.
            // Its children and handlers disappear with the app's own viewport.
            node->overlay=lv_obj_create(app->viewport);lv_obj_remove_style_all(node->overlay);
            lv_obj_set_size(node->overlay,LV_PCT(100),LV_PCT(100));lv_obj_remove_flag(node->overlay,LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(node->overlay,lv_color_black(),0);lv_obj_set_style_bg_opa(node->overlay,LV_OPA_50,0);
            lv_obj_add_event_cb(node->overlay,modal_backdrop_event,LV_EVENT_CLICKED,app);parent=node->overlay;
        }
        switch(model.type){
        case CONTAINER:case CARD:case LIST:case LIST_ITEM:case TOOLBAR:case DIVIDER:case DIALOG:case BOTTOM_SHEET:node->object=lv_obj_create(parent);break;
        case LABEL:node->object=lv_label_create(parent);break;
        case BUTTON:node->object=lv_button_create(parent);break;
        case SLIDER:node->object=lv_slider_create(parent);break;
        case IMAGE:node->object=lv_image_create(parent);break;
        case TEXT_INPUT:node->object=lv_textarea_create(parent);break;
        case SWITCH:node->object=lv_switch_create(parent);break;
        case CHECKBOX:node->object=lv_checkbox_create(parent);break;
        case DROPDOWN:node->object=lv_dropdown_create(parent);break;
        case PROGRESS:node->object=lv_bar_create(parent);break;
        case SPINNER:node->object=lv_spinner_create(parent);break;
        case ARC:node->object=lv_arc_create(parent);break;
        }
        lv_obj_t *object=node->object;
        if(container_widget(model.type) || model.type==BUTTON || model.type==DIVIDER){
            lv_obj_remove_style_all(object);lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE);
            if(model.type==BUTTON){
                ouse_theme_apply(object,OUSE_STYLE_BUTTON,0);ouse_theme_apply(object,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
                node->caption=lv_label_create(object);lv_obj_center(node->caption);
                lv_obj_set_width(node->caption,LV_PCT(100));lv_label_set_long_mode(node->caption,LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_color(node->caption,lv_color_hex(ouse_theme_colors().text),0);
            }else if(model.type==CARD || model.type==LIST_ITEM || modal_widget(model.type)){
                ouse_theme_apply(object,OUSE_STYLE_PANEL,0);
                if(model.type==LIST_ITEM)ouse_theme_apply(object,OUSE_STYLE_PRESSED,LV_STATE_PRESSED);
            }else if(model.type==TOOLBAR)ouse_theme_apply(object,OUSE_STYLE_APP,0);
            else if(model.type==DIVIDER)lv_obj_remove_flag(object,LV_OBJ_FLAG_CLICKABLE);
            if(container_widget(model.type) && text_widget(model.type)){
                lv_obj_t *text_parent=object;
                if(model.type==LIST_ITEM){
                    text_parent=lv_obj_create(object);lv_obj_remove_style_all(text_parent);lv_obj_remove_flag(text_parent,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_size(text_parent,0,LV_SIZE_CONTENT);lv_obj_set_flex_grow(text_parent,1);lv_obj_set_flex_flow(text_parent,LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_style_pad_row(text_parent,units(app,4),0);
                }
                node->caption=lv_label_create(text_parent);lv_label_set_long_mode(node->caption,LV_LABEL_LONG_WRAP);
                lv_obj_set_width(node->caption,model.type==TOOLBAR && model.flow==2 ? 0 : LV_PCT(100));
                if(model.type==TOOLBAR && model.flow==2)lv_obj_set_flex_grow(node->caption,1);
                lv_obj_set_style_text_color(node->caption,lv_color_hex(ouse_theme_colors().text),0);
                if(!model.text[0] && !model.text_key[0])lv_obj_add_flag(node->caption,LV_OBJ_FLAG_HIDDEN);
                if(model.type==LIST_ITEM && (model.subtitle[0] || model.subtitle_key[0])){
                    node->subtitle=lv_label_create(text_parent);lv_obj_set_width(node->subtitle,LV_PCT(100));lv_label_set_long_mode(node->subtitle,LV_LABEL_LONG_WRAP);
                    ouse_ui_use_font(node->subtitle,14);ouse_theme_apply(node->subtitle,OUSE_STYLE_MUTED,0);
                }
            }
        }else if(model.type==LABEL){
            node->caption=object;lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(object,lv_color_hex(ouse_theme_colors().text),0);
        }
        else if(model.type==IMAGE){lv_image_set_src(object,find_image(app,model.image));lv_image_set_inner_align(object,LV_IMAGE_ALIGN_STRETCH);}
        else if(model.type==TEXT_INPUT){
            ouse_theme_apply(object,OUSE_STYLE_PANEL,0);
            lv_textarea_set_one_line(object,!model.multiline);lv_textarea_set_max_length(object,model.max_length);
            lv_textarea_set_placeholder_text(object,model.placeholder);lv_textarea_set_password_mode(object,model.input_type==OUSE_IME_PASSWORD);
            if(model.input_type==OUSE_IME_NUMBER)lv_textarea_set_accepted_chars(object,"0123456789.-");
            (void)ouse_ime_attach(object,app->viewport,(ouse_ime_avoid_t)model.keyboard_mode,(ouse_ime_type_t)model.input_type);
        }else if(model.type==SLIDER || model.type==PROGRESS || model.type==SWITCH){
            if(model.type==SLIDER)lv_slider_set_range(object,model.min,model.max);
            if(model.type==PROGRESS)lv_bar_set_range(object,model.min,model.max);
        }else if(model.type==DROPDOWN){lv_dropdown_set_options(object,model.options);}
        else if(model.type==ARC){lv_arc_set_range(object,model.min,model.max);}
        else if(model.type==SPINNER){lv_spinner_set_anim_params(object,1000,80);}
        lv_obj_set_pos(object,units(app,model.x),units(app,model.y));
        lv_obj_set_size(object,model.fill_w ? LV_PCT(100) : units(app,model.w),model.fill_h ? LV_PCT(100) : model.content_h ? LV_SIZE_CONTENT : units(app,model.h));
        unsigned font_sp=model.font_sp ? model.font_sp : 16;
        ouse_ui_use_font(object,font_sp);
        // Button/card captions are child labels; LVGL text font does not inherit.
        if(node->caption && node->caption!=object)ouse_ui_use_font(node->caption,font_sp);
        if(container_widget(model.type) || model.type==BUTTON || model.type==LABEL || model.type==IMAGE || model.type==DIVIDER)lv_obj_set_style_pad_all(object,units(app,(int)model.padding),0);
        initialize_control_skin(node,&model);
        if(model.type==BUTTON){
            lv_obj_set_style_min_height(object,units(app,40),0);
            // Square/circle buttons: drop default pad so the glyph can fill and center.
            bool round = model.w > 0 && model.w == model.h
                && model.style.radius >= 0 && model.style.radius * 2 >= model.w;
            lv_obj_set_style_pad_all(object,units(app,round ? 0 : 8),0);
            if(round && node->caption){
                lv_obj_set_width(node->caption,LV_SIZE_CONTENT);
                lv_obj_center(node->caption);
            }
        }
        if(model.type==LIST_ITEM)lv_obj_set_style_min_height(object,units(app,node->subtitle ? 76 : 56),0);
        if(model.type==TOOLBAR)lv_obj_set_style_min_height(object,units(app,56),0);
        if(container_widget(model.type)){
            if(model.flow)lv_obj_set_flex_flow(object,model.flow==1 ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_row(object,units(app,(int)model.gap),0);lv_obj_set_style_pad_column(object,units(app,(int)model.gap),0);
            if(model.flow==2)lv_obj_set_flex_align(object,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
            if(model.scroll){lv_obj_add_flag(object,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(object,LV_DIR_VER);}
        }
        if(node->overlay)lv_obj_align(object,model.type==DIALOG ? LV_ALIGN_CENTER : LV_ALIGN_BOTTOM_MID,0,0);
        show_text(app,node,&model);show_value(app,node,&model);apply_style(app,node,&model);
        node->shown_text=model.text_revision;node->shown_value=model.value_revision;node->shown_style=model.style_revision;node->shown_image=model.image_revision;
        lv_obj_add_event_cb(object,input_event,LV_EVENT_ALL,app);
    }
    sync_modal_visibility(app);lv_obj_update_layout(app->viewport);
}
static void home_event(lv_event_t *event) {(void)event;(void)ouse_app_background();}
static void clear_page_ui(instance_t *app)
{
    app->applying_model=true;ouse_ime_hide_root(app->root);
    if(app->built)for(unsigned i=0;i<app->node_count;++i){node_t *node=&app->nodes[i];
        if(!node->object)continue;
        lv_obj_remove_event_cb(node->object,input_event);
        if(node->model.type==DROPDOWN)lv_dropdown_close(node->object);
        if(node->model.type==TEXT_INPUT)ouse_ime_detach(node->object);
    }
    if(app->viewport)lv_obj_clean(app->viewport);
    for(unsigned i=0;i<app->node_count;++i)app->nodes[i].object=app->nodes[i].caption=app->nodes[i].subtitle=app->nodes[i].overlay=NULL;
    for(unsigned i=0;i<app->image_count;++i)lv_image_cache_drop(&app->image_descriptors[i]);
    app->built=false;app->applying_model=false;app->applied_theme_revision=app->applied_display_revision=0;
}
static void show_fault(instance_t *app)
{
    ouse_ime_hide_root(app->root);
    lv_obj_t *panel=lv_obj_create(app->root);lv_obj_remove_style_all(panel);lv_obj_set_size(panel,LV_PCT(100),LV_PCT(100));
    lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);ouse_theme_apply(panel,OUSE_STYLE_APP,0);
    lv_obj_t *label=lv_label_create(panel);ouse_ui_use_font(label,20);ouse_ui_text(label,"应用运行出错");lv_obj_align(label,LV_ALIGN_TOP_MID,0,ouse_ui_dp(40));
    lv_obj_t *button=lv_button_create(panel);lv_obj_remove_style_all(button);ouse_theme_apply(button,OUSE_STYLE_BUTTON,0);
    lv_obj_set_size(button,ouse_ui_dp(180),ouse_ui_dp(44));lv_obj_align(button,LV_ALIGN_BOTTOM_MID,0,-ouse_ui_dp(28));
    lv_obj_add_event_cb(button,home_event,LV_EVENT_CLICKED,NULL);label=lv_label_create(button);ouse_ui_text(label,"回到桌面");lv_obj_center(label);
}
static void ui_pump(lv_timer_t *timer)
{
    instance_t *app=lv_timer_get_user_data(timer);
    model_lock(app);bool detach=app->navigation_stage==NAV_REQUESTED && !app->closed && !app->faulted;
    if(detach)app->ready=false;
    model_unlock(app);
    if(detach)clear_page_ui(app);
    ui_snapshot_t snapshot;capture_ui(&snapshot);
    if(app->viewport){lv_obj_update_layout(app->viewport);snapshot.app_width=lv_obj_get_width(app->viewport);snapshot.app_height=lv_obj_get_height(app->viewport);}
    if(app->built)for(unsigned i=0;i<app->node_count;++i)if(app->nodes[i].model.type==TEXT_INPUT && ouse_ime_focused(app->nodes[i].object)){
        ouse_ime_state_t ime;ouse_ime_get_state(&ime);snapshot.keyboard_visible=ime.visible;snapshot.keyboard_height=ime.visible ? ime.keyboard_height : 0;
        if(ime.visible && ime.mode==OUSE_IME_RESIZE){int inset=lv_obj_get_height(app->root)-snapshot.app_height;snapshot.keyboard_inset=inset>0 ? inset : 0;}break;
    }
    model_lock(app);ui_snapshot_t previous=app->snapshot;model_unlock(app);
    bool theme_changed=snapshot.revisions[0]!=app->applied_theme_revision;
    if(snapshot.revisions[2]!=app->core_display_revision || snapshot.app_width!=previous.app_width || snapshot.app_height!=previous.app_height ||
        snapshot.keyboard_visible!=previous.keyboard_visible || snapshot.keyboard_height!=previous.keyboard_height || snapshot.keyboard_inset!=previous.keyboard_inset)++app->display_revision;
    app->core_display_revision=snapshot.revisions[2];snapshot.revisions[2]=app->display_revision;
    unsigned commands;int values[4];bool save,ready,faulted,visible,home;navigation_stage_t navigation_stage;
    model_lock(app);app->snapshot=snapshot;commands=app->pending_commands;app->pending_commands=0;memcpy(values,app->command_values,sizeof(values));save=app->brightness_save;
    if(detach)app->navigation_stage=NAV_DETACHED;
    ready=app->ready;faulted=app->faulted;visible=app->visible;home=app->home_requested;app->home_requested=false;navigation_stage=app->navigation_stage;model_unlock(app);
    if(detach)wake_worker();
    if(commands & 1u)(void)ouse_core_set_theme((ouse_theme_t)values[0]);
    if(commands & 2u)(void)ouse_core_set_ui((unsigned)values[1],ouse_ui_font_percent());
    if(commands & 4u)(void)ouse_core_set_ui(ouse_ui_locale(),(unsigned)values[2]);
    if(commands & 8u){if(save)(void)ouse_core_set_brightness((uint8_t)values[3]);else (void)ouse_core_preview_brightness((uint8_t)values[3]);}
    if(commands & 16u)(void)ouse_core_sleep_display();
    if(commands & 32u)(void)ouse_core_wake_display();
    if(faulted && !app->fault_shown){app->fault_shown=true;show_fault(app);}
    if(!faulted && ready && !app->built && (visible || navigation_stage==NAV_MODEL_READY)){
        build_nodes(app);
        // Publish the laid-out viewport before the worker can dispatch onChange.
        lv_obj_update_layout(app->viewport);snapshot.app_width=lv_obj_get_width(app->viewport);snapshot.app_height=lv_obj_get_height(app->viewport);
        model_lock(app);app->snapshot=snapshot;app->built=true;app->navigation_stage=NAV_IDLE;app->navigation_event_pending=true;model_unlock(app);wake_worker();
    }
    if(!faulted && ready && visible && app->built && navigation_stage!=NAV_REQUESTED && navigation_stage!=NAV_DETACHED){
        for(unsigned i=0;i<app->node_count;++i){node_t *node=&app->nodes[i];node_model_t model;model_lock(app);model=node->model;model_unlock(app);
            if(node->shown_text!=model.text_revision){show_text(app,node,&model);node->shown_text=model.text_revision;}
            if(node->shown_image!=model.image_revision){lv_image_set_src(node->object,find_image(app,model.image));node->shown_image=model.image_revision;}
            if(node->shown_value!=model.value_revision){show_value(app,node,&model);node->shown_value=model.value_revision;}
            if(theme_changed || snapshot.revisions[2]!=app->applied_display_revision || node->shown_style!=model.style_revision){apply_style(app,node,&model);node->shown_style=model.style_revision;}
            model_lock(app);bool focus_allowed=node_visible_locked(app,i,true);model_unlock(app);
            if(node->shown_focus!=model.focus_revision){if(model.focus && focus_allowed && !ouse_ime_focused(node->object)){
                    lv_obj_t *previous=NULL;for(unsigned n=0;n<app->node_count;++n)if(app->nodes[n].model.type==TEXT_INPUT && ouse_ime_focused(app->nodes[n].object))previous=app->nodes[n].object;
                    ouse_ime_focus(node->object);
                    if(ouse_ime_focused(node->object)){if(previous)lv_obj_send_event(previous,LV_EVENT_DEFOCUSED,NULL);lv_obj_send_event(node->object,LV_EVENT_FOCUSED,NULL);}
                }else if(!model.focus && ouse_ime_blur(node->object))lv_obj_send_event(node->object,LV_EVENT_DEFOCUSED,NULL);
                node->shown_focus=model.focus_revision;}
        }
        app->applied_theme_revision=snapshot.revisions[0];app->applied_display_revision=snapshot.revisions[2];
    }
    if(app->built && visible && !faulted)sync_modal_visibility(app);
    if(home && visible)(void)ouse_app_background();
}
// Worker functions below never enter LVGL or synchronously request the UI task.
static void lifecycle(instance_t *app,const char *name)
{
    if(!app->context || app->faulted)return;
    begin_call(app);JSValue global=JS_GetGlobalObject(app->context),object=JS_GetPropertyStr(app->context,global,"app");
    if(JS_IsException(object)){JS_FreeValue(app->context,global);exception(app);return;}
    if(JS_IsObject(object)){
        JSValue handler=JS_GetPropertyStr(app->context,object,name);
        if(JS_IsException(handler))exception(app);
        else if(JS_IsFunction(app->context,handler))result_ok(app,call_js(app,"lifecycle",name,handler,object,0,NULL));
        JS_FreeValue(app->context,handler);
    }
    JS_FreeValue(app->context,object);JS_FreeValue(app->context,global);
    model_lock(app);app->home_requested=false;model_unlock(app);
}
static void destroy_js(instance_t *app)
{
    if(app->context){
        cancel_storage_jobs(app);
        cancel_net_jobs(app);
        for(unsigned i=0;i<MAX_NODES;++i)for(unsigned e=0;e<EVENT_COUNT;++e)JS_FreeValue(app->context,app->nodes[i].handlers[e]);
        for(unsigned i=0;i<MAX_TIMERS;++i)JS_FreeValue(app->context,app->timers[i].handler);
        for(unsigned i=0;i<MAX_JOBS;++i){JS_FreeValue(app->context,app->rejected_promises[i]);JS_FreeValue(app->context,app->rejection_reasons[i]);}
        JS_FreeValue(app->context,app->translations);JS_FreeValue(app->context,app->lifecycle_handler);JS_FreeValue(app->context,app->notification_handler);
        JS_FreeValue(app->context,app->navigation_handler);JS_FreeValue(app->context,app->task_handler);
        for(unsigned i=0;i<MAX_PAGES;++i)JS_FreeValue(app->context,app->page_roots[i]);
        for(unsigned i=0;i<6;++i)JS_FreeValue(app->context,app->changes[i]);
        JS_FreeContext(app->context);app->context=NULL;
    }
    if(app->runtime){JS_FreeRuntime(app->runtime);app->runtime=NULL;}
}
static esp_err_t initialize_js(instance_t *app)
{
    int64_t startup_started=esp_timer_get_time();
    app->translations=app->lifecycle_handler=app->notification_handler=app->navigation_handler=app->task_handler=JS_UNDEFINED;
    for(unsigned i=0;i<MAX_PAGES;++i)app->page_roots[i]=JS_UNDEFINED;
    for(unsigned i=0;i<6;++i){app->changes[i]=JS_UNDEFINED;app->revisions[i]=group_revision(app,i);}
    for(unsigned i=0;i<MAX_JOBS;++i)app->rejected_promises[i]=app->rejection_reasons[i]=JS_UNDEFINED;
    for(unsigned i=0;i<MAX_NODES;++i)for(unsigned e=0;e<EVENT_COUNT;++e)app->nodes[i].handlers[e]=JS_UNDEFINED;
    for(unsigned i=0;i<MAX_TIMERS;++i)app->timers[i].handler=JS_UNDEFINED;
    app->runtime=JS_NewRuntime();if(!app->runtime)return ESP_ERR_NO_MEM;
    JS_SetMemoryLimit(app->runtime,OUSE_RUNTIME_MEMORY_LIMIT);JS_SetMaxStackSize(app->runtime,JS_STACK_BYTES);
    JS_SetInterruptHandler(app->runtime,interrupt,app);JS_SetHostPromiseRejectionTracker(app->runtime,promise_rejected,app);
    app->worker_visible=true;begin_startup(app);app->context=JS_NewContext(app->runtime);if(!app->context)return ESP_ERR_NO_MEM;
    JS_SetContextOpaque(app->context,app);
    JSValue document=JS_ParseJSON(app->context,app->page,strlen(app->page),"page.json");if(JS_IsException(document))return ESP_ERR_INVALID_ARG;
    char unit[8]="";int api=0;app->parsing_page=true;
    bool valid=string_property(app,document,"units",unit,sizeof(unit),false) && (!unit[0] || !strcmp(unit,"dp") || !strcmp(unit,"px"));app->dp_units=!strcmp(unit,"dp");
    app->translations=source_property(app,document,"translations");
    if(!JS_IsObject(app->translations)){JS_FreeValue(app->context,app->translations);app->translations=JS_NewObject(app->context);}
    valid=valid && integer_property(app,document,"api",&api,1,1) && valid_translations(app) && parse_pages(app,document);
    JS_FreeValue(app->context,document);app->parsing_page=false;if(!valid)return ESP_ERR_INVALID_ARG;
    if(app->validation_only){begin_startup(app);JSValue value=JS_Eval(app->context,app->script,strlen(app->script),"app.js",JS_EVAL_TYPE_GLOBAL|JS_EVAL_FLAG_COMPILE_ONLY);valid=!JS_IsException(value);JS_FreeValue(app->context,value);return valid ? ESP_OK : ESP_ERR_INVALID_ARG;}
    install_api(app);begin_startup(app);
    if(!result_ok(app,JS_Eval(app->context,app->script,strlen(app->script),"app.js",JS_EVAL_TYPE_GLOBAL)))return ESP_FAIL;
    model_lock(app);
    if(!app->visible)for(unsigned i=0;i<app->node_count;++i)if(modal_widget(app->nodes[i].model.type))set_node_hidden_locked(app,&app->nodes[i],true);
    app->ready=true;model_unlock(app);
    ESP_LOGI("OuseJS","%s ready: widgets=%u; init=%lld us; shared script worker",app->id,app->node_count,(long long)(esp_timer_get_time()-startup_started));return ESP_OK;
}
static void set_timer_clock(instance_t *app,bool running)
{
    if(app->timer_clock_frozen==!running)return;
    uint32_t now=now_ms();
    for(unsigned i=0;i<MAX_TIMERS;++i){
        script_timer_t *timer=&app->timers[i];if(!timer->id)continue;
        if(running)timer->due=now+timer->remaining;
        else {int32_t remaining=(int32_t)(timer->due-now);timer->remaining=remaining>0 ? (uint32_t)remaining : 0;}
    }
    app->timer_clock_frozen=!running;
}
// Reconcile again after JS hooks: those hooks may begin/end background work.
// policy_dirty guarantees one later notification turn even when JS just became
// suspended, without recursively invoking the observer in its own callback.
static bool reconcile_policy(instance_t *app)
{
    model_lock(app);bool visible=app->visible,active=app->background_active,closed=app->closed,faulted=app->faulted;uint32_t revision=app->lifecycle_revision;model_unlock(app);
    bool runnable=!closed && !faulted && (visible || active);
    set_timer_clock(app,runnable);app->worker_runnable=runnable;app->worker_background=active;
    app->policy_dirty=revision!=app->seen_lifecycle || visible!=app->worker_visible;
    return !closed;
}
static bool update_policy(instance_t *app)
{
    model_lock(app);bool visible=app->visible,closed=app->closed;uint32_t revision=app->lifecycle_revision;
    app->worker_ui=app->snapshot;model_unlock(app);
    if(closed)return false;
    bool visibility_changed=!app->policy_known || visible!=app->worker_visible;
    bool old_runnable=app->worker_runnable,old_background=app->worker_background;
    // Existing timers thaw before onResume creates any new ones. Timers that
    // are created while genuinely suspended retain their full requested delay.
    if(app->timer_clock_frozen && visible)set_timer_clock(app,true);
    app->worker_visible=visible;
    if(app->policy_known && visibility_changed)lifecycle(app,visible ? "onResume" : "onPause");
    if(!reconcile_policy(app))return false;
    model_lock(app);revision=app->lifecycle_revision;bool faulted=app->faulted;model_unlock(app);
    bool changed=!app->policy_known || visibility_changed || app->worker_runnable!=old_runnable || app->worker_background!=old_background || revision!=app->seen_lifecycle;
    app->seen_lifecycle=revision;app->policy_known=true;app->policy_dirty=false;
    if(changed && !faulted && JS_IsFunction(app->context,app->lifecycle_handler)){
        begin_call(app);JSValue handler=JS_DupValue(app->context,app->lifecycle_handler),value=lifecycle_snapshot(app);
        JSValue result=call_js(app,"lifecycle","onChange",handler,JS_UNDEFINED,1,&value);JS_FreeValue(app->context,handler);JS_FreeValue(app->context,value);result_ok(app,result);
    }
    return reconcile_policy(app);
}
static void run_batch(instance_t *app)
{
    if(!app->worker_runnable || app->faulted || navigation_pending(app))return;
    begin_call(app);
    navigation_changed(app); // bind the new page before deferred asynchronous work
    if(app->faulted || navigation_pending(app))return;
    if(!result_ok(app,JS_UNDEFINED))return; // resume last slice's pending Promise jobs
    if(ouse_budget_slice_done(&app->budget) || !reconcile_policy(app) || !app->worker_runnable || navigation_pending(app))return;
    preference_notifications(app);notification_open_events(app);process_storage_results(app);process_net_results(app);task_completion_events(app);
    if(!reconcile_policy(app) || !app->worker_runnable || navigation_pending(app))return;
    input_event_t input;unsigned processed=0;
    while(!app->faulted && processed++<4 && !ouse_budget_slice_done(&app->budget) && xQueueReceive(app->events,&input,0)==pdTRUE){
        model_lock(app);bool valid=app->visible && input.generation==app->generation;model_unlock(app);
        if(!valid || input.node>=app->node_count || input.event>=EVENT_COUNT)continue;
        node_t *node=&app->nodes[input.node];JSValue handler=JS_DupValue(app->context,node->handlers[input.event]);
        if(JS_IsFunction(app->context,handler)){
            JSValue argument=JS_UNDEFINED;
            if(input.event==EVENT_CHANGE){node_type_t type=node->model.type;argument=type==TEXT_INPUT ? JS_NewString(app->context,input.text) : type==SWITCH || type==CHECKBOX ? JS_NewBool(app->context,input.value) : JS_NewInt32(app->context,input.value);}
            else if((input.event==EVENT_SUBMIT && node->model.type==TEXT_INPUT) || input.event==EVENT_DISMISS)argument=JS_NewString(app->context,input.text);
            if(ouse_budget_slice_done(&app->budget)){
                if(xQueueSendToFront(app->events,&input,0)!=pdTRUE){model_lock(app);++app->dropped_events;model_unlock(app);}
                JS_FreeValue(app->context,argument);JS_FreeValue(app->context,handler);break;
            }
            JSValue result=call_js(app,"input",node->model.id,handler,JS_UNDEFINED,1,&argument);JS_FreeValue(app->context,argument);result_ok(app,result);
        }
        JS_FreeValue(app->context,handler);
        if(!reconcile_policy(app) || !app->worker_runnable || navigation_pending(app))return;
    }
    uint32_t now=now_ms();
    for(unsigned i=0;i<MAX_TIMERS && !app->faulted;++i){
        script_timer_t *job=&app->timers[i];if(!job->id || (int32_t)(now-job->due)<0)continue;
        if(ouse_budget_slice_done(&app->budget))break; // defer pending callbacks instead of punishing an idle batch
        JSValue handler=JS_DupValue(app->context,job->handler);
        if(job->interval)job->due=now+job->period;else {JS_FreeValue(app->context,job->handler);job->handler=JS_UNDEFINED;job->id=0;}
        JSValue result=call_js(app,"timer",NULL,handler,JS_UNDEFINED,0,NULL);JS_FreeValue(app->context,handler);result_ok(app,result);
        if(!reconcile_policy(app) || !app->worker_runnable || navigation_pending(app))return;
    }
}
static void publish_stats(void)
{
    int64_t now=esp_timer_get_time();
    ouse_runtime_stats_t stats={.errors=error_count,.worker_stack_min=uxTaskGetStackHighWaterMark(NULL),.storage_jobs=storage_job_count};
    ouse_runtime_diagnostics_t diagnostics={.sampled_ms=now/1000};
    for(instance_t *app=instances;app;app=app->next){
        ++stats.instances;stats.widgets+=app->node_count;if(app->context)++stats.contexts;
        for(unsigned i=0;i<MAX_STORAGE_PENDING;++i)if(app->storage[i])++stats.storage_pending;
        unsigned timers=0;for(unsigned i=0;i<MAX_TIMERS;++i)if(app->timers[i].id)++timers;
        stats.timers+=timers;
        if(app->runtime && (!app->sampled_memory_us || now-app->sampled_memory_us>=1000000)){
            JSMemoryUsage memory;JS_ComputeMemoryUsage(app->runtime,&memory);app->sampled_js_bytes=(size_t)memory.malloc_size;app->sampled_memory_us=now;
        }
        stats.js_heap_bytes+=app->sampled_js_bytes;
        if(app->worker_runnable)++stats.runnable;
        if(app->worker_runnable && !app->worker_visible)++stats.background;
        unsigned queued=uxQueueMessagesWaiting(app->events);stats.queued_events+=queued;
        model_lock(app);stats.dropped_events+=app->dropped_events;
        if(diagnostics.app_count<OUSE_RUNTIME_DIAGNOSTIC_APPS){
            ouse_runtime_app_stats_t *entry=&diagnostics.apps[diagnostics.app_count++];
            strcpy(entry->app_id,app->id);entry->instance_id=app->owner.instance;
            entry->visible=app->visible;entry->runnable=app->worker_runnable;entry->background_allowed=app->background_allowed;
            entry->background_active=app->background_active;entry->faulted=app->faulted;
            if(app->background_active){strcpy(entry->background_reason,app->background_reason);entry->background_since_ms=app->background_since_us/1000;}
            entry->timers=timers;entry->queued_events=queued;entry->js_heap_bytes=app->sampled_js_bytes;
            entry->memory_sample_ms=app->sampled_memory_us/1000;
            entry->native_known_bytes=sizeof(*app)+strlen(app->page)+1+strlen(app->script)+1;
            for(unsigned i=0;i<app->image_count;++i)entry->native_known_bytes+=app->image_descriptors[i].data_size;
            for(unsigned i=0;i<app->asset_count;++i)entry->native_known_bytes+=app->assets[i].size+(app->assets[i].type==OUSE_SCRIPT_ASSET_TEXT ? 1 : 0);
            entry->last_scheduled_us=app->budget.scheduled_us;entry->last_elapsed_us=app->budget.observed_us-app->budget.started_us;
            entry->budget_checkpoints=app->budget.samples;entry->wall_guard_hit=app->budget.wall_exceeded;
        }
        model_unlock(app);
    }
    portENTER_CRITICAL(&stats_lock);runtime_stats=stats;runtime_diagnostics=diagnostics;portEXIT_CRITICAL(&stats_lock);
}
static void script_worker(void *opaque)
{
    (void)opaque;bool runnable=false;
    for(;;){
        (void)ulTaskNotifyTake(pdTRUE,runnable ? pdMS_TO_TICKS(20) : portMAX_DELAY);
        drain_storage_completions();
        drain_net_completions();
        instance_t *launch=NULL;
        if(xQueueReceive(launches,&launch,0)==pdTRUE){
            model_lock(launch);launch->worker_ui=launch->snapshot;bool closed=launch->closed;model_unlock(launch);
            esp_err_t error=closed ? ESP_ERR_INVALID_STATE : initialize_js(launch);
            if(launch->validation_only){launch->validation_result=error;destroy_js(launch);xSemaphoreGive(launch->validated);release_instance(launch);}
            else if(closed){destroy_js(launch);release_instance(launch);}
            else {if(error!=ESP_OK && !launch->faulted)fault(launch,error==ESP_ERR_NO_MEM ? "JavaScript memory unavailable" : "Invalid page.json or app.js");launch->next=instances;instances=launch;}
        }
        runnable=false;instance_t **link=&instances;
        while(*link){instance_t *app=*link;
            if(!navigation_worker(app)){runnable=true;link=&app->next;continue;}
            if(!update_policy(app)){
                if(app->worker_visible)lifecycle(app,"onPause");
                lifecycle(app,"onClose");*link=app->next;destroy_js(app);ESP_LOGI("OuseJS","%s closed: worker context released",app->id);release_instance(app);continue;
            }
            link=&app->next;
        }
        // Foreground always receives the first execution slice. Each retained
        // context receives at most one bounded background slice per turn.
        for(unsigned pass=0;pass<2;++pass)for(instance_t *app=instances;app;app=app->next){
            if(app->worker_visible!=(pass==0))continue;
            run_batch(app);if((app->worker_runnable || app->policy_dirty) && !app->faulted){runnable=true;vTaskDelay(1);}
        }
        publish_stats();if(uxQueueMessagesWaiting(launches))xTaskNotifyGive(worker_task);
    }
}
static esp_err_t ensure_worker(void)
{
    // Called only by UI-task entry points. Lifetime is the firmware lifetime.
    if(worker_task)return ESP_OK;
    ouse_tasks_set_notify(wake_worker);
    launches=xQueueCreate(12,sizeof(instance_t *));if(!launches)return ESP_ERR_NO_MEM;
    storage_completions=xQueueCreate(MAX_STORAGE_JOBS,sizeof(storage_job_t *));if(!storage_completions){vQueueDelete(launches);launches=NULL;return ESP_ERR_NO_MEM;}
    net_completions=xQueueCreate(MAX_NET_JOBS,sizeof(net_job_t *));
    if(!net_completions || xTaskCreate(script_worker,"ouse_js",WORKER_STACK_BYTES,NULL,3,&worker_task)!=pdPASS){
        if(net_completions)vQueueDelete(net_completions);
        net_completions=NULL;vQueueDelete(storage_completions);storage_completions=NULL;vQueueDelete(launches);launches=NULL;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
static bool valid_asset_id(const char *id)
{
    if(!id || !id[0] || strlen(id)>31)return false;
    for(unsigned i=0;id[i];++i){char c=id[i];bool alpha=(c>='a' && c<='z') || (c>='A' && c<='Z');
        if(!alpha && (!i || !((c>='0' && c<='9') || c=='_' || c=='-')))return false;
    }
    return true;
}
static bool valid_package_resources(const ouse_script_package_t *package)
{
    if(package->image_count>OUSE_RUNTIME_ASSETS_MAX || package->asset_count>OUSE_RUNTIME_ASSETS_MAX ||
        package->image_count+package->asset_count>OUSE_RUNTIME_ASSETS_MAX ||
        (package->image_count && !package->images) || (package->asset_count && !package->assets))return false;
    size_t images=0,data=0;
    for(unsigned i=0;i<package->image_count;++i){const ouse_script_image_t *source=&package->images[i];const lv_image_dsc_t *image=source->image;
        if(!valid_asset_id(source->id) || !image || !image->data || image->header.magic!=LV_IMAGE_HEADER_MAGIC || image->header.cf!=LV_COLOR_FORMAT_ARGB8888 ||
            !image->header.w || !image->header.h || image->header.w>512 || image->header.h>512 || image->header.stride<image->header.w*4 ||
            image->data_size!=(size_t)image->header.stride*image->header.h || image->data_size>OUSE_RUNTIME_IMAGE_BYTES_MAX-images)return false;
        images+=image->data_size;
        for(unsigned j=0;j<i;++j)if(!strcmp(source->id,package->images[j].id))return false;
    }
    for(unsigned i=0;i<package->asset_count;++i){const ouse_script_asset_t *asset=&package->assets[i];
        if(!valid_asset_id(asset->id) || (asset->size && !asset->data) ||
            (asset->type!=OUSE_SCRIPT_ASSET_TEXT && asset->type!=OUSE_SCRIPT_ASSET_BINARY) ||
            asset->size>(asset->type==OUSE_SCRIPT_ASSET_TEXT ? 16*1024u : 64*1024u) || asset->size>OUSE_RUNTIME_DATA_BYTES_MAX-data ||
            (asset->type==OUSE_SCRIPT_ASSET_TEXT && asset->size && memchr(asset->data,0,asset->size)))return false;
        data+=asset->size;
        for(unsigned j=0;j<package->image_count;++j)if(!strcmp(asset->id,package->images[j].id))return false;
        for(unsigned j=0;j<i;++j)if(!strcmp(asset->id,package->assets[j].id))return false;
    }
    return true;
}
static void *copy_resource(const void *data,size_t size,bool text)
{
    size_t bytes=size+(text || !size ? 1 : 0);
    unsigned char *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!copy)copy=malloc(bytes);
    if(!copy)return NULL;
    if(size)memcpy(copy,data,size);
    if(text || !size)copy[size]=0;
    return copy;
}
static instance_t *allocate_instance(const ouse_script_package_t *package)
{
    instance_t *app=heap_caps_calloc(1,sizeof(*app),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!app)app=calloc(1,sizeof(*app));
    if(!app)return NULL;
    atomic_init(&app->references,1);
    if(ouse_task_owner_open(package->id,&app->owner)!=ESP_OK){free(app);return NULL;}
    app->model_lock=xSemaphoreCreateMutex();app->events=xQueueCreate(MAX_EVENTS,sizeof(input_event_t));
    app->page=strdup(package->page);app->script=strdup(package->script);strcpy(app->id,package->id);app->background_allowed=package->background_allowed;app->notifications_allowed=package->notifications_allowed;
    app->network_allowed=package->network_allowed;app->media_allowed=package->media_allowed;app->alarms_allowed=package->alarms_allowed;
    if(!app->model_lock || !app->events || !app->page || !app->script){release_instance(app);return NULL;}
    size_t bytes=0;
    for(unsigned i=0;i<package->image_count;++i){const ouse_script_image_t *source=&package->images[i];
        if(!source->id || strlen(source->id)>=sizeof(app->image_ids[i]) || !source->image || !source->image->data || source->image->data_size>MAX_ASSET_BYTES-bytes){release_instance(app);return NULL;}
        void *data=copy_resource(source->image->data,source->image->data_size,false);if(!data){release_instance(app);return NULL;}bytes+=source->image->data_size;
        strcpy(app->image_ids[i],source->id);app->image_descriptors[i]=*source->image;app->image_descriptors[i].data=data;app->image_descriptors[i].reserved=NULL;app->image_descriptors[i].reserved_2=NULL;
        app->images[i]=(ouse_script_image_t){.id=app->image_ids[i],.image=&app->image_descriptors[i]};++app->image_count;
    }
    for(unsigned i=0;i<package->asset_count;++i){const ouse_script_asset_t *source=&package->assets[i];
        void *data=copy_resource(source->data,source->size,source->type==OUSE_SCRIPT_ASSET_TEXT);if(!data){release_instance(app);return NULL;}
        strcpy(app->asset_ids[i],source->id);app->assets[i]=(ouse_script_asset_t){.id=app->asset_ids[i],.type=source->type,.data=data,.size=source->size};++app->asset_count;
    }
    capture_ui(&app->snapshot);app->core_display_revision=app->snapshot.revisions[2];app->display_revision=1;app->snapshot.revisions[2]=app->display_revision;return app;
}
static bool enqueue_instance(instance_t *app)
{
    atomic_fetch_add(&app->references,1);
    if(xQueueSend(launches,&app,0)!=pdTRUE){release_instance(app);return false;}
    wake_worker();return true;
}
static esp_err_t open_app(lv_obj_t *root,void *opaque)
{
    ouse_script_app_t *state=opaque;esp_err_t error=ensure_worker();if(error!=ESP_OK)return error;
    instance_t *app=allocate_instance(state->package);if(!app)return ESP_ERR_NO_MEM;
    state->instance=app;app->root=root;app->visible=true;app->generation=1;
    app->viewport=lv_obj_create(root);lv_obj_remove_style_all(app->viewport);lv_obj_set_size(app->viewport,LV_PCT(100),LV_PCT(100));lv_obj_remove_flag(app->viewport,LV_OBJ_FLAG_SCROLLABLE);
    app->pump=lv_timer_create(ui_pump,20,app);if(!app->pump)return ESP_ERR_NO_MEM;
    if(!enqueue_instance(app))return ESP_ERR_NO_MEM;
    return ESP_OK;
}
static bool back_app(void *opaque)
{
    instance_t *app=((ouse_script_app_t *)opaque)->instance;if(!app)return false;
    model_lock(app);bool faulted=app->faulted,closed=app->closed;model_unlock(app);if(faulted || closed)return false;
    if(navigation_pending(app))return true;
    if(!app->built)return false;
    for(unsigned i=app->node_count;i>0;--i)if(app->nodes[i-1].overlay && !lv_obj_has_flag(app->nodes[i-1].overlay,LV_OBJ_FLAG_HIDDEN))
        return dismiss_modal(app,i-1,"back");
    return request_navigation(app,NAV_BACK,0);
}
static void pause_app(void *opaque)
{
    instance_t *app=((ouse_script_app_t *)opaque)->instance;if(!app)return;
    model_lock(app);app->visible=false;app->home_requested=false;++app->generation;++app->lifecycle_revision;
    if(app->ready)for(unsigned i=0;i<app->node_count;++i)if(modal_widget(app->nodes[i].model.type))set_node_hidden_locked(app,&app->nodes[i],true);
    model_unlock(app);
    if(app->built){
        // LVGL reparents an expanded dropdown list to the screen. It must be
        // closed explicitly before hiding its application root.
        for(unsigned i=0;i<app->node_count;++i)if(app->nodes[i].model.type==DROPDOWN && app->nodes[i].object)lv_dropdown_close(app->nodes[i].object);
        sync_modal_visibility(app);
    }
    ouse_ime_hide_root(app->root);wake_worker();
}
static void resume_app(void *opaque)
{
    instance_t *app=((ouse_script_app_t *)opaque)->instance;if(!app)return;
    model_lock(app);app->visible=true;++app->generation;++app->lifecycle_revision;model_unlock(app);wake_worker();
}
static void close_app(void *opaque)
{
    ouse_script_app_t *state=opaque;instance_t *app=state->instance;if(!app)return;
    state->instance=NULL;model_lock(app);app->closed=true;app->visible=false;app->background_active=false;++app->generation;model_unlock(app);
    ouse_task_owner_close(&app->owner);
    if(app->pump){lv_timer_delete(app->pump);app->pump=NULL;}
    ouse_ime_hide_root(app->root);
    if(app->built)for(unsigned i=0;i<app->node_count;++i){node_t *node=&app->nodes[i];if(node->object){lv_obj_remove_event_cb(node->object,input_event);if(node->model.type==DROPDOWN)lv_dropdown_close(node->object);if(node->model.type==TEXT_INPUT)ouse_ime_detach(node->object);node->object=node->caption=node->subtitle=node->overlay=NULL;}}
    // Objects and decoder cache must release copied image descriptors before
    // the UI reference disappears; the manager deletes the now-empty root next.
    if(app->viewport)lv_obj_clean(app->viewport);
    for(unsigned i=0;i<app->image_count;++i)lv_image_cache_drop(&app->image_descriptors[i]);
    app->root=app->viewport=NULL;wake_worker();release_instance(app);
}
esp_err_t ouse_script_prepare(ouse_script_app_t *state,const ouse_script_package_t *package,ouse_app_definition_t *definition)
{
    if(!state || !package || !definition || state->instance || !package->page || !package->script ||
        !package->id || !*package->id || strlen(package->id)>47 || !package->name || !*package->name || strlen(package->name)>47 ||
        !package->icon || !*package->icon || strlen(package->icon)>15 ||
        (package->name_zh && strlen(package->name_zh)>47) || (package->name_en && strlen(package->name_en)>47) ||
        package->api!=OUSE_RUNTIME_API_VERSION || strlen(package->page)>MAX_SOURCE || strlen(package->script)>MAX_SOURCE ||
        !valid_package_resources(package))return ESP_ERR_INVALID_ARG;
    state->package=package;
    *definition=(ouse_app_definition_t){.id=package->id,.name=package->name,.icon=package->icon,.open=open_app,.close=close_app,.pause=pause_app,.resume=resume_app,.back=back_app,.context=state,
        .name_zh=package->name_zh,.name_en=package->name_en,.icon_image=package->icon_image};return ESP_OK;
}
void ouse_runtime_get_stats(ouse_runtime_stats_t *stats)
{
    if(!stats)return;
    portENTER_CRITICAL(&stats_lock);*stats=runtime_stats;portEXIT_CRITICAL(&stats_lock);
}
esp_err_t ouse_script_validate_begin(const ouse_script_package_t *package,ouse_script_validation_t **out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    *out=NULL;
    ouse_script_app_t state={0};ouse_app_definition_t definition;esp_err_t error=ouse_script_prepare(&state,package,&definition);if(error!=ESP_OK)return error;
    error=ensure_worker();if(error!=ESP_OK)return error;
    instance_t *app=allocate_instance(package);if(!app)return ESP_ERR_NO_MEM;
    app->validation_only=true;app->validated=xSemaphoreCreateBinary();if(!app->validated){release_instance(app);return ESP_ERR_NO_MEM;}
    if(!enqueue_instance(app)){release_instance(app);return ESP_ERR_NO_MEM;}
    *out=(ouse_script_validation_t *)app;return ESP_OK;
}
esp_err_t ouse_script_validate_wait(ouse_script_validation_t *validation)
{
    if(!validation)return ESP_ERR_INVALID_ARG;
    instance_t *app=(instance_t *)validation;
    // The installer waits outside the UI callback/lock. Compilation retains its
    // own copies and reference even if this native caller reaches its timeout.
    esp_err_t error=xSemaphoreTake(app->validated,pdMS_TO_TICKS(2000))==pdTRUE ? app->validation_result : ESP_ERR_TIMEOUT;
    release_instance(app);return error;
}

void ouse_runtime_get_diagnostics(ouse_runtime_diagnostics_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&stats_lock);*out=runtime_diagnostics;portEXIT_CRITICAL(&stats_lock);
}
