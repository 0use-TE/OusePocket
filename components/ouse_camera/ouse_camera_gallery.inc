// Gallery enumeration and stable thumbnail cache. Worker-only.
static int recent_first(const void *first,const void *second)
{
    const ouse_photo_entry_t *left=first,*right=second;
    if(left->modified!=right->modified)return left->modified>right->modified ? -1 : 1;
    const char *left_name=strrchr(left->path,'/')+1,*right_name=strrchr(right->path,'/')+1;
    if((!strncmp(left_name,"IMG_",4) || !strncmp(left_name,"VID_",4)) && (!strncmp(right_name,"IMG_",4) || !strncmp(right_name,"VID_",4)))return strcmp(right_name+4,left_name+4);
    return strcmp(right_name,left_name);
}
static esp_err_t enumerate(const job_t *job)
{
    if(!sd_ready())return ESP_ERR_INVALID_STATE;
    char directory[180];if(!ouse_files_path("Pictures/placeholder.jpg",directory,sizeof(directory)))return ESP_FAIL;
    *strrchr(directory,'/')=0;DIR *handle=opendir(directory);if(!handle)return ESP_FAIL;
    unsigned capacity=job->offset+OUSE_PHOTO_PAGE+1;
    ouse_photo_entry_t *recent=heap_caps_calloc(capacity,sizeof(*recent),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!recent){closedir(handle);return ESP_ERR_NO_MEM;}
    unsigned total=0;struct dirent *entry;int64_t deadline=esp_timer_get_time()+3000000;
    while((entry=readdir(handle))!=NULL){
        if(esp_timer_get_time()>deadline || !live(&job->owner) || ouse_task_cancel_requested(job->id)){closedir(handle);free(recent);return ESP_ERR_TIMEOUT;}
        char path[128],absolute[180];int length=snprintf(path,sizeof(path),"Pictures/%s",entry->d_name);if(length<0 || length>=sizeof(path) || !media_path(path,absolute))continue;
        struct stat info;if(stat(absolute,&info) || !S_ISREG(info.st_mode) || info.st_size<=0)continue;
        ouse_photo_entry_t candidate={.bytes=info.st_size,.video=video_path(path,absolute),.modified=info.st_mtime};strcpy(candidate.path,path);
        if(total<capacity){recent[total++]=candidate;qsort(recent,total,sizeof(*recent),recent_first);}
        else if(recent_first(&candidate,&recent[total-1])<0){recent[total-1]=candidate;qsort(recent,total,sizeof(*recent),recent_first);}
    }
    closedir(handle);
    ouse_photo_entry_t entries[OUSE_PHOTO_PAGE]={0};unsigned count=total>job->offset ? total-job->offset : 0;
    bool more=count>OUSE_PHOTO_PAGE;if(count>OUSE_PHOTO_PAGE)count=OUSE_PHOTO_PAGE;
    if(count)memcpy(entries,recent+job->offset,count*sizeof(*recent));
    free(recent);
    bool cached[OUSE_PHOTO_PAGE]={0};
    lock();client_t *client=find(&job->owner);
    bool assigned=client && !client->closed && ouse_photo_slots(entries,count,client->state.entries,client->state.count,cached);
    unlock();
    if(!assigned)return ESP_ERR_INVALID_STATE;
    for(unsigned index=0;index<count;++index){
        if(!live(&job->owner) || ouse_task_cancel_requested(job->id))return ESP_ERR_INVALID_STATE;
        if(cached[index])continue;
        unsigned slot=entries[index].image+2;
        // Only spare slots change during decoding. Corrupt media gets a blank
        // placeholder instead of pixels retained from a previous file.
        lock();client=find(&job->owner);
        if(client && client->images[slot]){memset(client->images[slot],0,128*96*2);++client->versions[slot];}
        unlock();
        (void)load_photo(&job->owner,entries[index].path,slot);
    }
    lock();client=find(&job->owner);if(client && !client->closed){
        memcpy(client->state.entries,entries,sizeof(entries));client->state.count=count;client->state.more=more;client->state.offset=job->offset;
    }unlock();
    return ESP_OK;
}
