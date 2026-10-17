#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <errno.h>
#include "ouse_uninstall_journal.h"
static bool fail_rename;
static int checked_rename(const char *a,const char *b){if(fail_rename){errno=EIO;return -1;}return rename(a,b);}
#define rename checked_rename
#define OUSE_UNINSTALL_DIRECTORY "journal-data"
#include "JOURNAL_SOURCE"
#undef rename
typedef struct {char id[48];} package_t;
static package_t *installed[8];
static char pending_remove[48];
static esp_err_t recovery_error;
static int failure,media_calls,alarm_calls,storage_calls;
static int find_package(const char *id){return installed[0]&&!strcmp(installed[0]->id,id) ? 0 : -1;}
static esp_err_t on_ui(int action,void *p,const char *id){assert(action==2&&!p&&!strcmp(id,"user.a"));return failure==1 ? ESP_FAIL : ESP_ERR_NOT_FOUND;}
static void ouse_alarm_register_app(const char *id,bool allowed){assert(!allowed&&!strcmp(id,"user.a"));}
static void path_for(const char *id,char path[128]){snprintf(path,128,"journal-data/%s.bpk",id);}
static esp_err_t ouse_media_purge(const char *id){(void)id;++media_calls;return failure==2 ? ESP_FAIL : ESP_OK;}
static esp_err_t ouse_alarm_purge(const char *id){(void)id;++alarm_calls;return failure==3 ? ESP_FAIL : ESP_OK;}
static esp_err_t ouse_storage_purge(const char *id){(void)id;++storage_calls;return failure==4 ? ESP_FAIL : ESP_OK;}
static void ouse_bpk_free(package_t *p){free(p);}
static esp_err_t ouse_storage_refresh(void){return ESP_OK;}
/* UNINSTALL_FINISH */
int main(void)
{
    _mkdir("journal-data");_unlink("journal-data/remove.pending");_unlink("journal-data/remove.tmp");char id[48];
    assert(ouse_uninstall_intent_read(id)==ESP_ERR_NOT_FOUND);
    assert(ouse_uninstall_intent_write("../../bad")==ESP_ERR_INVALID_ARG);
    fail_rename=true;assert(ouse_uninstall_intent_write("user.a")==ESP_FAIL);assert(ouse_uninstall_intent_read(id)==ESP_ERR_NOT_FOUND);fail_rename=false;
    for(int stage=1;stage<=4;++stage){
        assert(ouse_uninstall_intent_write("user.a")==ESP_OK);assert(ouse_uninstall_intent_write("user.b")==ESP_ERR_INVALID_STATE);
        installed[0]=calloc(1,sizeof(package_t));strcpy(installed[0]->id,"user.a");strcpy(pending_remove,"user.a");
        FILE *f=fopen("journal-data/user.a.bpk","wb");assert(f);fputs("package",f);fclose(f);
        failure=stage;assert(finish_uninstall("user.a")==ESP_FAIL);assert(installed[0]);
        assert(ouse_uninstall_intent_read(id)==ESP_OK&&!strcmp(id,"user.a"));
        // A reboot/retry can resume from intent even when the package is gone.
        failure=0;assert(finish_uninstall(id)==ESP_OK);assert(!installed[0]&&!pending_remove[0]);assert(ouse_uninstall_intent_read(id)==ESP_ERR_NOT_FOUND);
    }
    assert(media_calls&&alarm_calls&&storage_calls);
    assert(ouse_uninstall_intent_write("user.a")==ESP_OK);
    FILE *f=fopen("journal-data/remove.pending","r+b");assert(f);fseek(f,12,SEEK_SET);fputc('!',f);fclose(f);
    assert(ouse_uninstall_intent_read(id)==ESP_ERR_INVALID_STATE);assert(ouse_uninstall_intent_write("user.a")==ESP_ERR_INVALID_STATE);
    assert(_access("journal-data/remove.pending",0)==0);_unlink("journal-data/remove.pending");
    puts("PASS journal: actual disk schema/CRC/failed commit preservation and real uninstall cleanup retries across each service failure");
}
