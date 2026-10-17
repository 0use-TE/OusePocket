// SPDX-License-Identifier: MIT
#include "ouse_uninstall_journal.h"
#include "ouse_protocol.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifndef OUSE_UNINSTALL_DIRECTORY
#define OUSE_UNINSTALL_DIRECTORY "/bpkfs/ouse-apps"
#endif
#define INTENT OUSE_UNINSTALL_DIRECTORY "/remove.pending"
#define TEMP OUSE_UNINSTALL_DIRECTORY "/remove.tmp"
static bool valid(const char *id)
{
    size_t n=strnlen(id,48);if(n<6 || n>=48 || strncmp(id,"user.",5))return false;
    for(size_t i=0;i<n;++i){char c=id[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;}
    return true;
}
static void checksum(unsigned char bytes[64])
{uint32_t crc=ouse_crc32(bytes,60);for(unsigned i=0;i<4;++i)bytes[60+i]=(unsigned char)(crc>>(i*8));}
esp_err_t ouse_uninstall_intent_read(char id[48])
{
    if(!id)return ESP_ERR_INVALID_ARG;
    id[0]=0;unsigned char bytes[64],checked[64];FILE *file=fopen(INTENT,"rb");
    if(!file)return errno==ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    bool ok=fread(bytes,1,64,file)==64 && fgetc(file)==EOF && !ferror(file);
    if(fclose(file)!=0)ok=false;
    if(!ok || memcmp(bytes,"OURM\1\0\0\0",8) || !memchr(bytes+8,0,48))return ESP_ERR_INVALID_STATE;
    memcpy(checked,bytes,64);checksum(checked);
    if(memcmp(bytes+60,checked+60,4) || !valid((const char *)bytes+8))return ESP_ERR_INVALID_STATE;
    strcpy(id,(const char *)bytes+8);return ESP_OK;
}
esp_err_t ouse_uninstall_intent_write(const char *id)
{
    if(!id || !valid(id))return ESP_ERR_INVALID_ARG;
    char prior[48];esp_err_t error=ouse_uninstall_intent_read(prior);
    if(error==ESP_OK)return strcmp(id,prior) ? ESP_ERR_INVALID_STATE : ESP_OK;
    if(error!=ESP_ERR_NOT_FOUND)return error;
    unsigned char bytes[64]={0};memcpy(bytes,"OURM\1\0\0\0",8);strcpy((char *)bytes+8,id);checksum(bytes);
    FILE *file=fopen(TEMP,"wb");if(!file)return ESP_FAIL;
    bool ok=fwrite(bytes,1,64,file)==64;
    if(fflush(file)!=0)ok=false;
    if(fsync(fileno(file))!=0)ok=false;
    if(fclose(file)!=0)ok=false;
    if(!ok || rename(TEMP,INTENT)!=0){unlink(TEMP);return ESP_FAIL;}
    return ESP_OK;
}
esp_err_t ouse_uninstall_intent_clear(void)
{return unlink(INTENT)==0 || errno==ENOENT ? ESP_OK : ESP_FAIL;}
