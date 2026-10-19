#pragma once
#include <stdio.h>
#include "ouse_tasks.h"
#include "sdmmc_cmd.h"

#define OUSE_FILES_PATH_MAX 128
#define OUSE_FILES_TRACK_MAX 64
typedef struct { char path[OUSE_FILES_PATH_MAX]; uint64_t bytes; } ouse_file_entry_t;
typedef struct {
    bool mounted,ready,busy,present,ejecting,ejected,recovering;
    uint64_t total_bytes,free_bytes,download_bytes;
    unsigned count,revision;
    uint32_t download_id;
    esp_err_t error;
    int mount_result;
} ouse_files_state_t;
esp_err_t ouse_files_init(sdmmc_card_t *card);
esp_err_t ouse_files_start(esp_err_t (*recover)(void));
esp_err_t ouse_files_recover(void);
esp_err_t ouse_files_format_unmounted(sdmmc_card_t *card);
void ouse_files_get(ouse_files_state_t *state);
unsigned ouse_files_list(unsigned offset,ouse_file_entry_t *out,unsigned capacity);
bool ouse_files_path(const char *path,char *absolute,size_t capacity);
bool ouse_files_read_path(const char *path,char *absolute,size_t capacity);
FILE *ouse_files_open(const char *path);
int ouse_files_close(FILE *file);
bool ouse_files_lease_begin(void);
void ouse_files_lease_end(void);
esp_err_t ouse_files_eject(void);
bool ouse_files_refresh(void);
esp_err_t ouse_files_download(const ouse_task_owner_t *owner,const char *url,const char *path,uint32_t *id);
esp_err_t ouse_files_upload_begin(const char *path,size_t size,uint32_t crc);
esp_err_t ouse_files_upload_data(size_t offset,const unsigned char *data,size_t size);
esp_err_t ouse_files_upload_end(void);
void ouse_files_upload_abort(void);
esp_err_t ouse_files_discard_partial(const char *path);
void ouse_files_upload_poll(void);

#define OUSE_SD_PATH_MAX 1024
#define OUSE_SD_PAGE_SIZE 8
typedef struct {char name[256],path[OUSE_SD_PATH_MAX];uint64_t bytes;bool directory,accessible;} ouse_sd_entry_t;
typedef struct {
    char path[OUSE_SD_PATH_MAX];uint32_t id,offset;
    unsigned count;bool pending,more;esp_err_t error;
    ouse_sd_entry_t entries[OUSE_SD_PAGE_SIZE];
} ouse_sd_directory_t;
esp_err_t ouse_sd_browser_init(void);
bool ouse_sd_browser_path(const char *path,char *absolute,size_t capacity);
esp_err_t ouse_sd_browser_request(const ouse_task_owner_t *owner,const char *path,uint32_t offset,uint32_t *id);
bool ouse_sd_browser_result(const ouse_task_owner_t *owner,uint32_t id,ouse_sd_directory_t *out);
void ouse_sd_browser_forget(const ouse_task_owner_t *owner);
esp_err_t ouse_files_checksum(const char *path,size_t *size,uint32_t *crc);
