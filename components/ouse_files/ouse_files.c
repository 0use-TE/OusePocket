#include "ouse_files.h"
#include "ouse_net.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <strings.h>

#ifndef OUSE_FILES_ROOT
#define OUSE_FILES_ROOT "/sdcard/Ouse"
#endif
#define ROOT OUSE_FILES_ROOT
#define FILE_LIMIT (128U*1024*1024)
typedef struct {uint32_t id;char path[OUSE_FILES_PATH_MAX],url[OUSE_NET_URL_MAX+1];} job_t;
static SemaphoreHandle_t mutex;
static QueueHandle_t jobs;
static ouse_files_state_t state;
static ouse_file_entry_t *entries;
static FATFS *filesystem;
static sdmmc_card_t *medium;
static struct {FILE *file;char target[180],temporary[192];size_t expected,written;uint32_t expected_crc,crc;int64_t touched;} upload;
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static uint32_t checksum(uint32_t crc,const unsigned char *data,size_t size)
{
    for(size_t index=0;index<size;++index){crc^=data[index];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320U:0);}
    return crc;
}
bool ouse_files_path(const char *path,char *absolute,size_t capacity)
{
    if(!path || !absolute || strlen(path)>=OUSE_FILES_PATH_MAX)return false;
    const char *name=strchr(path,'/');if(!name || !name[1])return false;
    size_t category=(size_t)(name-path);
    if(!((category==5 && !strncmp(path,"Music",5)) || (category==5 && !strncmp(path,"Video",5)) ||
         (category==8 && !strncmp(path,"Pictures",8)) || (category==9 && !strncmp(path,"Downloads",9))))return false;
    ++name;if(*name=='.' || strstr(name,".."))return false;
    for(const unsigned char *cursor=(const unsigned char *)name;*cursor;++cursor)
        if(*cursor<32 || strchr("/\\:<>\"|?*",*cursor))return false;
    size_t length=strlen(name);if(name[length-1]==' ' || name[length-1]=='.')return false;
    int written=snprintf(absolute,capacity,ROOT "/%s",path);
    return written>0 && (size_t)written<capacity;
}
void ouse_files_get(ouse_files_state_t *out)
{
    if(!out)return;
    if(!mutex){*out=state;if(!out->error)out->error=ESP_ERR_INVALID_STATE;return;}
    lock();*out=state;unlock();
}
unsigned ouse_files_list(unsigned offset,ouse_file_entry_t *out,unsigned capacity)
{
    if(!mutex || !out || offset>=OUSE_FILES_TRACK_MAX)return 0;
    lock();unsigned count=0;
    while(state.ready && count<capacity && offset+count<state.count){out[count]=entries[offset+count];++count;}
    unlock();return count;
}
FILE *ouse_files_open(const char *path)
{
    char absolute[180];if(!ouse_files_path(path,absolute,sizeof(absolute)))return NULL;
    ouse_files_state_t snapshot;ouse_files_get(&snapshot);if(!snapshot.ready)return NULL;
    return fopen(absolute,"rb");
}
static int compare(const void *first,const void *second)
{return strcmp(((const ouse_file_entry_t *)first)->path,((const ouse_file_entry_t *)second)->path);}
static void scan(void)
{
    ouse_file_entry_t *found=heap_caps_calloc(OUSE_FILES_TRACK_MAX,sizeof(*found),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!found)return;
    unsigned count=0;DIR *directory=opendir(ROOT "/Music");esp_err_t error=directory ? ESP_OK : ESP_FAIL;
    if(directory){struct dirent *entry;
        while(count<OUSE_FILES_TRACK_MAX && (entry=readdir(directory))) {
            size_t length=strlen(entry->d_name);if(length<5 || strcasecmp(entry->d_name+length-4,".wav"))continue;
            char path[OUSE_FILES_PATH_MAX],absolute[180];
            if(snprintf(path,sizeof(path),"Music/%s",entry->d_name)>=(int)sizeof(path) || !ouse_files_path(path,absolute,sizeof(absolute)))continue;
            struct stat info;if(stat(absolute,&info) || !S_ISREG(info.st_mode))continue;
            strcpy(found[count].path,path);found[count++].bytes=info.st_size;
        }
        closedir(directory);qsort(found,count,sizeof(*found),compare);
    }
    uint64_t total=0,free_bytes=0;if(error==ESP_OK)error=esp_vfs_fat_info("/sdcard",&total,&free_bytes);
    lock();memcpy(entries,found,count*sizeof(*found));state.count=count;state.total_bytes=total;state.free_bytes=free_bytes;
    state.error=error;state.ready=error==ESP_OK;++state.revision;unlock();free(found);
}
static bool acquire(void)
{
    if(!mutex)return false;
    lock();bool accepted=state.ready && !state.busy;if(accepted)state.busy=true;unlock();return accepted;
}
static void release(void){lock();state.busy=false;unlock();}
static FILE *prepare(const char *path,char *target,char *temporary)
{
    if(!ouse_files_path(path,target,180))return NULL;
    struct stat info;if(!stat(target,&info)){errno=EEXIST;return NULL;}
    snprintf(temporary,192,"%s.part",target);
    int descriptor=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0666);
    if(descriptor<0){ESP_LOGW("OuseFiles","Create temporary failed: %s errno=%d",temporary,errno);return NULL;}
    FILE *file=fdopen(descriptor,"wb");if(!file){close(descriptor);unlink(temporary);}return file;
}
static esp_err_t commit(FILE *file,const char *temporary,const char *target,bool complete)
{
    bool ok=complete;
    if(fflush(file) || fsync(fileno(file)))ok=false;
    if(fclose(file))ok=false;
    struct stat info;
    if(ok && !stat(target,&info))ok=false;
    if(ok && rename(temporary,target))ok=false;
    if(!ok && unlink(temporary))ESP_LOGW("OuseFiles","Cleanup failed: %s errno=%d",temporary,errno);
    return ok ? ESP_OK : ESP_FAIL;
}
void ouse_files_upload_abort(void)
{
    if(!upload.file)return;
    fclose(upload.file);upload.file=NULL;unlink(upload.temporary);release();
}
void ouse_files_upload_poll(void)
{if(upload.file && esp_timer_get_time()-upload.touched>30000000)ouse_files_upload_abort();}
esp_err_t ouse_files_discard_partial(const char *path)
{
    char target[180],temporary[192];if(!ouse_files_path(path,target,sizeof(target)))return ESP_ERR_INVALID_ARG;
    if(!acquire())return ESP_ERR_INVALID_STATE;
    snprintf(temporary,sizeof(temporary),"%s.part",target);
    int result=unlink(temporary);release();return result==0 || errno==ENOENT ? ESP_OK : ESP_FAIL;
}
esp_err_t ouse_files_checksum(const char *path,size_t *size,uint32_t *crc)
{
    FILE *file=ouse_files_open(path);if(!file)return ESP_ERR_NOT_FOUND;
    unsigned char buffer[1024];size_t total=0,count;uint32_t value=UINT32_MAX;esp_err_t error=ESP_OK;
    while((count=fread(buffer,1,sizeof(buffer),file))){total+=count;if(total>FILE_LIMIT){error=ESP_ERR_INVALID_SIZE;break;}value=checksum(value,buffer,count);}
    if(ferror(file))error=ESP_FAIL;
    fclose(file);if(error==ESP_OK){*size=total;*crc=value^UINT32_MAX;}return error;
}
esp_err_t ouse_files_upload_begin(const char *path,size_t size,uint32_t crc)
{
    if(!size || size>FILE_LIMIT)return ESP_ERR_INVALID_ARG;
    ouse_files_upload_abort();if(!acquire())return ESP_ERR_INVALID_STATE;
    memset(&upload,0,sizeof(upload));upload.file=prepare(path,upload.target,upload.temporary);
    if(!upload.file){release();return ESP_FAIL;}
    upload.expected=size;upload.expected_crc=crc;upload.crc=UINT32_MAX;upload.touched=esp_timer_get_time();return ESP_OK;
}
esp_err_t ouse_files_upload_data(size_t offset,const unsigned char *data,size_t size)
{
    if(!upload.file || !data || !size || offset!=upload.written || size>upload.expected-upload.written)return ESP_ERR_INVALID_ARG;
    if(fwrite(data,1,size,upload.file)!=size){ouse_files_upload_abort();return ESP_FAIL;}
    upload.crc=checksum(upload.crc,data,size);upload.written+=size;upload.touched=esp_timer_get_time();return ESP_OK;
}
esp_err_t ouse_files_upload_end(void)
{
    if(!upload.file)return ESP_ERR_INVALID_STATE;
    esp_err_t error=commit(upload.file,upload.temporary,upload.target,upload.written==upload.expected && (upload.crc^UINT32_MAX)==upload.expected_crc);
    upload.file=NULL;release();if(error==ESP_OK)ouse_files_refresh();return error;
}
typedef struct {FILE *file;uint32_t id;int64_t deadline;ouse_http_response_t *response;} transfer_t;
static bool cancelled(void *context)
{transfer_t *transfer=context;return esp_timer_get_time()>=transfer->deadline || ouse_task_cancel_requested(transfer->id);}
static esp_err_t sink(void *context,const unsigned char *data,size_t size)
{
    transfer_t *transfer=context;
    if(transfer->response->status!=200 || fwrite(data,1,size,transfer->file)!=size)return ESP_FAIL;
    lock();state.download_bytes+=size;unlock();return ESP_OK;
}
static void download(job_t *job)
{
    if(!ouse_task_start(job->id,true))return;
    esp_err_t error=ESP_ERR_INVALID_STATE;bool held=acquire();
    if(held){char target[180],temporary[192];FILE *file=prepare(job->path,target,temporary);
        if(!file)error=ESP_FAIL;
        else {
            ouse_http_response_t *response=calloc(1,sizeof(*response));
            transfer_t transfer={.file=file,.id=job->id,.deadline=esp_timer_get_time()+20LL*60*1000000,.response=response};
            lock();state.download_id=job->id;state.download_bytes=0;unlock();
            ouse_http_request_t request={.url=job->url,.method="GET",.timeout_ms=15000};
            error=response ? ouse_http_transfer(&request,transfer.deadline,FILE_LIMIT,response,sink,cancelled,&transfer) : ESP_ERR_NO_MEM;
            bool complete=error==ESP_OK && response->status==200 && !cancelled(&transfer);
            esp_err_t saved=commit(file,temporary,target,complete);if(error==ESP_OK)error=saved;
            free(response);
        }
        release();scan();
    }
    bool stopped=ouse_task_cancel_requested(job->id);
    ouse_task_finish(job->id,stopped ? ESP_OK : error,stopped);
}
static void worker(void *unused)
{
    (void)unused;job_t *job;
    for(;;){
        if(xQueueReceive(jobs,&job,pdMS_TO_TICKS(2000))==pdTRUE){if(job){download(job);free(job);}else scan();}
        else if(medium){
            esp_err_t error=sdmmc_get_status(medium);
            if(error!=ESP_OK){lock();state.ready=false;state.present=false;state.error=error;state.count=0;++state.revision;unlock();}
        }
    }
}
bool ouse_files_refresh(void)
{job_t *job=NULL;return jobs && xQueueSend(jobs,&job,0)==pdTRUE;}
esp_err_t ouse_files_download(const ouse_task_owner_t *owner,const char *url,const char *path,uint32_t *id)
{
    char absolute[180];ouse_http_request_t request={.url=url,.method="GET",.timeout_ms=15000};
    if(!owner || !id || !ouse_files_path(path,absolute,sizeof(absolute)) || ouse_http_validate(&request)!=ESP_OK)return ESP_ERR_INVALID_ARG;
    if(!jobs)return ESP_ERR_INVALID_STATE;
    job_t *job=calloc(1,sizeof(*job));if(!job)return ESP_ERR_NO_MEM;
    strcpy(job->path,path);strcpy(job->url,url);
    esp_err_t error=ouse_task_create(owner,OUSE_TASK_NETWORK,"download",OUSE_TASK_STOP_ON_CLOSE,true,id);
    if(error==ESP_OK){job->id=*id;if(xQueueSend(jobs,&job,0)!=pdTRUE){error=ESP_ERR_NO_MEM;ouse_task_finish(*id,error,false);}}
    if(error!=ESP_OK)free(job);
    return error;
}
esp_err_t ouse_files_format_unmounted(sdmmc_card_t *card)
{
    if(mutex || !card)return ESP_ERR_INVALID_STATE;
    BYTE drive;esp_err_t error=ff_diskio_get_drive(&drive);if(error!=ESP_OK)return error;
    char name[3]={(char)('0'+drive),':',0};
    ff_diskio_register_sdmmc(drive,card);
    void *work=heap_caps_aligned_alloc(64,4096,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if(!work){ff_diskio_register(drive,NULL);return ESP_ERR_NO_MEM;}
    MKFS_PARM options={.fmt=FM_FAT32,.n_fat=2,.align=2048,.au_size=32768};
    FRESULT result=f_mkfs(name,&options,work,4096);
    heap_caps_free(work);ff_diskio_register(drive,NULL);
    return result==FR_OK ? ESP_OK : ESP_FAIL;
}
esp_err_t ouse_files_init(sdmmc_card_t *card)
{
    if(mutex || !card)return ESP_ERR_INVALID_STATE;
    BYTE drive;esp_err_t error=ff_diskio_get_drive(&drive);if(error!=ESP_OK)return error;
    char name[3]={(char)('0'+drive),':',0};
    ff_diskio_register_sdmmc(drive,card);ff_sdmmc_set_disk_status_check(drive,false);
    esp_vfs_fat_conf_t config={.base_path="/sdcard",.fat_drive=name,.max_files=6};
    error=esp_vfs_fat_register(&config,&filesystem);
    if(error!=ESP_OK){ff_diskio_register(drive,NULL);return error;}
    state.mount_result=f_mount(filesystem,name,1);
    if(state.mount_result!=FR_OK){f_mount(NULL,name,0);esp_vfs_fat_unregister_path("/sdcard");ff_diskio_register(drive,NULL);state.error=ESP_FAIL;return ESP_FAIL;}
    mutex=xSemaphoreCreateMutex();entries=heap_caps_calloc(OUSE_FILES_TRACK_MAX,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    jobs=xQueueCreate(4,sizeof(job_t *));
    if(!mutex || !entries || !jobs){error=ESP_ERR_NO_MEM;goto failed;}
    const char *directories[]={ROOT,ROOT "/Music",ROOT "/Video",ROOT "/Pictures",ROOT "/Downloads"};
    for(unsigned index=0;index<5;++index){struct stat info;
        if(stat(directories[index],&info)==0){if(!S_ISDIR(info.st_mode)){error=ESP_ERR_INVALID_STATE;goto failed;}}
        else if(mkdir(directories[index],0777)){error=ESP_FAIL;goto failed;}
    }
    state.mounted=true;state.present=true;medium=card;scan();
    if(xTaskCreate(worker,"ouse_files",8192,NULL,2,NULL)!=pdPASS){error=ESP_ERR_NO_MEM;goto failed;}
    if(ouse_sd_browser_init()!=ESP_OK)ESP_LOGW("OuseFiles","SD browser unavailable: no worker memory");
    return ESP_OK;
failed:
    if(jobs)vQueueDelete(jobs);
    jobs=NULL;free(entries);entries=NULL;
    if(mutex)vSemaphoreDelete(mutex);
    mutex=NULL;
    f_mount(NULL,name,0);esp_vfs_fat_unregister_path("/sdcard");ff_diskio_register(drive,NULL);
    state=(ouse_files_state_t){.error=error};return error;
}
