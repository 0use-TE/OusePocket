// SPDX-License-Identifier: MIT
#include "ouse_screen.h"
#include "ouse_devices.h"
#include "ouse_dialog.h"
#include "ouse_camera.h"
#include "ouse_setup.h"
#include "esp_log.h"
typedef struct {lv_indev_t *input;lv_indev_read_cb_t read;bool injecting,remote_down,consume,physical_down;lv_point_t point;uint32_t activity;} contact_t;
static struct {
    ouse_core_port_t port;
    contact_t contacts[2];
    uint32_t activity;
    uint8_t brightness;
    bool asleep;
} screen;
esp_err_t ouse_screen_brightness(uint8_t value)
{
    esp_err_t e=screen.asleep ? ESP_OK : screen.port.set_brightness(screen.port.context,value);
    if(e==ESP_OK)screen.brightness=value;
    return e;
}
esp_err_t ouse_screen_sleep(void)
{
    if(screen.asleep)return ESP_OK;
    esp_err_t e=screen.port.set_brightness(screen.port.context,0);
    if(e==ESP_OK){for(unsigned i=0;i<2;i++){screen.contacts[i].consume=true;if(screen.contacts[i].input)lv_indev_reset(screen.contacts[i].input,NULL);}screen.asleep=true;ouse_setup_lock();ouse_power_set_asleep(true);ESP_LOGI("OuseScreen","Display asleep");}return e;
}
esp_err_t ouse_screen_wake(void)
{
    screen.activity=lv_tick_get();if(!screen.asleep)return ESP_OK;
    ouse_setup_lock();
    esp_err_t e=screen.port.set_brightness(screen.port.context,screen.brightness);
    if(e==ESP_OK){screen.asleep=false;ouse_power_set_asleep(false);ESP_LOGI("OuseScreen","Display awake");}return e;
}
static void read_touch(lv_indev_t *input,lv_indev_data_t *data)
{
    unsigned slot=0;while(slot<2 && screen.contacts[slot].input!=input)++slot;if(slot>=2){data->state=LV_INDEV_STATE_RELEASED;return;}
    contact_t *c=&screen.contacts[slot];
    if(c->injecting || c->remote_down){
        data->point=c->point;
        if(lv_display_get_rotation(screen.port.display)==LV_DISPLAY_ROTATION_270)data->point=(lv_point_t){lv_display_get_vertical_resolution(screen.port.display)-1-c->point.y,c->point.x};
        data->state=c->remote_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    }else if(c->read)c->read(input,data);
    bool pressed=data->state==LV_INDEV_STATE_PRESSED;
#if CONFIG_OUSE_DEVICE_DEBUG
    bool physical=!c->injecting&&!c->remote_down&&pressed;
    if(physical&&!c->physical_down&&!ouse_core_access_blocked())ESP_LOGI("OuseTouch","Physical %u down: %d,%d",slot,(int)data->point.x,(int)data->point.y);
    c->physical_down=physical;
#endif
    if(pressed)screen.activity=lv_tick_get();
    if(screen.asleep&&pressed){for(unsigned i=0;i<2;i++)screen.contacts[i].consume=true;(void)ouse_screen_wake();}
    if(c->consume){data->state=LV_INDEV_STATE_RELEASED;if(!pressed)c->consume=false;}
}
esp_err_t ouse_screen_inject_contact(unsigned slot,int x,int y,bool pressed)
{
    if(slot>=2||!screen.contacts[slot].input)return ESP_ERR_INVALID_STATE;
    if(x<0||y<0||x>=lv_display_get_horizontal_resolution(screen.port.display)||y>=lv_display_get_vertical_resolution(screen.port.display))return ESP_ERR_INVALID_ARG;
    contact_t *c=&screen.contacts[slot];c->point=(lv_point_t){x,y};c->remote_down=pressed;c->activity=lv_tick_get();c->injecting=true;
    lv_indev_read(c->input);c->injecting=false;return ESP_OK;
}
esp_err_t ouse_screen_inject(int x,int y,bool pressed){return ouse_screen_inject_contact(0,x,y,pressed);}
static void idle(lv_timer_t *timer)
{
    (void)timer;ouse_power_state_t p;ouse_audio_state_t a;
    // Disconnecting a client must not leave a pressed pointer forever.
    for(unsigned i=0;i<2;i++){contact_t *c=&screen.contacts[i];if(c->remote_down&&lv_tick_elaps(c->activity)>10000)(void)ouse_screen_inject_contact(i,c->point.x,c->point.y,false);}
    bool busy=ouse_audio_get(&a)==ESP_OK && a.mode!=OUSE_AUDIO_IDLE && a.mode!=OUSE_AUDIO_ERROR;
    if(busy || ouse_camera_media_active() || ouse_dialog_active()){screen.activity=lv_tick_get();return;}
    if(!screen.asleep && ouse_power_get(&p)==ESP_OK && p.sleep_seconds && lv_tick_elaps(screen.activity)>=p.sleep_seconds*1000u) {
        esp_err_t e=ouse_screen_sleep();if(e!=ESP_OK){screen.activity=lv_tick_get();ouse_dialog_show("无法关闭屏幕，请重试",NULL,NULL);}
    }
}
void ouse_screen_init(const ouse_core_port_t *port,uint8_t brightness)
{
    screen.port=*port;screen.brightness=brightness;screen.activity=lv_tick_get();
    screen.contacts[0].input=port->touch;
    for(lv_indev_t *input=lv_indev_get_next(NULL);input;input=lv_indev_get_next(input))if(input!=port->touch&&lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER&&lv_indev_get_display(input)==port->display){screen.contacts[1].input=input;break;}
    for(unsigned i=0;i<2;i++)if(screen.contacts[i].input){screen.contacts[i].read=lv_indev_get_read_cb(screen.contacts[i].input);lv_indev_set_read_cb(screen.contacts[i].input,read_touch);lv_timer_set_period(lv_indev_get_read_timer(screen.contacts[i].input),8);}
    lv_timer_create(idle,250,NULL);
}
