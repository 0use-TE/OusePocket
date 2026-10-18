static struct {
    FILE *file;
    bool leased;
    unsigned char *input;
    size_t capacity;
    ouse_avi_info_t info;
    ouse_task_owner_t owner;
    int64_t next;
    uint32_t task;
} playback;
static void playback_stop(void)
{
    if(playback.file)fclose(playback.file);
    if(playback.leased)ouse_files_lease_end();
    free(playback.input);
    lock();client_t *client=find(&playback.owner);if(client){client->state.playing=false;client->play_wanted=false;}playback_active=false;unlock();
    if(playback.task)ouse_task_finish(playback.task,ESP_OK,ouse_task_cancel_requested(playback.task));
    memset(&playback,0,sizeof(playback));
}
static esp_err_t playback_start(const job_t *job)
{
    char absolute[180];if(!video_path(job->path,absolute))return ESP_ERR_INVALID_ARG;
    playback.leased=ouse_files_lease_begin();if(!playback.leased)return ESP_ERR_INVALID_STATE;
    playback.owner=job->owner;playback.file=fopen(absolute,"rb");
    jpeg_decode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_DEC_ALLOC_INPUT_BUFFER};
    playback.input=jpeg_alloc_decoder_mem(PHOTO_LIMIT,&memory,&playback.capacity);
    esp_err_t error=playback.input ? ESP_OK : ESP_ERR_NO_MEM;
    if(error==ESP_OK && (!playback.file || !ouse_avi_read_info(playback.file,&playback.info)))error=ESP_FAIL;
    if(error==ESP_OK)error=ouse_task_create(&job->owner,OUSE_TASK_CAMERA,"playback",OUSE_TASK_STOP_ON_CLOSE,true,&playback.task);
    if(error==ESP_OK && !ouse_task_start(playback.task,true))error=ESP_ERR_INVALID_STATE;
    if(error!=ESP_OK){playback_stop();return error;}
    lock();client_t *client=find(&job->owner);if(client){client->state.playing=true;client->play_wanted=true;}playback_active=true;unlock();return ESP_OK;
}
static void playback_pump(const ouse_power_state_t *power)
{
    if(!playback.file)return;
    lock();client_t *client=find(&playback.owner);bool visible=client && !client->closed && client->play_wanted;unlock();
    if(!visible || !sd_ready() || power->asleep || ouse_tasks_quiescing() || ouse_task_cancel_requested(playback.task)){playback_stop();return;}
    int64_t now=esp_timer_get_time();if(now<playback.next)return;
    if(ftell(playback.file)>=(long)playback.info.end){playback_stop();return;}
    uint32_t length=0;esp_err_t error=ouse_avi_read_frame(playback.file,&playback.info,playback.input,playback.capacity,&length) ? decode_frame(&playback.owner,1,playback.input,length) : ESP_FAIL;
    if(error!=ESP_OK){lock();client=find(&playback.owner);if(client)client->state.error=error;unlock();playback_stop();return;}
    playback.next=now+playback.info.interval_us;
}
