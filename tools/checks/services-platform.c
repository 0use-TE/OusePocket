// Deterministic single-thread RTOS double. Production workers are stepped until
// queue idle; hardware, OS scheduling, TLS and flash timings are not simulated.
#include "platform.h"
#include "esp_err.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int64_t test_clock;
bool break_on_empty;
jmp_buf worker_exit;
void (*platform_hook)(void);
typedef struct {unsigned capacity,size,count;unsigned char data[16*512];} queue_t;
typedef struct {unsigned capacity,count;unsigned char data[8192];} buffer_t;
QueueHandle_t xQueueCreate(unsigned capacity,unsigned size)
{assert(capacity<=16 && capacity*size<=8192);queue_t *q=calloc(1,sizeof(*q));q->capacity=capacity;q->size=size;return q;}
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait)
{(void)wait;queue_t *q=queue;assert(q);if(q->count==q->capacity)return 0;memcpy(q->data+q->count++*q->size,item,q->size);return 1;}
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait)
{(void)wait;queue_t *q=queue;assert(q);if(!q->count){if(break_on_empty)longjmp(worker_exit,1);return 0;}memcpy(item,q->data,q->size);memmove(q->data,q->data+q->size,--q->count*q->size);return 1;}
void vQueueDelete(QueueHandle_t q){free(q);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){int *p=malloc(sizeof(*p));*p=1;return p;}
SemaphoreHandle_t xSemaphoreCreateBinary(void){int *p=malloc(sizeof(*p));*p=0;return p;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;if(s && !*(int*)s && platform_hook)platform_hook();assert(s && *(int*)s==1);*(int*)s=0;return 1;}
int xSemaphoreGive(SemaphoreHandle_t s){assert(s && !*(int*)s);*(int*)s=1;return 1;}
void vSemaphoreDelete(SemaphoreHandle_t s){free(s);}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *task)
{(void)fn;(void)name;(void)arg;(void)priority;(void)task;assert(stack>=4096);return pdPASS;}
void vTaskDelay(unsigned ticks){test_clock+=(int64_t)ticks*1000;if(platform_hook)platform_hook();}
int64_t esp_timer_get_time(void){return test_clock;}
StreamBufferHandle_t xStreamBufferCreate(unsigned size,unsigned trigger)
{assert(size<=8192 && trigger==2);buffer_t *b=calloc(1,sizeof(*b));b->capacity=size;return b;}
size_t xStreamBufferSend(StreamBufferHandle_t h,const void *data,size_t size,unsigned wait)
{buffer_t *b=h;if(b->count==b->capacity){vTaskDelay(wait);}size_t n=b->capacity-b->count;if(n>size)n=size;memcpy(b->data+b->count,data,n);b->count+=n;return n;}
size_t xStreamBufferReceive(StreamBufferHandle_t h,void *data,size_t size,unsigned wait)
{buffer_t *b=h;if(!b->count)vTaskDelay(wait);size_t n=b->count<size ? b->count : size;memcpy(data,b->data,n);memmove(b->data,b->data+n,b->count-=n);return n;}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t h){return ((buffer_t*)h)->count;}
int xStreamBufferReset(StreamBufferHandle_t h){((buffer_t*)h)->count=0;return 1;}
void vStreamBufferDelete(StreamBufferHandle_t h){free(h);}
const char *esp_err_to_name(int e){return e==ESP_OK ? "ESP_OK" : "TEST_ERROR";}
uint32_t esp_rom_crc32_le(uint32_t crc,const unsigned char *bytes,size_t size)
{crc=~crc;while(size--){crc^=*bytes++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((0-(crc&1))&0xedb88320);}return ~crc;}
