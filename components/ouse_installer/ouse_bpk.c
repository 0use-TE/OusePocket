// SPDX-License-Identifier: MIT
#include "ouse_bpk.h"
#include "ouse_protocol.h"
#include "quickjs.h"
#include "ouse_memory.h"
#include <stdlib.h>
#include <string.h>

static unsigned u16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const unsigned char *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
#define BPK_FILES_MAX (4 + OUSE_RUNTIME_ASSETS_MAX)
#define BPK_ASSET_IMAGE 1
#define BPK_ASSET_TEXT 2
#define BPK_ASSET_BINARY 3
typedef struct {
    const unsigned char *data;
    size_t size;
    unsigned offset;
    char name[OUSE_BPK_ASSET_PATH_MAX+1];
} member_t;

static bool utf8(const unsigned char *p,size_t n)
{
    for(size_t i=0;i<n;){
        unsigned a=p[i++],count,min=0x80,max=0xbf;
        if(a<0x80){if(!a)return false;continue;}
        if(a>=0xc2 && a<=0xdf)count=1;
        else if(a>=0xe0 && a<=0xef){count=2;if(a==0xe0)min=0xa0;if(a==0xed)max=0x9f;}
        else if(a>=0xf0 && a<=0xf4){count=3;if(a==0xf0)min=0x90;if(a==0xf4)max=0x8f;}
        else return false;
        if(count>n-i || p[i]<min || p[i]>max)return false;
        for(unsigned j=1;j<count;++j)if(p[i+j]<0x80 || p[i+j]>0xbf)return false;
        i+=count;
    }
    return true;
}

static bool asset_path(const char *path)
{
    if(strncmp(path,"assets/",7) || !path[7] || strlen(path)>OUSE_BPK_ASSET_PATH_MAX)return false;
    const char *part=path+7;
    for(const char *s=part;;++s){
        unsigned c=(unsigned char)*s;
        if(!c || c=='/'){
            size_t n=(size_t)(s-part);
            if(!n || (n==1 && part[0]=='.') || (n==2 && part[0]=='.' && part[1]=='.'))return false;
            if(!c)return true;
            part=s+1;
        }else if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='_' || c=='-'))return false;
    }
}
static int fixed_file(const char *name)
{
    static const char *names[]={"manifest.json","page.json","app.js","icon.bin"};
    for(unsigned i=0;i<4;++i)if(!strcmp(name,names[i]))return (int)i;
    return -1;
}
static int find_file(const member_t *files,unsigned count,const char *name)
{
    for(unsigned i=0;i<count;++i)if(!strcmp(files[i].name,name))return (int)i;
    return -1;
}
// One narrow ZIP profile: four fixed files plus declared assets. Stored regular
// files only; no directories, duplicates, extras, descriptors, ZIP64, comments,
// preambles or trailing bytes. Central/local headers and CRCs must agree.
static bool unzip(const unsigned char *p,size_t n,member_t *files,unsigned *file_count)
{
    if(!p || n<22 || n>OUSE_BPK_MAX)return false;
    const unsigned char *end=p+n-22;unsigned count=u16(end+10);
    if(u32(end)!=0x06054b50 || u16(end+4) || u16(end+6) ||
       count<4 || count>BPK_FILES_MAX || u16(end+8)!=count || u16(end+20))return false;
    size_t central=u32(end+16),central_size=u32(end+12);
    if(central>n-22 || central_size!=n-22-central)return false;
    size_t pos=0;unsigned fixed_seen=0;
    for(unsigned j=0;j<count;++j){
        if(pos>central || central-pos<30 || u32(p+pos)!=0x04034b50)return false;
        const unsigned char *h=p+pos;unsigned flags=u16(h+6),len=u16(h+26);
        uint32_t size=u32(h+18);
        if(!u16(h+4) || u16(h+4)>20 || (flags & ~0x800u) || u16(h+8) || u16(h+28) ||
           u32(h+22)!=size || !len || len>OUSE_BPK_ASSET_PATH_MAX || 30+len>central-pos || size>central-pos-30-len || memchr(h+30,0,len))return false;
        memcpy(files[j].name,h+30,len);files[j].name[len]=0;
        int index=fixed_file(files[j].name);
        if((index<0 && !asset_path(files[j].name)) || find_file(files,j,files[j].name)>=0)return false;
        size_t limit=index==0?OUSE_BPK_MANIFEST_MAX:index==3?OUSE_BPK_ICON_PIXELS_MAX+8:index>=0?32768:OUSE_RUNTIME_DATA_BYTES_MAX;
        if((index>=0 && !size) || size>limit)return false;
        if(index>=0)fixed_seen|=1u<<index;
        files[j].data=h+30+len;files[j].size=size;files[j].offset=(unsigned)pos;
        if((index>=0 && index!=3 && !utf8(files[j].data,size)) || ouse_crc32(files[j].data,size)!=u32(h+14))return false;
        pos+=30+len+size;
    }
    if(pos!=central || fixed_seen!=15)return false;
    unsigned seen=0,mask=(1u<<count)-1;
    for(unsigned j=0;j<count;++j){
        if(pos>n-22 || n-22-pos<46 || u32(p+pos)!=0x02014b50)return false;
        const unsigned char *h=p+pos;unsigned len=u16(h+28);
        if(!len || len>OUSE_BPK_ASSET_PATH_MAX || 46+len>n-22-pos || u16(h+30) || u16(h+32) || u16(h+34) || memchr(h+46,0,len))return false;
        char name[OUSE_BPK_ASSET_PATH_MAX+1];memcpy(name,h+46,len);name[len]=0;
        int index=find_file(files,count,name);
        if(index<0 || (seen&(1u<<index)))return false;
        seen|=1u<<index;
        const unsigned char *local=p+files[index].offset;
        unsigned mode=(u32(h+38)>>16)&0170000;
        if((mode && mode!=0100000) || (u32(h+38)&0x10) || u32(h+42)!=files[index].offset ||
           u16(h+6)!=u16(local+4) || u16(h+8)!=u16(local+6) || u16(h+10) ||
           u16(h+12)!=u16(local+10) || u16(h+14)!=u16(local+12) || u32(h+16)!=u32(local+14) ||
           u32(h+20)!=files[index].size || u32(h+24)!=files[index].size)return false;
        pos+=46+len;
    }
    *file_count=count;return pos==n-22 && seen==mask;
}

