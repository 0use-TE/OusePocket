// SPDX-License-Identifier: MIT
// Included by the runtime: JS conversion only, no hardware I/O or heap walks.
static JSValue diagnostic_app(JSContext *context,const ouse_runtime_app_stats_t *app)
{
    JSValue object=JS_NewObject(context);
#define DN(name,value) JS_SetPropertyStr(context,object,name,JS_NewFloat64(context,(double)(value)))
#define DB(name,value) JS_SetPropertyStr(context,object,name,JS_NewBool(context,value))
    JS_SetPropertyStr(context,object,"appId",JS_NewString(context,app->app_id));
    JS_SetPropertyStr(context,object,"backgroundReason",JS_NewString(context,app->background_reason));
    DN("instanceId",app->instance_id);DB("visible",app->visible);DB("runnable",app->runnable);
    DB("backgroundAllowed",app->background_allowed);DB("backgroundActive",app->background_active);DB("faulted",app->faulted);
    DN("timers",app->timers);DN("queuedEvents",app->queued_events);DN("jsHeapBytes",app->js_heap_bytes);
    DN("nativeKnownBytes",app->native_known_bytes);DN("memorySampleMs",app->memory_sample_ms);
    DN("backgroundSinceMs",app->background_since_ms);DN("lastScheduledUs",app->last_scheduled_us);
    DN("lastElapsedUs",app->last_elapsed_us);DN("budgetCheckpoints",app->budget_checkpoints);DB("wallGuardHit",app->wall_guard_hit);
#undef DN
#undef DB
    return object;
}
static JSValue diagnostics_api(JSContext *context,JSValueConst self,int argc,JSValueConst *argv,int system)
{
    (void)self;(void)argc;(void)argv;
    ouse_runtime_diagnostics_t cache;ouse_runtime_get_diagnostics(&cache);
    if(!system){
        uint32_t instance=from_js(context)->owner.instance;
        for(unsigned i=0;i<cache.app_count;++i)if(cache.apps[i].instance_id==instance)return diagnostic_app(context,&cache.apps[i]);
        return JS_NULL; // first worker snapshot has not yet been published
    }
    JSValue object=JS_NewObject(context),apps=JS_NewArray(context);
    for(unsigned i=0;i<cache.app_count;++i)JS_SetPropertyUint32(context,apps,i,diagnostic_app(context,&cache.apps[i]));
    JS_SetPropertyStr(context,object,"apps",apps);
    JS_SetPropertyStr(context,object,"sampledMs",JS_NewFloat64(context,(double)cache.sampled_ms));
    JS_SetPropertyStr(context,object,"budgetClock",JS_NewString(context,ouse_budget_clock_name()));
    ouse_runtime_stats_t runtime;ouse_runtime_get_stats(&runtime);
    ouse_task_stats_t tasks;ouse_tasks_get_stats(&tasks);
#define SN(name,value) JS_SetPropertyStr(context,object,name,JS_NewFloat64(context,(double)(value)))
    SN("instances",runtime.instances);SN("contexts",runtime.contexts);SN("jsHeapBytes",runtime.js_heap_bytes);
    SN("backgroundJs",runtime.background);SN("runnableJs",runtime.runnable);SN("timers",runtime.timers);
    SN("queuedEvents",runtime.queued_events);SN("droppedEvents",runtime.dropped_events);SN("errors",runtime.errors);
    SN("jsWorkerStackMinBytes",runtime.worker_stack_min);SN("storageJobs",runtime.storage_jobs);
    SN("networkJobs",net_job_count);SN("networkJobsLimit",MAX_NET_JOBS);
    ouse_alarm_stats_t alarms;ouse_alarm_get_stats(&alarms);
    SN("alarms",alarms.count);SN("alarmsRinging",alarms.ringing);SN("alarmServiceError",alarms.error);
    JS_SetPropertyStr(context,object,"alarmServiceReady",JS_NewBool(context,alarms.ready));
    JS_SetPropertyStr(context,object,"clockValid",JS_NewBool(context,alarms.clock_valid));
    ouse_media_state_t media={0};bool media_known=ouse_media_get(NULL,&media);
    JS_SetPropertyStr(context,object,"mediaActive",JS_NewBool(context,media_known && media.active));
    SN("mediaPcmBufferBytes",OUSE_MEDIA_BUFFER_BYTES);SN("mediaBufferedBytes",media.buffered_bytes);
    SN("taskRecords",tasks.records);SN("nativeTasksActive",tasks.active);SN("taskEventsPending",tasks.pending_delivery);
    SN("taskResultsPending",tasks.pending_results);
    SN("taskCapacity",OUSE_TASK_CAPACITY);SN("taskActiveLimit",OUSE_TASK_ACTIVE_LIMIT);SN("taskOwnerLimit",OUSE_TASK_OWNER_LIMIT);
    SN("taskOwners",tasks.owners);SN("taskRejected",tasks.rejected);SN("taskRecycled",tasks.recycled);SN("taskRegistryBytes",tasks.registry_bytes);
    SN("memorySampleIntervalMs",1000);SN("foregroundBudgetUs",CALL_BUDGET_US);SN("backgroundBudgetUs",BACKGROUND_BUDGET_US);
#undef SN
    return object;
}
