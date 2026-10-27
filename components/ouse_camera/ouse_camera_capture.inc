// Still photo acquisition, JPEG encoding and atomic SD commit. Worker-only.
static void capture_stage(const job_t *job,const char *stage)
{
    lock();client_t *client=find(&job->owner);if(client)snprintf(client->state.error_stage,sizeof(client->state.error_stage),"%s",stage);unlock();
}
static esp_err_t capture_frame(const job_t *job,ouse_camera_frame_t *frame)
{
    // Early sensor frames can be incomplete/EIO. Retry only acquisition, never
    // a file write, and keep cancellation bounded by the driver's 250ms wait.
    for(unsigned attempt=0;attempt<4;++attempt){
        if(!live(&job->owner) || !wanted(&job->owner) || ouse_task_cancel_requested(job->id))return ESP_ERR_INVALID_STATE;
        esp_err_t error=driver.frame(frame);
        if(error!=ESP_ERR_TIMEOUT || attempt==3)return error;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_ERR_TIMEOUT;
}
static esp_err_t capture(const job_t *job,char saved[128])
{
    capture_stage(job,"camera");
    if(!hardware || !same(&job->owner,&camera_owner) || !wanted(&job->owner))return ESP_ERR_INVALID_STATE;
    if(!sd_ready()){capture_stage(job,"storage");return ESP_ERR_INVALID_STATE;}
    ouse_camera_frame_t frame;esp_err_t error=capture_frame(job,&frame);if(error!=ESP_OK)return error;
    capture_stage(job,"encode");
    jpeg_encoder_handle_t encoder=NULL;unsigned char *output=NULL;
    jpeg_encode_engine_cfg_t engine={.timeout_ms=1000};error=jpeg_new_encoder_engine(&engine,&encoder);size_t capacity=0;uint32_t length=0;
    if(error==ESP_OK){jpeg_encode_memory_alloc_cfg_t memory={.buffer_direction=JPEG_ENC_ALLOC_OUTPUT_BUFFER};output=jpeg_alloc_encoder_mem((size_t)frame.width*frame.height*2,&memory,&capacity);if(!output)error=ESP_ERR_NO_MEM;}
    if(error==ESP_OK){jpeg_encode_cfg_t config={.width=frame.width,.height=frame.height,.src_type=JPEG_ENCODE_IN_FORMAT_RGB565,.sub_sample=JPEG_DOWN_SAMPLING_YUV420,.image_quality=85};
        error=jpeg_encoder_process(encoder,&config,frame.pixels,frame.bytes,output,capacity,&length);}
    driver.put();if(encoder)jpeg_del_encoder_engine(encoder);
    if(error!=ESP_OK){free(output);return error;}
    capture_stage(job,"storage");
    char absolute[180]="",temporary[192];
    if(error==ESP_OK && (!length || length>capacity || length>PHOTO_LIMIT))error=ESP_ERR_INVALID_SIZE;
    snprintf(saved,128,"Pictures/IMG_%lld_%08lx_%04lx.jpg",(long long)time(NULL),(unsigned long)nonce,(unsigned long)++sequence);
    if(!photo_path(saved,absolute))error=ESP_ERR_INVALID_ARG;
    snprintf(temporary,sizeof(temporary),"%s.part",absolute);
    if(error==ESP_OK && (!live(&job->owner) || ouse_task_cancel_requested(job->id) || !sd_ready()))error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK){int descriptor=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);if(descriptor<0)error=ESP_FAIL;
        else {FILE *file=fdopen(descriptor,"wb");if(!file){close(descriptor);error=ESP_FAIL;}
            else {bool written=true;for(uint32_t offset=0;offset<length;){uint32_t count=length-offset;if(count>4096)count=4096;if(fwrite(output+offset,1,count,file)!=count){written=false;break;}offset+=count;}
                if(fflush(file) || fsync(fileno(file)))written=false;
                if(fclose(file))written=false;
                if(!written)error=ESP_FAIL;}
            if(error==ESP_OK && (!live(&job->owner) || ouse_task_cancel_requested(job->id) || !wanted(&job->owner)))error=ESP_ERR_INVALID_STATE;
            struct stat existing;if(error==ESP_OK && (stat(absolute,&existing)==0 || rename(temporary,absolute)))error=ESP_FAIL;
            if(error!=ESP_OK)unlink(temporary);
        }
    }
    if(error!=ESP_OK)ESP_LOGW("OuseCamera","Capture commit failed: %s errno=%d",esp_err_to_name(error),errno);
    free(output);if(error==ESP_OK){capture_stage(job,"");(void)ouse_files_refresh();}return error;
}
