#define RECORD_FRAMES 18000
static struct {
    FILE *file;
    bool leased;
    ouse_avi_writer_t avi;
    ouse_avi_index_t *index;
    jpeg_encoder_handle_t encoder;
    unsigned char *output;
    unsigned char *input;
    unsigned width,height;
    size_t capacity;
    int64_t started,next;
    uint32_t task;
    uint64_t budget;
    char path[128],absolute[180],temporary[192];
} recording;

static esp_err_t record_finish(esp_err_t error)
{
    if(!recording.file)return error;
    bool valid=ouse_avi_finish(&recording.avi,esp_timer_get_time()-recording.started);
    if(fflush(recording.file) || fsync(fileno(recording.file)))valid=false;
    if(fclose(recording.file))valid=false;
    recording.file=NULL;
    struct stat existing;
    bool saved=sd_ready() && valid && stat(recording.absolute,&existing)!=0 && rename(recording.temporary,recording.absolute)==0;
    if(!saved){
        unlink(recording.temporary);if(error==ESP_OK)error=ESP_FAIL;
    }
    lock();client_t *client=find(&camera_owner);if(client){client->state.recording=false;client->state.error=error;
        client->state.recorded_ms=(esp_timer_get_time()-recording.started)/1000;
        if(saved)strcpy(client->state.path,recording.path);
        ++client->state.revision;}
    unlock();
    if(recording.encoder)jpeg_del_encoder_engine(recording.encoder);
    free(recording.input);free(recording.output);free(recording.index);
    if(recording.task)ouse_task_finish(recording.task,error,ouse_task_cancel_requested(recording.task));
    ESP_LOGI("OuseCamera","Recording closed: %s; frames=%lu; %s",recording.path,(unsigned long)recording.avi.count,esp_err_to_name(error));
    if(recording.leased)ouse_files_lease_end();
    memset(&recording,0,sizeof(recording));(void)ouse_files_refresh();return error;
}
static esp_err_t record_start(const job_t *job)
{
    if(recording.file || !hardware || !same(&job->owner,&camera_owner) || !wanted(&job->owner) || !sd_ready())return ESP_ERR_INVALID_STATE;
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<32768)return ESP_ERR_NO_MEM;
    ouse_files_state_t storage;ouse_files_get(&storage);if(storage.free_bytes<8U*1024*1024)return ESP_ERR_NO_MEM;
    recording.budget=storage.free_bytes-4U*1024*1024;
    ouse_camera_frame_t frame;esp_err_t error=driver.frame(&frame);if(error!=ESP_OK)return error;
    unsigned width=frame.width,height=frame.height;driver.put();
    recording.width=width>640 ? 640 : width;recording.height=(uint64_t)height*recording.width/width;
    recording.height&=~15U;
    if(!recording.width || !recording.height)return ESP_ERR_NOT_SUPPORTED;
    recording.index=heap_caps_calloc(RECORD_FRAMES,sizeof(ouse_avi_index_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    jpeg_encode_engine_cfg_t config={.timeout_ms=1000};
    if(!recording.index)error=ESP_ERR_NO_MEM;
    if(error==ESP_OK)error=jpeg_new_encoder_engine(&config,&recording.encoder);
    if(error==ESP_OK && (recording.width!=width || recording.height!=height)){
        jpeg_encode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_ENC_ALLOC_INPUT_BUFFER};size_t allocated;
        recording.input=jpeg_alloc_encoder_mem((size_t)recording.width*recording.height*2,&memory,&allocated);if(!recording.input)error=ESP_ERR_NO_MEM;
    }
    if(error==ESP_OK){jpeg_encode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER};recording.output=jpeg_alloc_encoder_mem((size_t)recording.width*recording.height*2,&memory,&recording.capacity);if(!recording.output)error=ESP_ERR_NO_MEM;}
    if(error==ESP_OK && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<16384)error=ESP_ERR_NO_MEM;
    snprintf(recording.path,sizeof(recording.path),"Pictures/VID_%lld_%08lx_%04lx.avi",(long long)time(NULL),(unsigned long)nonce,(unsigned long)++sequence);
    if(error==ESP_OK && !video_path(recording.path,recording.absolute))error=ESP_ERR_INVALID_ARG;
    snprintf(recording.temporary,sizeof(recording.temporary),"%s.part",recording.absolute);
    if(error==ESP_OK){int descriptor=open(recording.temporary,O_RDWR|O_CREAT|O_EXCL,0600);if(descriptor<0)error=ESP_FAIL;
        else {recording.file=fdopen(descriptor,"w+b");if(!recording.file){close(descriptor);unlink(recording.temporary);error=ESP_FAIL;}}}
    if(error==ESP_OK && !ouse_avi_begin(&recording.avi,recording.file,recording.width,recording.height,recording.index,RECORD_FRAMES))error=ESP_FAIL;
    if(error==ESP_OK)error=ouse_task_create(&job->owner,OUSE_TASK_CAMERA,"recording",OUSE_TASK_STOP_ON_CLOSE,true,&recording.task);
    if(error==ESP_OK && !ouse_task_start(recording.task,true))error=ESP_ERR_INVALID_STATE;
    recording.started=esp_timer_get_time();recording.next=recording.started;
    if(error!=ESP_OK){
        if(recording.file)(void)record_finish(error);
        else {if(recording.encoder)jpeg_del_encoder_engine(recording.encoder);free(recording.input);free(recording.output);free(recording.index);memset(&recording,0,sizeof(recording));}
        return error;
    }
    lock();client_t *client=find(&job->owner);if(client){client->state.recording=true;client->state.recorded_ms=0;}unlock();
    ESP_LOGI("OuseCamera","Recording started: %s; MJPEG AVI; silent; %ux%u; internal_free=%u; largest=%u",recording.path,recording.width,recording.height,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));return ESP_OK;
}
static const unsigned char *record_input(const ouse_camera_frame_t *frame)
{
    if(!recording.input)return frame->pixels;
    const uint16_t *source=(const uint16_t *)frame->pixels;uint16_t *target=(uint16_t *)recording.input;
    for(unsigned vertical=0;vertical<recording.height;++vertical){unsigned source_row=vertical*frame->height/recording.height*frame->stride;
        for(unsigned horizontal=0;horizontal<recording.width;++horizontal)target[vertical*recording.width+horizontal]=source[source_row+horizontal*frame->width/recording.width];}
    return recording.input;
}
static void record_frame(const ouse_camera_frame_t *frame)
{
    if(!recording.file || esp_timer_get_time()<recording.next)return;
    int64_t now=esp_timer_get_time();
    if(!sd_ready()){(void)record_finish(ESP_ERR_INVALID_STATE);return;}
    if(recording.avi.count>=RECORD_FRAMES || now-recording.started>=1800000000LL || ouse_task_cancel_requested(recording.task)){
        (void)record_finish(ESP_OK);return;
    }
    const unsigned char *input=record_input(frame);
    jpeg_encode_cfg_t config={.width=recording.width,.height=recording.height,.src_type=JPEG_ENCODE_IN_FORMAT_RGB565,.sub_sample=JPEG_DOWN_SAMPLING_YUV420,.image_quality=75};
    uint32_t length=0;esp_err_t error=jpeg_encoder_process(recording.encoder,&config,input,recording.width*recording.height*2,recording.output,recording.capacity,&length);
    if(error==ESP_OK && (uint64_t)length+24>recording.budget){(void)record_finish(ESP_ERR_NO_MEM);return;}
    if(error==ESP_OK && (!length || length>recording.capacity || !ouse_avi_frame(&recording.avi,recording.output,length)))error=ESP_FAIL;
    if(error==ESP_OK && recording.avi.count%10==0 && fflush(recording.file))error=ESP_FAIL;
    if(error!=ESP_OK){(void)record_finish(error);return;}
    if(recording.avi.count==1)ESP_LOGI("OuseCamera","First recorded frame: %lu bytes; internal_free=%u",(unsigned long)length,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    recording.budget-=length+24;
    recording.next=now+100000;
    lock();client_t *client=find(&camera_owner);if(client)client->state.recorded_ms=(esp_timer_get_time()-recording.started)/1000;unlock();
}
