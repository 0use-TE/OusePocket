// SPDX-License-Identifier: MIT
#include "ouse_debug.h"
#include "ouse_shell.h"
#include "ouse_screen.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

esp_err_t ouse_debug_touch(int x,int y,bool pressed) {return ouse_screen_inject(x,y,pressed);}
esp_err_t ouse_debug_home(void)
{ esp_err_t e=ouse_core_wake_display();if(e!=ESP_OK)return e;ouse_shell_foreground();return ouse_app_background(); }
esp_err_t ouse_debug_open(const char *id)
{ esp_err_t e=ouse_core_wake_display();if(e!=ESP_OK)return e;ouse_shell_foreground();return ouse_app_open(id); }
esp_err_t ouse_debug_state(char *out,size_t cap)
{
    ouse_app_stats_t app;ouse_shell_state_t shell;ouse_app_info_t info;
    unsigned page,pages;ouse_core_launcher_state(&page,&pages);
    if(!out || cap<2)return ESP_ERR_INVALID_ARG;
    esp_err_t error=ouse_app_get_stats(&app);if(error!=ESP_OK)return error;
    ouse_shell_get_state(&shell);
    int n=snprintf(out,cap,"{\"core\":\"%s\",\"width\":%ld,\"height\":%ld,\"active\":\"%s\",\"live\":%u,\"registered\":%u,\"quick\":%s,\"recents\":%s,\"tracking\":%s,\"cards\":%u,\"scroll_x\":%d,\"theme\":%u,\"locale\":%u,\"font\":%u,\"dpi\":%u,\"notifications\":%s,\"notification_cards\":%u,\"quick_y\":%d,\"alive\":[",
        ouse_core_version(),(long)lv_display_get_horizontal_resolution(NULL),(long)lv_display_get_vertical_resolution(NULL),
        app.active_id,app.live_instances,app.registered,shell.quick?"true":"false",shell.recents?"true":"false",
        shell.tracking?"true":"false",shell.cards,shell.recent_scroll_x,(unsigned)ouse_theme_get(),
        ouse_ui_locale(),ouse_ui_font_percent(),ouse_ui_density(),
        shell.notifications?"true":"false",shell.notification_cards,shell.quick_y);
    if(n<0 || (size_t)n>=cap)return ESP_ERR_NO_MEM;
    size_t used=(size_t)n;bool first=true;
    for(unsigned i=0;ouse_app_get_recent(i,&info)==ESP_OK;++i){
        int w=snprintf(out+used,cap-used,"%s\"%s\"",first?"":",",info.id);
        if(w<0 || (size_t)w>=cap-used)return ESP_ERR_NO_MEM;
        used+=(size_t)w;first=false;
    }
    if(used+3>cap)return ESP_ERR_NO_MEM;
    int tail=snprintf(out+used,cap-used,"],\"page\":%u,\"pages\":%u,\"per_page\":12,\"bar_offset\":%d}",page+1,pages,shell.bar_offset);
    if(tail<0 || (size_t)tail>=cap-used)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
typedef struct {char *out;size_t used,cap;unsigned nodes;bool failed,first;} writer_t;
static void append(writer_t *w,const char *format,...)
{
    if(w->failed)return;
    va_list a;va_start(a,format);int n=vsnprintf(w->out+w->used,w->cap-w->used,format,a);va_end(a);
    if(n<0 || (size_t)n>=w->cap-w->used)w->failed=true;else w->used+=(size_t)n;
}
static void quoted(writer_t *w,const char *text)
{
    size_t n=strlen(text);if(n>160){n=160;while(n && ((unsigned char)text[n]&0xc0)==0x80)--n;}
    append(w,"\"");
    for(size_t i=0;i<n;++i){unsigned char c=(unsigned char)text[i];
        if(c=='"' || c=='\\')append(w,"\\%c",c);
        else if(c<32)append(w,"\\u%04x",c);else append(w,"%c",c);
    }
    append(w,"\"");
}
static void visit(writer_t *w,lv_obj_t *obj,unsigned depth)
{
    if(depth>14 || w->nodes>=160 || !lv_obj_is_visible(obj))return;
    lv_area_t a;lv_obj_get_coords(obj,&a);
    const char *type=lv_obj_check_type(obj,&lv_label_class)?"label":lv_obj_check_type(obj,&lv_button_class)?"button":
        lv_obj_check_type(obj,&lv_slider_class)?"slider":lv_obj_check_type(obj,&lv_switch_class)?"switch":
        lv_obj_check_type(obj,&lv_textarea_class)?"input":lv_obj_check_type(obj,&lv_checkbox_class)?"checkbox":
        lv_obj_check_type(obj,&lv_dropdown_class)?"dropdown":lv_obj_check_type(obj,&lv_spinner_class)?"spinner":
        lv_obj_check_type(obj,&lv_arc_class)?"arc":lv_obj_check_type(obj,&lv_bar_class)?"progressBar":
        lv_obj_check_type(obj,&lv_keyboard_class)?"keyboard":"object";
    append(w,"%s{\"depth\":%u,\"type\":\"%s\",\"bounds\":[%ld,%ld,%ld,%ld],\"clickable\":%s,\"text\":",
        w->first?"":",",depth,type,(long)a.x1,(long)a.y1,(long)a.x2,(long)a.y2,
        lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE)?"true":"false");
    w->first=false;++w->nodes;
    const char *text="";char selection[513];
    if(lv_obj_check_type(obj,&lv_label_class))text=lv_label_get_text(obj);
    else if(lv_obj_check_type(obj,&lv_checkbox_class))text=lv_checkbox_get_text(obj);
    else if(lv_obj_check_type(obj,&lv_dropdown_class)){
        lv_dropdown_get_selected_str(obj,selection,sizeof(selection));text=selection;
    }
    quoted(w,text);
    append(w,",\"fg\":\"#%06lX\",\"bg\":\"#%06lX\",\"bg_opa\":%u,\"border\":\"#%06lX\",\"border_width\":%ld,\"radius\":%ld,\"disabled\":%s,\"checked\":%s}",
        (unsigned long)lv_color_to_u32(lv_obj_get_style_text_color(obj,0))&0xffffffUL,
        (unsigned long)lv_color_to_u32(lv_obj_get_style_bg_color(obj,0))&0xffffffUL,
        (unsigned)lv_obj_get_style_bg_opa(obj,0),
        (unsigned long)lv_color_to_u32(lv_obj_get_style_border_color(obj,0))&0xffffffUL,
        (long)lv_obj_get_style_border_width(obj,0),(long)lv_obj_get_style_radius(obj,0),
        lv_obj_has_state(obj,LV_STATE_DISABLED)?"true":"false",lv_obj_has_state(obj,LV_STATE_CHECKED)?"true":"false");
    // Do not traverse input internals or export password values.
    if(lv_obj_check_type(obj,&lv_textarea_class))return;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i)visit(w,lv_obj_get_child(obj,i),depth+1);
}
esp_err_t ouse_debug_tree(char *out,size_t cap)
{
    if(!out || cap<2)return ESP_ERR_INVALID_ARG;
    lv_obj_update_layout(lv_screen_active());writer_t w={.out=out,.cap=cap,.first=true};
    append(&w,"{\"nodes\":[");visit(&w,lv_screen_active(),0);
    lv_obj_t *layers[]={lv_layer_top(),lv_layer_sys()};
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<lv_obj_get_child_count(layers[i]);++j)visit(&w,lv_obj_get_child(layers[i],j),0);
    append(&w,"],\"limit\":160}");return w.failed ? ESP_ERR_NO_MEM : ESP_OK;
}
esp_err_t ouse_debug_snapshot(ouse_debug_image_t *image)
{
    if(!image)return ESP_ERR_INVALID_ARG;
    memset(image,0,sizeof(*image));lv_obj_t *root=lv_screen_active();lv_obj_update_layout(root);
    lv_draw_buf_t *snap=lv_snapshot_take(root,LV_COLOR_FORMAT_RGB888);if(!snap)return ESP_ERR_NO_MEM;
    size_t row=(size_t)snap->header.w*3,size=row*snap->header.h;
    if(!row || size>4u*1024*1024){lv_draw_buf_destroy(snap);return ESP_ERR_INVALID_ARG;}
    uint8_t *pixels=lv_malloc(size);
    if(pixels){
        for(unsigned y=0;y<snap->header.h;++y)memcpy(pixels+y*row,snap->data+y*snap->header.stride,row);
        *image=(ouse_debug_image_t){pixels,size,(int)snap->header.w,(int)snap->header.h};
    }
    lv_draw_buf_destroy(snap);return pixels?ESP_OK:ESP_ERR_NO_MEM;
}
void ouse_debug_snapshot_free(ouse_debug_image_t *image)
{if(image){lv_free(image->pixels);memset(image,0,sizeof(*image));}}
