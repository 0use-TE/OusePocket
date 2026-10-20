#include "quickjs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define OUSE_FILES_PATH_MAX 128
#define OUSE_RUNTIME_ASSETS_MAX 16
typedef struct{const char *id;const lv_image_dsc_t *image;} ouse_script_image_t;
typedef struct{const char *id;} asset_t;
typedef struct{bool files_allowed,ready,closed,faulted,validation_only;unsigned image_count,asset_count;size_t sd_image_bytes;ouse_script_image_t images[16];asset_t assets[16];char image_ids[16][32];lv_image_dsc_t image_descriptors[16];} instance_t;
static instance_t app;
static bool fail_alloc,close_during_read;
static unsigned leases;
static instance_t *from_js(JSContext *c){(void)c;return &app;}
static void model_lock(instance_t *a){(void)a;}
static void model_unlock(instance_t *a){(void)a;}
static bool copy_string(JSContext *c,JSValueConst v,char *s,size_t cap){if(!JS_IsString(v))return false;size_t n;const char *p=JS_ToCStringLen(c,&n,v);if(!p)return false;bool ok=n<cap&&!memchr(p,0,n);if(ok)memcpy(s,p,n+1);JS_FreeCString(c,p);return ok;}
static bool valid_asset_id(const char *id){if(!id||!id[0]||strlen(id)>31)return false;for(unsigned i=0;id[i];i++){char c=id[i];bool alpha=(c>='a'&&c<='z')||(c>='A'&&c<='Z');if(!alpha&&(!i||!((c>='0'&&c<='9')||c=='_'||c=='-')))return false;}return true;}
static bool ouse_files_path(const char *p,char *out,size_t cap){if(strncmp(p,"Pictures/",9)||strstr(p,"..")||strchr(p+9,'/'))return false;return snprintf(out,cap,"%s",p)>0;}
static FILE *ouse_files_open(const char *p){(void)p;FILE *f=fopen("test-image.bin","rb");if(f)leases++;return f;}
static int ouse_files_close(FILE *f){leases--;if(close_during_read)app.closed=true;return fclose(f);}
static void *ouse_psram_malloc(size_t n){return fail_alloc?NULL:malloc(n);}
#include "ouse_runtime_sd_images.inc"
static void reset(void){for(unsigned i=0;i<app.image_count;i++)free((void *)app.image_descriptors[i].data);memset(&app,0,sizeof(app));app.files_allowed=true;fail_alloc=close_during_read=false;assert(!leases);}
static void file(unsigned w,unsigned h,int delta){FILE *f=fopen("test-image.bin","wb");assert(f);unsigned char header[]={ 'O','I','C','1',w&255,w>>8,h&255,h>>8};assert(fwrite(header,1,8,f)==8);size_t n=(size_t)w*h*4;if(delta<0)n--;if(delta>0)n++;for(size_t i=0;i<n;i++)fputc((unsigned char)i,f);fclose(f);}
static bool call(JSContext *c,const char *id,const char *path){JSValue args[]={JS_NewString(c,id),JS_NewString(c,path)};JSValue v=assets_load_image(c,JS_UNDEFINED,2,args);bool ok=!JS_IsException(v)&&JS_ToBool(c,v)==1;if(JS_IsException(v))JS_FreeValue(c,JS_GetException(c));JS_FreeValue(c,v);JS_FreeValue(c,args[0]);JS_FreeValue(c,args[1]);assert(!leases);return ok;}
int main(void){JSRuntime *r=JS_NewRuntime();JSContext *c=JS_NewContext(r);reset();file(3,2,0);assert(call(c,"village","Pictures/test.bin"));assert(app.image_count==1&&app.sd_image_bytes==24);assert(app.images[0].image->header.w==3&&app.images[0].image->data[23]==23);assert(!call(c,"village","Pictures/test.bin"));
 reset();assert(!call(c,"bad/id","Pictures/test.bin"));assert(!call(c,"x","/sdcard/test.bin"));assert(!call(c,"x","Pictures/../test.bin"));
 app.files_allowed=false;assert(!call(c,"x","Pictures/test.bin"));app.files_allowed=true;app.ready=true;assert(!call(c,"x","Pictures/test.bin"));app.ready=false;app.validation_only=true;assert(!call(c,"x","Pictures/test.bin"));reset();app.closed=true;assert(!call(c,"x","Pictures/test.bin"));reset();
 app.asset_count=16;for(unsigned i=0;i<16;i++)app.assets[i].id="occupied";assert(!call(c,"x","Pictures/test.bin"));reset();app.asset_count=1;app.assets[0].id="x";assert(!call(c,"x","Pictures/test.bin"));reset();
 for(int delta=-1;delta<=1;delta+=2){file(3,2,delta);assert(!call(c,"x","Pictures/test.bin"));assert(!app.image_count);}
 file(0,2,0);assert(!call(c,"x","Pictures/test.bin"));file(2049,1,0);assert(!call(c,"x","Pictures/test.bin"));file(3,2,0);fail_alloc=true;assert(!call(c,"x","Pictures/test.bin"));reset();close_during_read=true;assert(!call(c,"x","Pictures/test.bin"));assert(!app.image_count);reset();
 app.sd_image_bytes=SD_IMAGE_BYTES_MAX-16;assert(!call(c,"x","Pictures/test.bin"));reset();file(2048,512,0);assert(call(c,"large","Pictures/test.bin"));assert(app.sd_image_bytes==SD_IMAGE_BYTES_MAX);file(1,1,0);assert(!call(c,"next","Pictures/test.bin"));reset();
 JS_FreeContext(c);JS_FreeRuntime(r);remove("test-image.bin");puts("PASS production SD image loader: pixels/dimensions, immutable IDs, permissions/startup/path scope, truncation/trailing bytes, allocation/close cleanup, slots and 4MiB budget");return 0;}

