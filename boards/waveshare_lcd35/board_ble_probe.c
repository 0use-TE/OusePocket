#include "sdkconfig.h"
#if CONFIG_OUSE_DEVICE_DEBUG && !CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE && !CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern int esp_hosted_tx(uint8_t interface_type,uint8_t interface_number,uint8_t *payload,uint16_t length,uint8_t zerocopy,uint8_t *owned,void (*release)(void *),uint8_t flags);
static portMUX_TYPE probe_lock=portMUX_INITIALIZER_UNLOCKED;
static struct {bool active,done;uint16_t opcode;uint8_t status,version,features[8];unsigned advertisements;} probe;

int hci_rx_handler(uint8_t *bytes,size_t size)
{
    if(!bytes || size<3 || bytes[0]!=4 || (size_t)bytes[2]+3>size)return ESP_OK;
    portENTER_CRITICAL(&probe_lock);
    if(probe.active){
        if(bytes[1]==0x0e && size>=7 && (uint16_t)(bytes[4]|((uint16_t)bytes[5]<<8))==probe.opcode){
            probe.status=bytes[6];
            if(probe.opcode==0x1001 && size>=8)probe.version=bytes[7];
            if(probe.opcode==0x2003 && size>=15)memcpy(probe.features,bytes+7,8);
            probe.done=true;
        }else if(bytes[1]==0x0f && size>=7 && (uint16_t)(bytes[5]|((uint16_t)bytes[6]<<8))==probe.opcode){probe.status=bytes[3];probe.done=true;}
        else if(bytes[1]==0x3e && size>=5 && bytes[3]==2)probe.advertisements+=bytes[4];
    }
    portEXIT_CRITICAL(&probe_lock);return ESP_OK;
}
static esp_err_t command(uint16_t opcode,const uint8_t *parameters,uint8_t length)
{
    uint8_t *packet=heap_caps_aligned_alloc(64,(size_t)length+4,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!packet)return ESP_ERR_NO_MEM;
    packet[0]=1;packet[1]=(uint8_t)opcode;packet[2]=(uint8_t)(opcode>>8);packet[3]=length;
    if(length)memcpy(packet+4,parameters,length);
    portENTER_CRITICAL(&probe_lock);probe.opcode=opcode;probe.done=false;portEXIT_CRITICAL(&probe_lock);
    if(esp_hosted_tx(4,0,packet,(uint16_t)length+4,0,packet,heap_caps_free,0)!=ESP_OK)return ESP_FAIL;
    for(unsigned attempt=0;attempt<200;++attempt){
        vTaskDelay(pdMS_TO_TICKS(10));
        portENTER_CRITICAL(&probe_lock);bool done=probe.done;uint8_t status=probe.status;portEXIT_CRITICAL(&probe_lock);
        if(done)return status==0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_ERR_TIMEOUT;
}
esp_err_t ouse_board_debug_ble_probe(char *out,size_t capacity)
{
    portENTER_CRITICAL(&probe_lock);
    if(probe.active){portEXIT_CRITICAL(&probe_lock);return ESP_ERR_INVALID_STATE;}
    memset(&probe,0,sizeof(probe));probe.active=true;portEXIT_CRITICAL(&probe_lock);
    const char *stage="version";
    esp_err_t error=command(0x1001,NULL,0),stopped=ESP_OK;
    if(error==ESP_OK){stage="features";error=command(0x2003,NULL,0);}
    if(error==ESP_OK){const uint8_t parameters[]={0,0xa0,0,0x50,0,0,0};stage="scan-parameters";error=command(0x200b,parameters,sizeof(parameters));}
    bool scanning=false;
    if(error==ESP_OK){const uint8_t parameters[]={1,1};stage="scan-start";scanning=true;error=command(0x200c,parameters,sizeof(parameters));}
    if(error==ESP_OK){stage="scan";vTaskDelay(pdMS_TO_TICKS(3000));}
    if(scanning){const uint8_t parameters[]={0,0};stopped=command(0x200c,parameters,sizeof(parameters));}
    portENTER_CRITICAL(&probe_lock);probe.active=false;unsigned reports=probe.advertisements;uint8_t version=probe.version,status=probe.status;portEXIT_CRITICAL(&probe_lock);
    snprintf(out,capacity,"stage=%s,result=%s,stop=%s,hci_version=%u,reports=%u,status=%u,c6_firmware_unchanged=1",stage,esp_err_to_name(error),esp_err_to_name(stopped),version,reports,status);
    return ESP_OK;
}
#endif
