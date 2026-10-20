// Startup only: publish immutable descriptors before exposing their IDs.
#define SD_IMAGE_BYTES_MAX (4U*1024*1024)
static bool valid_asset_id(const char *id);
static JSValue assets_load_image(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv)
{
    (void)self;instance_t *app=from_js(ctx);char id[32],path[OUSE_FILES_PATH_MAX];
    if(argc!=2 || !copy_string(ctx,argv[0],id,sizeof(id)) || !valid_asset_id(id) ||
       !copy_string(ctx,argv[1],path,sizeof(path)))return JS_ThrowTypeError(ctx,"loadImage expects image id and scoped SD path");
    char absolute[180];
    if(!ouse_files_path(path,absolute,sizeof(absolute)))return JS_FALSE;
    model_lock(app);
    bool allowed=app->files_allowed && !app->ready && !app->closed && !app->faulted && !app->validation_only &&
        app->image_count+app->asset_count<OUSE_RUNTIME_ASSETS_MAX;
    for(unsigned i=0;i<app->image_count;++i)if(!strcmp(id,app->images[i].id))allowed=false;
    for(unsigned i=0;i<app->asset_count;++i)if(!strcmp(id,app->assets[i].id))allowed=false;
    model_unlock(app);if(!allowed)return JS_FALSE;
    FILE *file=ouse_files_open(path);if(!file)return JS_FALSE;
    unsigned char header[8];unsigned char *pixels=NULL;bool valid=fread(header,1,8,file)==8 && !memcmp(header,"OIC1",4);
    unsigned w=valid ? header[4]|((unsigned)header[5]<<8) : 0,h=valid ? header[6]|((unsigned)header[7]<<8) : 0;
    size_t size=(size_t)w*h*4;
    valid=valid && w && h && w<=2048 && h<=2048 && size<=SD_IMAGE_BYTES_MAX-app->sd_image_bytes;
    if(valid){pixels=ouse_psram_malloc(size);valid=pixels && fread(pixels,1,size,file)==size && fgetc(file)==EOF && !ferror(file);}
    if(ouse_files_close(file))valid=false;
    if(!valid){free(pixels);return JS_FALSE;}
    model_lock(app);
    if(app->closed || app->faulted){model_unlock(app);free(pixels);return JS_FALSE;}
    unsigned slot=app->image_count;strcpy(app->image_ids[slot],id);
    app->image_descriptors[slot]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,
        .w=w,.h=h,.stride=w*4},.data_size=size,.data=pixels};
    app->images[slot]=(ouse_script_image_t){.id=app->image_ids[slot],.image=&app->image_descriptors[slot]};
    app->sd_image_bytes+=size;
    // Atomic count publishes a fully initialized slot to the UI. No replacement.
    ++app->image_count;model_unlock(app);return JS_TRUE;
}
