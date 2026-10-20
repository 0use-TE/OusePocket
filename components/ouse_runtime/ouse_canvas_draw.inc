// UI only. LVGL snapshots the layer clip into each draw task. This private
// LVGL 9 interface is isolated here and covered by atlas/pixel regression tests.
#include "src/draw/lv_draw_private.h"
#include "src/misc/lv_area_private.h"
static lv_area_t canvas_command_area(instance_t *app,const canvas_command_t *c,const lv_area_t *view)
{
    int x=c->x,y=c->y,x2=x+c->w-1,y2=y+c->h-1,pad=0;
    if(c->op==CANVAS_LINE || c->op==CANVAS_TRIANGLE){
        x=LV_MIN(x,c->x2);y=LV_MIN(y,c->y2);x2=LV_MAX(c->x,c->x2);y2=LV_MAX(c->y,c->y2);
        if(c->op==CANVAS_TRIANGLE){x=LV_MIN(x,c->x3);y=LV_MIN(y,c->y3);x2=LV_MAX(x2,c->x3);y2=LV_MAX(y2,c->y3);pad=1;}
        else pad=c->line_width;
    }else if(c->op==CANVAS_ARC){x-=c->radius;y-=c->radius;x2=c->x+c->radius;y2=c->y+c->radius;pad=1;}
    lv_area_t result={view->x1+units(app,x-pad),view->y1+units(app,y-pad),
        view->x1+units(app,x2+pad+1)-1,view->y1+units(app,y2+pad+1)-1};
    return result;
}
static void canvas_pump(instance_t *app,node_t *node)
{
    model_lock(app);canvas_frame_t *next=node->canvas_pending,*old=NULL;
    if(next){old=node->canvas_render;node->canvas_render=next;node->canvas_pending=NULL;}
    model_unlock(app);
    if(!next)return;
    // LVGL 9.5 has 32 invalid-area slots. Exceeding them forces a whole
    // DISPLAY refresh, even when the Canvas occupies only part of the screen.
    // Collapse dense changes into one Canvas-sized invalidation first.
    unsigned changed=0;
    if(old && old->count==next->count)
        for(unsigned i=0;i<next->count;++i)changed+=memcmp(&old->commands[i],&next->commands[i],sizeof(canvas_command_t))!=0;
    if(!old || old->count!=next->count || changed>32)lv_obj_invalidate(node->object);
    else {
        lv_area_t view;lv_obj_get_coords(node->object,&view);
        for(unsigned i=0;i<next->count;++i){
            const canvas_command_t *a=&old->commands[i],*b=&next->commands[i];
            if(!memcmp(a,b,sizeof(*a)))continue;
            lv_area_t before=canvas_command_area(app,a,&view),after=canvas_command_area(app,b,&view),damage,clip;
            lv_area_join(&damage,&before,&after);
            if(lv_area_intersect(&clip,&view,&damage))lv_obj_invalidate_area(node->object,&clip);
        }
    }
    free(old);
}
static void canvas_draw_event(lv_event_t *event)
{
    node_t *node=lv_event_get_user_data(event);instance_t *app=node->canvas_owner;
    canvas_frame_t *frame=node->canvas_render;if(!frame)return;
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t bounds;
    lv_obj_get_coords(node->object,&bounds);
    lv_area_t saved=layer->_clip_area,clip;
    if(!lv_area_intersect(&clip,&saved,&bounds))return;
    layer->_clip_area=clip;
    for(unsigned i=0;i<frame->count;++i){
        const canvas_command_t *c=&frame->commands[i];
        int x=bounds.x1+units(app,c->x),y=bounds.y1+units(app,c->y);
        int w=units(app,c->w),h=units(app,c->h);
        if(!c->opacity)continue;
        lv_area_t area={x,y,x+w-1,y+h-1};
        if(c->op==CANVAS_FILL || c->op==CANVAS_STROKE){
            lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=units(app,c->radius);
            if(c->op==CANVAS_FILL){d.bg_color=lv_color_hex(c->color);d.bg_opa=c->opacity;}
            else {d.bg_opa=0;d.border_color=lv_color_hex(c->color);d.border_opa=c->opacity;d.border_width=units(app,c->line_width);}
            lv_draw_rect(layer,&d,&area);
        }else if(c->op==CANVAS_LINE){
            lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(c->color);d.opa=c->opacity;d.width=units(app,c->line_width);
            d.p1.x=x;d.p1.y=y;d.p2.x=bounds.x1+units(app,c->x2);d.p2.y=bounds.y1+units(app,c->y2);lv_draw_line(layer,&d);
        }else if(c->op==CANVAS_ARC){
            lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.color=lv_color_hex(c->color);d.opa=c->opacity;d.width=units(app,c->line_width);
            d.center.x=x;d.center.y=y;d.radius=units(app,c->radius);d.start_angle=c->start;d.end_angle=c->end;lv_draw_arc(layer,&d);
        }else if(c->op==CANVAS_TRIANGLE){
            lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.color=lv_color_hex(c->color);d.opa=c->opacity;
            d.p[0].x=x;d.p[0].y=y;d.p[1].x=bounds.x1+units(app,c->x2);d.p[1].y=bounds.y1+units(app,c->y2);
            d.p[2].x=bounds.x1+units(app,c->x3);d.p[2].y=bounds.y1+units(app,c->y3);lv_draw_triangle(layer,&d);
        }else if(c->op==CANVAS_TEXT){
            lv_area_t text_clip;if(!lv_area_intersect(&text_clip,&clip,&area))continue;
            lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.text=c->text;d.text_local=1;
            d.font=lv_obj_get_style_text_font(node->object,0);d.color=lv_color_hex(c->color);d.opa=c->opacity;
            layer->_clip_area=text_clip;lv_draw_label(layer,&d,&area);layer->_clip_area=clip;
        }else if(c->op==CANVAS_IMAGE){
            lv_area_t image_clip;if(!lv_area_intersect(&image_clip,&clip,&area))continue;
            lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=c->image;d.opa=c->opacity;d.antialias=0;
            d.scale_x=w*256/c->sw;d.scale_y=h*256/c->sh;d.pivot.x=0;d.pivot.y=0;
            area.x1=x-c->sx*d.scale_x/256;area.y1=y-c->sy*d.scale_y/256;
            area.x2=area.x1+c->image->header.w-1;area.y2=area.y1+c->image->header.h-1;
            layer->_clip_area=image_clip;lv_draw_image(layer,&d,&area);layer->_clip_area=clip;
        }
    }
    layer->_clip_area=saved;
}