static bool field(JSContext *ctx,JSValue obj,const char *key,char *out,size_t cap)
{
    JSValue value=JS_GetPropertyStr(ctx,obj,key);size_t n=0;
    const char *s=JS_IsString(value)?JS_ToCStringLen(ctx,&n,value):NULL;
    bool ok=s && n && n<cap && utf8((const unsigned char *)s,n);
    if(ok){memcpy(out,s,n);out[n]=0;}
    if(s)JS_FreeCString(ctx,s);
    JS_FreeValue(ctx,value);return ok;
}
static bool integer(JSContext *ctx,JSValue obj,const char *key,int expected)
{
    JSValue value=JS_GetPropertyStr(ctx,obj,key);int32_t n=0;
    bool ok=JS_IsNumber(value) && JS_ToInt32(ctx,&n,value)==0 && n==expected;
    double exact=0;if(ok)ok=JS_ToFloat64(ctx,&exact,value)==0 && exact==expected;
    JS_FreeValue(ctx,value);return ok;
}
static bool localized_names(JSContext *ctx,JSValue doc,ouse_bpk_t *p)
{
    JSValue names=JS_GetPropertyStr(ctx,doc,"names");
    if(JS_IsUndefined(names)){JS_FreeValue(ctx,names);return true;}
    bool valid=JS_IsObject(names) && !JS_IsArray(names);
    JSPropertyEnum *keys=NULL;uint32_t count=0;
    if(valid)valid=JS_GetOwnPropertyNames(ctx,&keys,&count,names,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)==0;
    if(valid)for(uint32_t i=0;i<count;++i){
        JSValue key_value=JS_AtomToValue(ctx,keys[i].atom);size_t key_length=0;
        const char *key=JS_ToCStringLen(ctx,&key_length,key_value);
        if(!key || memchr(key,0,key_length))valid=false;
        else if(!strcmp(key,"zh_CN"))valid=field(ctx,names,key,p->name_zh,sizeof(p->name_zh));
        else if(!strcmp(key,"en"))valid=field(ctx,names,key,p->name_en,sizeof(p->name_en));
        else valid=false;
        if(key)JS_FreeCString(ctx,key);
        JS_FreeValue(ctx,key_value);
        if(!valid)break;
    }
    for(uint32_t i=0;i<count;++i)JS_FreeAtom(ctx,keys[i].atom);
    js_free(ctx,keys);JS_FreeValue(ctx,names);return valid;
}
static bool valid_id(const char *id)
{
    if(strncmp(id,"user.",5) || !id[5])return false;
    for(const char *s=id;*s;++s)if(!((*s>='a' && *s<='z') || (*s>='0' && *s<='9') || *s=='.' || *s=='_' || *s=='-'))return false;
    return true;
}
static bool capabilities(JSContext *ctx,JSValue doc,ouse_bpk_t *p)
{
    JSValue caps=JS_GetPropertyStr(ctx,doc,"capabilities");
    if(JS_IsUndefined(caps)){JS_FreeValue(ctx,caps);return true;}
    bool valid=JS_IsObject(caps) && !JS_IsArray(caps) && !JS_IsNull(caps);
    JSPropertyEnum *keys=NULL;uint32_t count=0;
    if(valid)valid=JS_GetOwnPropertyNames(ctx,&keys,&count,caps,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)==0;
    for(uint32_t i=0;valid && i<count;++i){
        const char *key=JS_AtomToCString(ctx,keys[i].atom);
        valid=key && (!strcmp(key,"background") || !strcmp(key,"notifications") || !strcmp(key,"network") || !strcmp(key,"media") || !strcmp(key,"alarms") || !strcmp(key,"files") || !strcmp(key,"sdcard"));
        if(key)JS_FreeCString(ctx,key);
    }
    JSValue background=JS_GetPropertyStr(ctx,caps,"background");
    valid=valid && (JS_IsUndefined(background) || JS_IsBool(background));
    if(valid)p->package.background_allowed=JS_ToBool(ctx,background)>0;
    JS_FreeValue(ctx,background);
    JSValue notifications=JS_GetPropertyStr(ctx,caps,"notifications");
    valid=valid && (JS_IsUndefined(notifications) || JS_IsBool(notifications));
    if(valid)p->package.notifications_allowed=JS_ToBool(ctx,notifications)>0;
    JS_FreeValue(ctx,notifications);
    const char *services[]={"network","media","alarms","files","sdcard"};
    bool *allowed[]={&p->package.network_allowed,&p->package.media_allowed,&p->package.alarms_allowed,&p->package.files_allowed,&p->package.sdcard_allowed};
    for(unsigned i=0;i<sizeof(services)/sizeof(services[0]);++i){JSValue value=JS_GetPropertyStr(ctx,caps,services[i]);
        valid=valid && (JS_IsUndefined(value) || JS_IsBool(value));
        if(valid)*allowed[i]=JS_ToBool(ctx,value)>0;
        JS_FreeValue(ctx,value);
    }
    for(uint32_t i=0;i<count;++i)JS_FreeAtom(ctx,keys[i].atom);
    js_free(ctx,keys);JS_FreeValue(ctx,caps);return valid;
}
static bool asset_id(const char *id)
{
    unsigned first=(unsigned char)*id;
    if(!((first>='A' && first<='Z') || (first>='a' && first<='z')))return false;
    for(const char *s=id+1;*s;++s){
        unsigned c=(unsigned char)*s;
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-'))return false;
    }
    return true;
}
static bool manifest_assets(JSContext *ctx,JSValue doc,ouse_bpk_t *p)
{
    JSValue list=JS_GetPropertyStr(ctx,doc,"assets");
    if(JS_IsUndefined(list)){JS_FreeValue(ctx,list);return true;}
    bool valid=JS_IsArray(list);
    JSValue length=JS_GetPropertyStr(ctx,list,"length");uint32_t count=0;
    valid=valid && JS_ToUint32(ctx,&count,length)==0 && count<=OUSE_RUNTIME_ASSETS_MAX;
    JS_FreeValue(ctx,length);
    for(unsigned i=0;valid && i<count;++i){
        JSValue item=JS_GetPropertyUint32(ctx,list,i);char type[16];
        ouse_bpk_asset_t *asset=&p->asset_storage[i];
        valid=JS_IsObject(item) && !JS_IsNull(item) && !JS_IsArray(item) &&
            field(ctx,item,"id",asset->id,sizeof(asset->id)) && asset_id(asset->id) &&
            field(ctx,item,"path",asset->path,sizeof(asset->path)) && asset_path(asset->path) &&
            field(ctx,item,"type",type,sizeof(type));
        JSPropertyEnum *keys=NULL;uint32_t key_count=0;
        if(valid)valid=JS_GetOwnPropertyNames(ctx,&keys,&key_count,item,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)==0 && key_count==3;
        for(uint32_t k=0;k<key_count;++k)JS_FreeAtom(ctx,keys[k].atom);
        js_free(ctx,keys);
        if(valid){
            asset->type=!strcmp(type,"image")?BPK_ASSET_IMAGE:!strcmp(type,"text")?BPK_ASSET_TEXT:!strcmp(type,"binary")?BPK_ASSET_BINARY:0;
            valid=asset->type!=0;
            for(unsigned j=0;valid && j<i;++j)valid=strcmp(p->asset_storage[j].id,asset->id) && strcmp(p->asset_storage[j].path,asset->path);
        }
        JS_FreeValue(ctx,item);
    }
    JS_FreeValue(ctx,list);if(valid)p->asset_count=count;
    return valid;
}
static bool image_info(const member_t *file,unsigned side,size_t maximum,unsigned *width,unsigned *height,size_t *pixels)
{
    if(file->size<8 || memcmp(file->data,"OIC1",4))return false;
    *width=u16(file->data+4);*height=u16(file->data+6);*pixels=(size_t)*width * *height * 4;
    return *width && *width<=side && *height && *height<=side && *pixels<=maximum && file->size==*pixels+8;
}
static lv_image_dsc_t image_descriptor(unsigned w,unsigned h,size_t pixels,const unsigned char *data)
{
    return (lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,
        .w=w,.h=h,.stride=w*4},.data_size=pixels,.data=data};
}
void ouse_bpk_free(ouse_bpk_t *p)
{
    // Installed images must first be detached and their LVGL caches dropped on
    // the UI task. Parsing failures have never exposed these images to LVGL.
    if(p){
        for(unsigned i=0;i<p->asset_count;++i)free(p->asset_storage[i].data);
        free(p->page);free(p->script);free(p->icon_pixels);free(p);
    }
}
esp_err_t ouse_bpk_parse(const unsigned char *bytes,size_t length,ouse_bpk_t **output)
{
    if(!output)return ESP_ERR_INVALID_ARG;
    *output=NULL;
    member_t *files=ouse_psram_calloc(BPK_FILES_MAX,sizeof(*files));if(!files)return ESP_ERR_NO_MEM;
    unsigned count=0;if(!unzip(bytes,length,files,&count)){free(files);return ESP_ERR_INVALID_ARG;}
    ouse_bpk_t *p=ouse_psram_calloc(1,sizeof(*p));if(!p){free(files);return ESP_ERR_NO_MEM;}
    esp_err_t error=ESP_ERR_INVALID_ARG;
    const member_t *manifest_file=&files[find_file(files,count,"manifest.json")];
    char *manifest=ouse_psram_malloc(manifest_file->size+1);
    if(!manifest){error=ESP_ERR_NO_MEM;goto fail;}
    memcpy(manifest,manifest_file->data,manifest_file->size);manifest[manifest_file->size]=0;
    JSRuntime *rt=JS_NewRuntime();if(!rt){free(manifest);error=ESP_ERR_NO_MEM;goto fail;}
    JS_SetMemoryLimit(rt,OUSE_RUNTIME_MEMORY_LIMIT);JS_SetMaxStackSize(rt,8*1024);
    JSContext *ctx=JS_NewContext(rt);bool valid=false;
    if(ctx){
        JSValue doc=JS_ParseJSON(ctx,manifest,manifest_file->size,"manifest.json");
        JSValue format=JS_IsException(doc)?JS_UNDEFINED:JS_GetPropertyStr(ctx,doc,"format");
        char runtime[24],icon[16];
        valid=!JS_IsException(doc) && JS_IsUndefined(format) && field(ctx,doc,"runtime",runtime,sizeof(runtime)) && !strcmp(runtime,"ouse-core") &&
            integer(ctx,doc,"api",1) && field(ctx,doc,"id",p->id,sizeof(p->id)) &&
            field(ctx,doc,"name",p->name,sizeof(p->name)) && field(ctx,doc,"icon",icon,sizeof(icon)) && valid_id(p->id);
        if(valid)valid=!strcmp(icon,"icon.bin") && localized_names(ctx,doc,p) && capabilities(ctx,doc,p) && manifest_assets(ctx,doc,p);
        p->package.icon=LV_SYMBOL_IMAGE;
        JS_FreeValue(ctx,format);JS_FreeValue(ctx,doc);JS_FreeContext(ctx);
    }
    JS_FreeRuntime(rt);free(manifest);
    if(!valid || count!=4+p->asset_count)goto fail;
    {
        const member_t *icon=&files[find_file(files,count,"icon.bin")];unsigned w,h;size_t pixels;
        if(!image_info(icon,OUSE_BPK_ICON_SIDE_MAX,OUSE_BPK_ICON_PIXELS_MAX,&w,&h,&pixels))goto fail;
        p->icon_pixels=ouse_psram_malloc(pixels);if(!p->icon_pixels){error=ESP_ERR_NO_MEM;goto fail;}
        memcpy(p->icon_pixels,icon->data+8,pixels);
        p->icon_image=image_descriptor(w,h,pixels,p->icon_pixels);p->package.icon_image=&p->icon_image;
    }
    size_t image_bytes=0,data_bytes=0;
    for(unsigned i=0;i<p->asset_count;++i){
        ouse_bpk_asset_t *asset=&p->asset_storage[i];int index=find_file(files,count,asset->path);
        if(index<0)goto fail;
        const member_t *file=&files[index];const unsigned char *source=file->data;size_t size=file->size;
        unsigned w=0,h=0;
        if(asset->type==BPK_ASSET_IMAGE){
            if(!image_info(file,512,OUSE_RUNTIME_IMAGE_BYTES_MAX,&w,&h,&size))goto fail;
            image_bytes+=size;if(image_bytes>OUSE_RUNTIME_IMAGE_BYTES_MAX)goto fail;
            source+=8;
        }else{
            size_t maximum=asset->type==BPK_ASSET_TEXT?16384:OUSE_RUNTIME_DATA_BYTES_MAX;
            if(size>maximum || (asset->type==BPK_ASSET_TEXT && !utf8(source,size)))goto fail;
            data_bytes+=size;if(data_bytes>OUSE_RUNTIME_DATA_BYTES_MAX)goto fail;
        }
        asset->data=ouse_psram_malloc(size+1);if(!asset->data){error=ESP_ERR_NO_MEM;goto fail;}
        memcpy(asset->data,source,size);asset->data[size]=0;asset->size=size;
        if(asset->type==BPK_ASSET_IMAGE){
            asset->image=image_descriptor(w,h,size,asset->data);
            p->images[p->package.image_count++]=(ouse_script_image_t){asset->id,&asset->image};
        }else p->assets[p->package.asset_count++]=(ouse_script_asset_t){.id=asset->id,
            .type=asset->type==BPK_ASSET_TEXT?OUSE_SCRIPT_ASSET_TEXT:OUSE_SCRIPT_ASSET_BINARY,.data=asset->data,.size=size};
    }
    const member_t *page=&files[find_file(files,count,"page.json")],*script=&files[find_file(files,count,"app.js")];
    p->page=ouse_psram_malloc(page->size+1);p->script=ouse_psram_malloc(script->size+1);
    if(!p->page || !p->script){error=ESP_ERR_NO_MEM;goto fail;}
    memcpy(p->page,page->data,page->size);p->page[page->size]=0;
    memcpy(p->script,script->data,script->size);p->script[script->size]=0;
    p->package.id=p->id;p->package.name=p->name;p->package.page=p->page;p->package.script=p->script;p->package.api=1;
    p->package.name_zh=*p->name_zh?p->name_zh:NULL;p->package.name_en=*p->name_en?p->name_en:NULL;
    p->package.images=p->images;p->package.assets=p->assets;
    error=ouse_script_prepare(&p->state,&p->package,&p->definition);
    if(error!=ESP_OK)goto fail;
    free(files);*output=p;return ESP_OK;
fail:
    free(files);ouse_bpk_free(p);return error;
}
