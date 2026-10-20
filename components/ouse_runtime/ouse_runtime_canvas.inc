// Worker only: a complete display list is validated before publication.
// A newer unpublished frame replaces the old one: queues cannot grow unbounded.
static bool canvas_number(JSContext *ctx,JSValueConst obj,const char *key,
                          int *out,int fallback,int min,int max)
{
    JSValue value=JS_GetPropertyStr(ctx,obj,key);
    double number=fallback;
    bool ok=JS_IsUndefined(value) || (JS_IsNumber(value) &&
        !JS_ToFloat64(ctx,&number,value) && isfinite(number) && floor(number)==number);
    JS_FreeValue(ctx,value);
    if(!ok || number<min || number>max)return false;
    *out=(int)number;return true;
}
static JSValue canvas_submit(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(ctx);char id[32];
    if(app->canvas_submitting)return JS_ThrowTypeError(ctx,"Recursive canvas submission");
    if(argc!=2 || !copy_string(ctx,argv[0],id,sizeof(id)) || !JS_IsArray(argv[1]))
        return JS_ThrowTypeError(ctx,"canvas.submit expects canvas id and command array");
    int length;
    if(!canvas_number(ctx,argv[1],"length",&length,0,0,CANVAS_MAX_COMMANDS))
        return JS_ThrowRangeError(ctx,"At most 256 canvas commands per frame");
    // Resolve the target before allocating, without exposing a pointer to JS.
    model_lock(app);unsigned index=0;
    while(index<app->node_count && strcmp(app->nodes[index].model.id,id))++index;
    bool active=index<app->node_count && app->nodes[index].model.type==CANVAS &&
        !app->closed && !app->faulted && app->navigation_stage==NAV_IDLE;
    model_unlock(app);
    if(!active)return JS_ThrowRangeError(ctx,"Invalid canvas or inactive page");
    canvas_frame_t *frame=ouse_psram_calloc(1,canvas_frame_bytes((unsigned)length));
    if(!frame)return JS_ThrowOutOfMemory(ctx);
    app->canvas_submitting=true;
    frame->count=(unsigned)length;bool valid=true;
    const char *ops[]={"fillRect","strokeRect","drawLine","drawArc","drawImage","drawText","fillTriangle"};
    for(unsigned i=0;i<frame->count && valid;++i){
        JSValue item=JS_GetPropertyUint32(ctx,argv[1],i),value=JS_GetPropertyStr(ctx,item,"op");
        canvas_command_t *c=&frame->commands[i];char op[24];
        valid=JS_IsObject(item) && !JS_IsArray(item) && copy_string(ctx,value,op,sizeof(op));
        JS_FreeValue(ctx,value);unsigned kind=0;
        while(kind<sizeof(ops)/sizeof(ops[0]) && (!valid || strcmp(op,ops[kind])))++kind;
        valid=valid && kind<sizeof(ops)/sizeof(ops[0]);c->op=(canvas_op_t)kind;
        int color=0,opacity=255;
#define CN(key,field,def,lo,hi) (valid=valid && canvas_number(ctx,item,key,&c->field,def,lo,hi))
        CN("x",x,0,-16384,16384);CN("y",y,0,-16384,16384);
        valid=valid && canvas_number(ctx,item,"color",&color,0xffffff,0,0xffffff) &&
            canvas_number(ctx,item,"opacity",&opacity,255,0,255);
        c->color=(uint32_t)color;c->opacity=(unsigned)opacity;
        if(c->op==CANVAS_FILL || c->op==CANVAS_STROKE || c->op==CANVAS_IMAGE || c->op==CANVAS_TEXT){
            CN("width",w,1,1,4096);CN("height",h,1,1,4096);
        }
        if(c->op==CANVAS_FILL || c->op==CANVAS_STROKE)CN("radius",radius,0,0,2048);
        if(c->op==CANVAS_STROKE || c->op==CANVAS_LINE || c->op==CANVAS_ARC)CN("lineWidth",line_width,1,1,128);
        if(c->op==CANVAS_LINE || c->op==CANVAS_TRIANGLE){CN("x2",x2,0,-16384,16384);CN("y2",y2,0,-16384,16384);}
        if(c->op==CANVAS_TRIANGLE){CN("x3",x3,0,-16384,16384);CN("y3",y3,0,-16384,16384);}
        if(c->op==CANVAS_ARC){CN("radius",radius,1,1,2048);CN("startAngle",start,0,0,360);CN("endAngle",end,360,0,360);}
        if(c->op==CANVAS_TEXT){
            value=JS_GetPropertyStr(ctx,item,"text");valid=valid && copy_string(ctx,value,c->text,sizeof(c->text));JS_FreeValue(ctx,value);
        }
        if(c->op==CANVAS_IMAGE){
            char src[32];value=JS_GetPropertyStr(ctx,item,"src");
            valid=valid && copy_string(ctx,value,src,sizeof(src)) && src[0]!='@';JS_FreeValue(ctx,value);
            c->image=valid ? find_image(app,src) : NULL;valid=valid && c->image;
            if(valid){
                CN("sourceX",sx,0,0,c->image->header.w-1);CN("sourceY",sy,0,0,c->image->header.h-1);
                CN("sourceWidth",sw,c->image->header.w-c->sx,1,c->image->header.w-c->sx);
                CN("sourceHeight",sh,c->image->header.h-c->sy,1,c->image->header.h-c->sy);
                // Bound scale to values representable by LVGL, including dp conversion.
                valid=valid && c->w*256/c->sw>=1 && c->h*256/c->sh>=1;
            }
        }
#undef CN
        JS_FreeValue(ctx,item);
    }
    app->canvas_submitting=false;
    if(!valid){free(frame);if(JS_HasException(ctx))return JS_EXCEPTION;return JS_ThrowTypeError(ctx,"Invalid canvas command");}
    model_lock(app);
    active=!app->closed && !app->faulted && app->navigation_stage==NAV_IDLE;
    canvas_frame_t *old=NULL;
    if(active){old=app->nodes[index].canvas_pending;app->nodes[index].canvas_pending=frame;}
    model_unlock(app);free(old);
    if(!active){free(frame);return JS_ThrowRangeError(ctx,"Inactive page");}
    return JS_UNDEFINED;
}
