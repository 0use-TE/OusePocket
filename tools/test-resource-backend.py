"""Compile the unchanged resource backend against thin, counted SDK substitutes.

Uses real Boost JSON and the full product translation unit, not copied formulas.
Hardware counter accuracy, heap allocator internals and mounted media remain
device checks. The app's ten-second storage polling policy is covered by the
separate real-JavaScript test; the native storage API intentionally has no timer.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples/brookesia-minimal/build/host-resource-backend'
OUT.mkdir(parents=True, exist_ok=True)
SDK = OUT / 'sdk'
SDK.mkdir(exist_ok=True)

vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([
    str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
], text=True).strip()
script = OUT / 'environment.cmd'
script.write_text(
    f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n', encoding='mbcs'
)
env = dict(os.environ)
for line in subprocess.check_output(['cmd.exe', '/d', '/c', str(script)], encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value

source = ROOT / 'components/pocket_platform/pocket_resources.cpp'
for name in re.findall(r'^#include "((?:esp_|freertos/)[^"]+)"', source.read_text(encoding='utf-8'), re.M):
    header = SDK / name
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('// SDK declarations supplied by sdk.hpp.\n', encoding='utf-8')

(SDK / 'sdk.hpp').write_text(r'''
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#ifndef NO_RUNTIME_STATS
#define NO_RUNTIME_STATS 0
#endif
#define CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS (!NO_RUNTIME_STATS)
#define CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER 1
#define CONFIG_FREERTOS_SMP 0
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define CONFIG_IDF_TARGET "esp32p4"
#define portNUM_PROCESSORS 2
#ifdef COUNTER_U32
using configRUN_TIME_COUNTER_TYPE=uint32_t;
#else
using configRUN_TIME_COUNTER_TYPE=uint64_t;
#endif
constexpr int ESP_OK=0, ESP_FAIL=-1;
constexpr uint32_t MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4;
constexpr int SOC_MOD_CLK_CPU=1, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED=2, CHIP_ESP32P4=18;
enum esp_reset_reason_t {
 ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC,
 ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT,
 ESP_RST_SDIO, ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP
};
struct esp_chip_info_t {int model;uint16_t revision;uint8_t cores;};
struct esp_app_desc_t {const char *version,*idf_ver,*project_name;};
struct esp_partition_t {int type;const char *label;uint32_t address,size;};
struct esp_partition_pos_t {uint32_t offset,size;};
struct esp_image_metadata_t {uint32_t image_len;};
constexpr int ESP_PARTITION_TYPE_ANY=-1, ESP_PARTITION_SUBTYPE_ANY=-1;
constexpr int ESP_PARTITION_TYPE_APP=0, ESP_PARTITION_TYPE_DATA=1;
struct MockIterator {size_t index=0;};
using esp_partition_iterator_t=MockIterator*;

namespace sdk {
inline int64_t now=1000000;
inline std::array<uint64_t,2> idle{UINT32_MAX-100000ULL,UINT32_MAX-100000ULL};
inline std::array<size_t,2> total{512*1024,32*1024*1024},free{256*1024,26*1024*1024};
inline std::array<size_t,2> minimum{192*1024,24*1024*1024},largest{128*1024,20*1024*1024};
inline std::array<int,2> largest_calls{};
inline int heap_reads=0,idle_reads=0,clock_reads=0,chip_reads=0;
inline int physical_reads=0,configured_reads=0,partition_reads=0,iterator_releases=0,image_reads=0;
inline int littlefs_reads=0,sd_reads=0;
inline uint32_t frequency=360000000,image_size=6500000;
inline bool frequency_ok=true,flash_ok=true,image_ok=true,littlefs_ok=true,sd_ok=true;
inline bool iterator_live=false;
inline size_t littlefs_total=8323072,littlefs_used=2097152;
inline uint64_t sd_total=64ULL*1024*1024*1024,sd_free=40ULL*1024*1024*1024;
inline std::array<esp_partition_t,3> partitions{{
 {ESP_PARTITION_TYPE_APP,"factory",65536,8388608},
 {ESP_PARTITION_TYPE_DATA,"littlefs_data",8454144,8323072},
 {99,"other",0,4096}
}};
inline MockIterator iterator;
inline const esp_app_desc_t app{"resource-test","v6.1","ouse_brookesia_minimal"};
inline void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
inline size_t heap_index(uint32_t caps) {
 require(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT) || caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
         "Heap capability masks mix internal RAM and PSRAM");
 return caps & MALLOC_CAP_INTERNAL?0:1;
}
}

inline int64_t esp_timer_get_time(){return sdk::now;}
inline void esp_chip_info(esp_chip_info_t *chip){++sdk::chip_reads;*chip={CHIP_ESP32P4,302,2};}
inline const esp_app_desc_t *esp_app_get_description(){return &sdk::app;}
inline esp_reset_reason_t esp_reset_reason(){return ESP_RST_POWERON;}
inline int esp_clk_tree_src_get_freq_hz(int source,int precision,uint32_t *value){
 sdk::require(source==SOC_MOD_CLK_CPU && precision==ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,"Wrong CPU clock source");
 ++sdk::clock_reads;if(!sdk::frequency_ok)return ESP_FAIL;*value=sdk::frequency;return ESP_OK;
}
inline size_t heap_caps_get_total_size(uint32_t caps){++sdk::heap_reads;return sdk::total[sdk::heap_index(caps)];}
inline size_t heap_caps_get_free_size(uint32_t caps){++sdk::heap_reads;return sdk::free[sdk::heap_index(caps)];}
inline size_t heap_caps_get_minimum_free_size(uint32_t caps){++sdk::heap_reads;return sdk::minimum[sdk::heap_index(caps)];}
inline size_t heap_caps_get_largest_free_block(uint32_t caps){const auto i=sdk::heap_index(caps);++sdk::largest_calls[i];return sdk::largest[i];}
inline configRUN_TIME_COUNTER_TYPE ulTaskGetIdleRunTimeCounterForCore(int core){
 sdk::require(core>=0 && core<2,"Wrong core index");++sdk::idle_reads;return static_cast<configRUN_TIME_COUNTER_TYPE>(sdk::idle[core]);
}
inline int esp_flash_get_physical_size(void*,uint32_t *value){++sdk::physical_reads;if(!sdk::flash_ok)return ESP_FAIL;*value=32*1024*1024;return ESP_OK;}
inline int esp_flash_get_size(void*,uint32_t *value){++sdk::configured_reads;if(!sdk::flash_ok)return ESP_FAIL;*value=16*1024*1024;return ESP_OK;}
inline esp_partition_iterator_t esp_partition_find(int type,int subtype,const char *label){
 sdk::require(type==ESP_PARTITION_TYPE_ANY && subtype==ESP_PARTITION_SUBTYPE_ANY && !label,"Partition scan filtered unexpectedly");
 sdk::require(!sdk::iterator_live,"Previous partition iterator leaked");++sdk::partition_reads;sdk::iterator_live=true;
 sdk::iterator.index=0;return &sdk::iterator;
}
inline const esp_partition_t *esp_partition_get(esp_partition_iterator_t iterator){return &sdk::partitions.at(iterator->index);}
inline esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t iterator){
 if(++iterator->index<sdk::partitions.size())return iterator;
 sdk::require(sdk::iterator_live,"Partition iterator released twice");sdk::iterator_live=false;++sdk::iterator_releases;return nullptr;
}
inline const esp_partition_t *esp_ota_get_running_partition(){return &sdk::partitions[0];}
inline int esp_image_get_metadata(const esp_partition_pos_t *position,esp_image_metadata_t *metadata){
 sdk::require(position->offset==sdk::partitions[0].address && position->size==sdk::partitions[0].size,"Wrong running image range");
 ++sdk::image_reads;if(!sdk::image_ok)return ESP_FAIL;metadata->image_len=sdk::image_size;return ESP_OK;
}
inline int esp_littlefs_info(const char *label,size_t *total,size_t *used){
 sdk::require(std::strcmp(label,"littlefs_data")==0,"Wrong LittleFS partition");++sdk::littlefs_reads;
 if(!sdk::littlefs_ok)return ESP_FAIL;*total=sdk::littlefs_total;*used=sdk::littlefs_used;return ESP_OK;
}
inline int esp_vfs_fat_info(const char *path,uint64_t *total,uint64_t *free){
 sdk::require(std::strcmp(path,"/sdcard")==0,"Wrong SD mountpoint");++sdk::sd_reads;
 if(!sdk::sd_ok)return ESP_FAIL;*total=sdk::sd_total;*free=sdk::sd_free;return ESP_OK;
}
''', encoding='utf-8')

(OUT / 'check.cpp').write_text(r'''
#include "boost/json/src.hpp"
#include "pocket_resources.hpp"
#include "sdk.hpp"
#include <cmath>
#include <iostream>
#include <string>
namespace bj=boost::json;
using sdk::require;
double numeric(const bj::value &value){return value.to_number<double>();}
void near(const bj::value &value,double expected,const char *message){require(std::fabs(numeric(value)-expected)<0.000001,message);}
const bj::object &object(const bj::object &value,const char *name){return value.at(name).as_object();}
void invalid_cpu(const bj::object &snapshot){
 const auto &cpu=object(snapshot,"cpu");
 require(!cpu.at("valid").as_bool() && cpu.at("loadPercent").is_null(),"CPU warm-up/unavailable data was marked valid");
 require(cpu.at("cores").as_array().size()==2,"Missing CPU cores");
 for(const auto &core:cpu.at("cores").as_array())require(core.as_object().at("loadPercent").is_null(),"Unavailable core displayed a load");
}
void cpu_load(const bj::object &snapshot,double zero,double one,int interval){
#if NO_RUNTIME_STATS
 invalid_cpu(snapshot);
 require(!object(snapshot,"cpu").at("error").as_string().empty(),"Missing unavailable-runtime explanation");
#else
 const auto &cpu=object(snapshot,"cpu");require(cpu.at("valid").as_bool(),"Valid runtime sample rejected");
 near(cpu.at("intervalMs"),interval,"CPU interval differs from sample window");
 near(cpu.at("loadPercent"),(zero+one)/2,"CPU total is not the per-core average");
 const auto &cores=cpu.at("cores").as_array();
 near(cores[0].as_object().at("loadPercent"),zero,"Core 0 idle difference incorrect");
 near(cores[1].as_object().at("loadPercent"),one,"Core 1 idle difference incorrect");
#endif
}
void resource_samples(){
 auto first=ouse::resource_snapshot();invalid_cpu(first);
 require(first.at("ok").as_bool() && numeric(first.at("schemaVersion"))==1,"Snapshot schema invalid");
 near(object(first,"cpu").at("frequencyMHz"),360,"CPU clock unit conversion incorrect");
 const auto &memory=object(first,"memory"), &internal=object(memory,"internal"), &psram=object(memory,"psram");
 near(internal.at("total"),512*1024,"Internal heap total incorrect");near(internal.at("used"),256*1024,"Internal heap used incorrect");
 near(internal.at("minimumFree"),192*1024,"Internal low-water mark missing");
 near(psram.at("total"),32*1024*1024,"PSRAM heap mixed with internal heap");near(psram.at("free"),26*1024*1024,"PSRAM free incorrect");
 require(sdk::largest_calls[0]==1 && sdk::largest_calls[1]==1,"First largest-block sample missing");
 require(sdk::chip_reads==1,"System information sampled repeatedly");
 const int heap_before=sdk::heap_reads,clock_before=sdk::clock_reads,idle_before=sdk::idle_reads;
 sdk::now+=999999;require(ouse::resource_snapshot()==first,"Snapshot changed inside the one-second cache window");
 require(sdk::heap_reads==heap_before && sdk::clock_reads==clock_before && sdk::idle_reads==idle_before,"Cached request resampled hardware");
 ++sdk::now;sdk::idle[0]+=750000;sdk::idle[1]+=250000;
 auto second=ouse::resource_snapshot();cpu_load(second,25,75,1000);
 near(object(object(second,"memory"),"internal").at("largestAgeMs"),1000,"Largest-block cache age incorrect");
 require(sdk::largest_calls[0]==1,"Largest-block walk repeated before five seconds");
 // The 32-bit variant wraps here; the same real subtraction must remain correct.
 sdk::now+=1000000;sdk::idle[0]+=1000000;sdk::idle[1]+=0;
 auto third=ouse::resource_snapshot();cpu_load(third,0,100,1000);
 sdk::largest[0]=64*1024;sdk::free[0]=sdk::total[0]+1;sdk::frequency_ok=false;
 sdk::now=6000000;sdk::idle[0]+=1500000;sdk::idle[1]+=1500000;
 auto fifth=ouse::resource_snapshot();cpu_load(fifth,50,50,3000);
 require(sdk::largest_calls[0]==2 && sdk::largest_calls[1]==2,"Five-second largest-block cache failed to expire");
 const auto &updated=object(object(fifth,"memory"),"internal");
 near(updated.at("largestFree"),64*1024,"Largest block stayed stale");near(updated.at("largestAgeMs"),0,"New largest block has stale age");
 near(updated.at("free"),sdk::total[0],"Heap free above total was not clamped");near(updated.at("used"),0,"Heap used underflowed");
 require(object(fifth,"cpu").at("frequencyMHz").is_null(),"Failed clock read reused a plausible frequency");
 sdk::now=12000000;sdk::idle[0]+=6000000;sdk::idle[1]+=6000000;
 auto resumed=ouse::resource_snapshot();invalid_cpu(resumed);
 sdk::now=13000000;sdk::idle[0]+=250000;sdk::idle[1]+=1000000;
 cpu_load(ouse::resource_snapshot(),75,0,1000);
 // Upper edge is valid; a longer gap above discarded the previous baseline.
 sdk::now=18000000;sdk::idle[0]+=2500000;sdk::idle[1]+=2500000;
 cpu_load(ouse::resource_snapshot(),50,50,5000);
 require(sdk::chip_reads==1,"System information cache expired unexpectedly");
#if NO_RUNTIME_STATS
 require(sdk::idle_reads==0,"Disabled runtime-stat configuration still touched idle counters");
#else
 require(sdk::idle_reads==14,"Unexpected number of per-core counter reads");
#endif
}
void storage_samples(){
 auto absent=ouse::resource_storage(false,false);
 require(absent.at("ok").as_bool() && numeric(absent.at("schemaVersion"))==1,"Storage schema invalid");
 const auto &flash=object(absent,"flash"), &firmware=object(absent,"firmware");
 near(flash.at("totalBytes"),32*1024*1024,"Physical flash capacity incorrect");
 near(flash.at("configuredBytes"),16*1024*1024,"Configured flash capacity conflated with physical capacity");
 require(flash.at("partitions").as_array().size()==3,"Partition enumeration incomplete");
 require(flash.at("partitions").as_array()[2].as_object().at("type").as_string()=="other","Unknown partition type mislabeled");
 near(firmware.at("imageBytes"),6500000,"Image size did not use metadata");near(firmware.at("partitionBytes"),8388608,"Image capacity incorrect");
 require(firmware.at("partition").as_string()=="factory","Wrong active partition");
 require(!sdk::iterator_live && sdk::iterator_releases==1,"Partition iterator was not released at end");
 require(!object(absent,"sd").at("mounted").as_bool() && !object(absent,"sd").at("available").as_bool() && sdk::sd_reads==0,"Absent SD triggered a FAT scan");
 auto busy=ouse::resource_storage(true,true);
 require(object(busy,"sd").at("mounted").as_bool() && object(busy,"sd").at("busy").as_bool() && !object(busy,"sd").at("available").as_bool(),"Playing SD not reported busy");
 require(sdk::sd_reads==0,"Resource query scanned FAT while audio owned the SD card");
 auto mounted=ouse::resource_storage(true,false);
 require(sdk::sd_reads==1 && object(mounted,"sd").at("available").as_bool(),"Idle mounted SD was not sampled");
 near(object(mounted,"sd").at("total"),64.0*1024*1024*1024,"SD capacity narrowed to 32 bits");
 near(object(mounted,"sd").at("used"),24.0*1024*1024*1024,"SD used/free conversion incorrect");
 // Native storage is request-driven. Only static flash metadata is cached;
 // the real app test covers its separate ten-second polling policy.
 sdk::sd_free=sdk::sd_total+100;sdk::littlefs_used=sdk::littlefs_total+100;
 auto clamped=ouse::resource_storage(true,false);
 require(sdk::sd_reads==2 && sdk::littlefs_reads==4,"Native storage query reused stale dynamic capacity");
 near(object(clamped,"sd").at("used"),0,"SD free above total underflowed used bytes");
 near(object(clamped,"internal").at("free"),0,"LittleFS used above total underflowed free bytes");
 sdk::littlefs_ok=sdk::sd_ok=false;
 auto failed=ouse::resource_storage(true,false);
 require(!object(failed,"internal").at("available").as_bool() && !object(failed,"sd").at("available").as_bool(),"Storage read failure advertised valid capacity");
 near(object(failed,"internal").at("total"),0,"Failed LittleFS read retained capacity");near(object(failed,"sd").at("total"),0,"Failed FAT read retained capacity");
 require(sdk::physical_reads==1 && sdk::configured_reads==1 && sdk::partition_reads==1 && sdk::image_reads==1,"Static flash metadata rescanned on each request");
}
int main(int argc,char **argv){
 if(argc>1){
  const std::string scenario=argv[1];
  if(scenario=="image-failure")sdk::image_ok=false;
  else if(scenario=="image-oversize")sdk::image_size=sdk::partitions[0].size+1;
  else if(scenario=="flash-failure")sdk::flash_ok=false;
  else throw std::runtime_error("Unknown scenario");
  const auto result=ouse::resource_storage(false,false);
  if(scenario=="flash-failure")require(object(result,"flash").at("totalBytes").is_null() && object(result,"flash").at("configuredBytes").is_null(),"Unavailable flash size was invented");
  else require(object(result,"firmware").at("imageBytes").is_null(),"Invalid image metadata was trusted");
  std::cout<<"PASS resource metadata edge: "<<scenario<<'\n';return 0;
 }
 resource_samples();storage_samples();
 std::cout<<"PASS real resource backend ("<<sizeof(configRUN_TIME_COUNTER_TYPE)*8<<"-bit runtime, stats "<<(!NO_RUNTIME_STATS)
          <<"): first/cache/window/background CPU, per-core load, heap masks, five-second largest block, flash metadata cache, SD audio lease, capacity failure/clamping\n";
}
''', encoding='utf-8')

compiler = Path(env['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
boost = ROOT / 'examples/brookesia-minimal/managed_components/espressif__esp-boost/src'
for name, definitions in [('u64', []), ('u32', ['/DCOUNTER_U32']), ('unavailable', ['/DNO_RUNTIME_STATS=1'])]:
    executable = OUT / f'check-{name}.exe'
    command = [
        str(compiler), '/nologo', '/std:c++latest', '/EHsc', '/utf-8', '/MDd',
        '/DBOOST_ALL_NO_LIB', '/DBOOST_NO_USER_CONFIG', *definitions,
        f'/FI{SDK / "sdk.hpp"}', f'/I{SDK}', f'/I{boost}',
        f'/I{ROOT / "components/pocket_platform/include"}',
        str(source), str(OUT / 'check.cpp'), f'/Fe{executable}'
    ]
    result = subprocess.run(command, env=env, cwd=OUT, capture_output=True, text=True, errors='replace')
    if result.returncode:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit(result.returncode)
    subprocess.run([str(executable)], check=True, timeout=10)
    if name == 'u64':
        for scenario in ['image-failure', 'image-oversize', 'flash-failure']:
            subprocess.run([str(executable), scenario], check=True, timeout=10)
