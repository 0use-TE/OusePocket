// SPDX-License-Identifier: MIT
#include "ouse_alarm.h"
#include <string.h>
#include <time.h>
bool ouse_alarm_spec_valid(const ouse_alarm_spec_t *s)
{
    if(!s || !s->id[0] || !s->title[0] || strnlen(s->id,sizeof(s->id))>=sizeof(s->id) ||
       strnlen(s->title,sizeof(s->title))>=sizeof(s->title) || strnlen(s->payload,sizeof(s->payload))>=sizeof(s->payload) ||
       s->weekdays>127 || s->minute_of_day>1439 || s->utc_offset_minutes< -720 || s->utc_offset_minutes>840)return false;
    for(const char *p=s->id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return false;
    return s->weekdays || (s->at_utc>=OUSE_CLOCK_MIN_UTC && s->at_utc<OUSE_CLOCK_MAX_UTC);
}
int64_t ouse_alarm_next(const ouse_alarm_spec_t *s,int64_t after)
{
    if(!ouse_alarm_spec_valid(s) || after>=OUSE_CLOCK_MAX_UTC)return 0;
    if(!s->weekdays)return s->at_utc>after ? s->at_utc : 0;
    if(after<OUSE_CLOCK_MIN_UTC)after=OUSE_CLOCK_MIN_UTC-1;
    int64_t local=after+(int64_t)s->utc_offset_minutes*60;
    int64_t day=local/86400;
    for(unsigned i=0;i<8;++i){
        // Unix epoch was Thursday. Avoid libc timezone/global state entirely.
        unsigned weekday=(unsigned)((day+i+4)%7);
        int64_t at=(day+i)*86400+(int64_t)s->minute_of_day*60-(int64_t)s->utc_offset_minutes*60;
        if((s->weekdays&(1u<<weekday)) && at>after && at<OUSE_CLOCK_MAX_UTC)return at;
    }
    return 0;
}
