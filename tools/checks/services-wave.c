#include "ouse_wave.h"
#include "ouse_alarm.h"
#include "services-wave-fixture.h"
#include <assert.h>
#include <stdio.h>
static unsigned output_count;static int expected;static bool abort_output,any_sample;
static esp_err_t output(void *context,const int16_t *samples,unsigned count)
{(void)context;if(abort_output)return ESP_ERR_INVALID_STATE;for(unsigned i=0;i<count;++i)assert(any_sample || samples[i]==expected);output_count+=count;return ESP_OK;}
int main(int argc,char **argv)
{
    const unsigned rates[]={8000,16000,44100,48000};
    for(unsigned r=0;r<4;++r)for(unsigned channels=1;channels<=2;++channels)for(unsigned fragment=1;fragment<=1024;fragment*=8){
        size_t size;unsigned frames=rates[r]/10;unsigned char *data=make_wave(rates[r],channels,frames,&size);ouse_wave_t w;
        output_count=0;expected=channels==2 ? 0 : 1000;ouse_wave_init(&w,output,NULL);
        for(size_t at=0;at<size;){size_t n=size-at;if(n>fragment)n=fragment;assert(ouse_wave_feed(&w,data+at,n)==ESP_OK);at+=n;}
        assert(ouse_wave_finish(&w)==ESP_OK&&output_count==(uint64_t)frames*16000/rates[r]&&w.frames==frames);free(data);
    }
    size_t size;unsigned char *data=make_wave(16000,1,1000,&size);ouse_wave_t w;
    ouse_wave_init(&w,output,NULL);expected=1000;assert(ouse_wave_feed(&w,data,size-1)==ESP_OK&&ouse_wave_finish(&w)!=ESP_OK);
    data[34]=24;ouse_wave_init(&w,output,NULL);assert(ouse_wave_feed(&w,data,size)==ESP_ERR_NOT_SUPPORTED);data[34]=16;
    wave32(data+40,0x7fffffff);ouse_wave_init(&w,output,NULL);assert(ouse_wave_feed(&w,data,size)==ESP_ERR_INVALID_SIZE);wave32(data+40,2000);
    abort_output=true;ouse_wave_init(&w,output,NULL);assert(ouse_wave_feed(&w,data,size)==ESP_ERR_INVALID_STATE);abort_output=false;free(data);
    ouse_alarm_spec_t a={.id="weekly",.title="Morning",.weekdays=2,.minute_of_day=60};
    int64_t monday=1704067200LL;
    assert(ouse_alarm_next(&a,monday)==monday+3600);
    assert(ouse_alarm_next(&a,monday+3600)==monday+7*86400+3600);
    a.utc_offset_minutes=480;a.minute_of_day=450;
    assert(ouse_alarm_next(&a,monday)==monday+7*86400-1800);
    a.weekdays=127;a.utc_offset_minutes=-300;a.minute_of_day=0;
    assert(ouse_alarm_next(&a,monday)==monday+5*3600);
    a.weekdays=0;a.at_utc=monday+10;assert(ouse_alarm_next(&a,monday)==monday+10&&ouse_alarm_next(&a,monday+10)==0);
    a.weekdays=128;assert(!ouse_alarm_spec_valid(&a));a.weekdays=1;a.minute_of_day=1440;assert(!ouse_alarm_spec_valid(&a));
    if(argc==2){
        FILE *file=fopen(argv[1],"rb");assert(file);unsigned char chunk[997];size_t n;
        any_sample=true;output_count=0;ouse_wave_init(&w,output,NULL);
        while((n=fread(chunk,1,sizeof(chunk),file)))assert(ouse_wave_feed(&w,chunk,n)==ESP_OK);
        assert(!ferror(file));fclose(file);assert(ouse_wave_finish(&w)==ESP_OK);
        assert(w.rate==8000&&w.channels==1&&w.frames==240000&&output_count==480000);
    }
    puts("PASS WAVE/schedule: real fragmented PCM16 mono/stereo 8/16/44.1/48k conversion, truncation/format/bounds/abort; UTC weekly offsets, strict recurrence, one-shot");
}
