// SPDX-License-Identifier: MIT
#include "ouse_screen.h"
#include "ouse_devices.h"
#include "ouse_dialog.h"
#include "ouse_camera.h"
#include "ouse_setup.h"
#include "esp_log.h"
static struct {
    ouse_core_port_t port;
    lv_indev_read_cb_t read;
    uint32_t activity;
    uint8_t brightness;
    bool asleep,consume;
    bool injecting,remote_down;
    lv_point_t remote_point;
    uint32_t remote_activity;
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
    if(e==ESP_OK){screen.asleep=true;ouse_setup_lock();ouse_power_set_asleep(true);ESP_LOGI("OuseScreen","Display asleep");}return e;
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
    if(screen.injecting || screen.remote_down) {
        data->point=screen.remote_point;
        data->state=screen.remote_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else if(screen.read)screen.read(input,data);
    bool pressed=data->state==LV_INDEV_STATE_PRESSED;
#if CONFIG_OUSE_DEVICE_DEBUG
    static bool physical_down;
    bool physical=!screen.injecting && !screen.remote_down && pressed;
    if(physical && !physical_down && !ouse_core_access_blocked())ESP_LOGI("OuseTouch","Physical down: %d,%d",(int)data->point.x,(int)data->point.y);
    physical_down=physical;
#endif
    if(pressed)screen.activity=lv_tick_get();
    if(screen.asleep && pressed){(void)ouse_screen_wake();screen.consume=true;}
    // Consume the complete waking contact before LVGL sees any pressed event.
    if(screen.consume){data->state=LV_INDEV_STATE_RELEASED;if(!pressed)screen.consume=false;}
}
esp_err_t ouse_screen_inject(int x,int y,bool pressed)
{
    if(!screen.port.touch)return ESP_ERR_INVALID_STATE;
    if(x<0 || y<0 || x>=lv_display_get_horizontal_resolution(screen.port.display) ||
       y>=lv_display_get_vertical_resolution(screen.port.display))return ESP_ERR_INVALID_ARG;
#if CONFIG_OUSE_DEVICE_DEBUG
    if(!ouse_core_access_blocked())ESP_LOGI("OuseTouch","Remote %s: %d,%d",pressed ? "down" : "up",x,y);
#endif
    screen.remote_point=(lv_point_t){x,y};screen.remote_down=pressed;
    screen.remote_activity=lv_tick_get();screen.injecting=true;
    lv_indev_read(screen.port.touch);
    screen.injecting=false;
    return ESP_OK;
}
static void idle(lv_timer_t *timer)
{
    (void)timer;ouse_power_state_t p;ouse_audio_state_t a;
    // Disconnecting a client must not leave a pressed pointer forever.
    if(screen.remote_down && lv_tick_elaps(screen.remote_activity)>10000)
        (void)ouse_screen_inject(screen.remote_point.x,screen.remote_point.y,false);
    bool busy=ouse_audio_get(&a)==ESP_OK && a.mode!=OUSE_AUDIO_IDLE && a.mode!=OUSE_AUDIO_ERROR;
    if(busy || ouse_camera_media_active() || ouse_dialog_active()){screen.activity=lv_tick_get();return;}
    if(!screen.asleep && ouse_power_get(&p)==ESP_OK && p.sleep_seconds && lv_tick_elaps(screen.activity)>=p.sleep_seconds*1000u) {
        esp_err_t e=ouse_screen_sleep();if(e!=ESP_OK){screen.activity=lv_tick_get();ouse_dialog_show("无法关闭屏幕，请重试",NULL,NULL);}
    }
}
void ouse_screen_init(const ouse_core_port_t *port,uint8_t brightness)
{
    screen.port=*port;screen.brightness=brightness;screen.activity=lv_tick_get();
    screen.read=lv_indev_get_read_cb(port->touch);lv_indev_set_read_cb(port->touch,read_touch);
    lv_timer_create(idle,250,NULL);
}
