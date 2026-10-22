// SPDX-License-Identifier: MIT
// The IDF 6.1 P4 startup PMP marks the ordinary PSRAM heap RW, never executable.
// Keep those protections and give the loader a separate MMU alias for its one
// owned image, beyond the statically protected heap mapping. All relocations
// and module data accesses then use the same alias. No SDK/managed source edits.
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_mmu_map.h"
#include "esp_cache.h"
#include "esp_log.h"

void *__real_esp_elf_malloc(uint32_t bytes,bool executable);
void __real_esp_elf_free(void *pointer);
static struct {void *backing,*alias;size_t size;} image;
bool ouse_native_memory_retained(void){return image.backing!=NULL;}

void *__wrap_esp_elf_malloc(uint32_t bytes,bool executable)
{
    if(!executable)return __real_esp_elf_malloc(bytes,false);
    if(image.backing || !bytes || bytes>8u*1024u*1024u)return NULL;
    const size_t page=CONFIG_MMU_PAGE_SIZE;
    size_t size=((size_t)bytes+page-1)&~(page-1);
    void *backing=heap_caps_aligned_alloc(page,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!backing)return NULL;
    esp_paddr_t physical;mmu_target_t target;void *alias=NULL;
    esp_err_t error=esp_mmu_vaddr_to_paddr(backing,&physical,&target);
    if(error==ESP_OK && target!=MMU_TARGET_PSRAM0)error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK)error=esp_mmu_map(physical,size,target,
        MMU_MEM_CAP_EXEC|MMU_MEM_CAP_READ|MMU_MEM_CAP_32BIT,ESP_MMU_MMAP_FLAG_PADDR_SHARED,&alias);
    if(error!=ESP_OK){
        ESP_LOGE("OuseNative","Executable PSRAM mapping failed: %s",esp_err_to_name(error));
        heap_caps_free(backing);return NULL;
    }
    image.backing=backing;image.alias=alias;image.size=size;
    ESP_LOGI("OuseNative","Executable PSRAM: backing=%p alias=%p size=%zu",backing,alias,size);
    return alias;
}

void __wrap_esp_elf_free(void *pointer)
{
    if(!pointer || pointer!=image.alias){__real_esp_elf_free(pointer);return;}
    // Flush dirty alias cache lines before the backing pages can be reused.
    esp_err_t error=esp_cache_msync(image.alias,image.size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    if(error==ESP_OK)error=esp_mmu_unmap(image.alias);
    if(error!=ESP_OK){
        // A failed unmap must never leave an executable alias to reused pages.
        // Retain ownership; future executable allocations fail until reboot.
        ESP_LOGE("OuseNative","Executable image retained after unmap failure: %s",esp_err_to_name(error));return;
    }
    heap_caps_free(image.backing);image.backing=NULL;image.alias=NULL;image.size=0;
}
