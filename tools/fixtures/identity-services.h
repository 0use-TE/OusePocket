#pragma once
#include <string.h>
#include "ouse_identity.h"
static ouse_identity_state_t host_identity={.ready=true};
static char host_pin[7];
void ouse_identity_get(ouse_identity_state_t *out){*out=host_identity;}
bool ouse_identity_valid_name(const char *s){return s && s[0] && strlen(s)<=24 && s[0]!=' ' && s[strlen(s)-1]!=' ';}
bool ouse_identity_valid_pin(const char *s){if(!s||strlen(s)!=6)return false;for(unsigned i=0;i<6;++i)if(s[i]<'0'||s[i]>'9')return false;return true;}
static esp_err_t host_identity_result(esp_err_t e,uint32_t *r){*r=++host_identity.completed;host_identity.error=e;++host_identity.revision;return ESP_OK;}
esp_err_t ouse_identity_name(const char *s,uint32_t *r){if(!ouse_identity_valid_name(s))return ESP_ERR_INVALID_ARG;strcpy(host_identity.name,s);if(!host_identity.setup_step)host_identity.setup_step=1;return host_identity_result(ESP_OK,r);}
esp_err_t ouse_identity_step(unsigned step,uint32_t *r){if(!host_identity.name[0])return ESP_ERR_INVALID_STATE;host_identity.setup_step=step;host_identity.setup_done=step==4;return host_identity_result(ESP_OK,r);}
esp_err_t ouse_identity_verify(const char *s,uint32_t *r){host_identity.verified=!strcmp(s,host_pin);return host_identity_result(host_identity.verified?ESP_OK:ESP_ERR_INVALID_ARG,r);}
esp_err_t ouse_identity_pin(const char *old,const char *s,uint32_t *r){if(host_identity.pin_enabled && strcmp(old,host_pin))return host_identity_result(ESP_ERR_INVALID_ARG,r);strcpy(host_pin,s);host_identity.pin_enabled=s[0]!=0;return host_identity_result(ESP_OK,r);}
