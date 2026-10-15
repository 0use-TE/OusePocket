"""Compile real log ring and Playback implementation; mock hardware/decoder I/O.

The proprietary RISC-V decoder and physical codec still need device validation.
This exercises native bounds, format changes, pause/stop and worker lifetimes.
"""
from pathlib import Path
import os,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8',errors='replace')
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'examples/brookesia-minimal/build/host-native-services';OUT.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
script=OUT/'environment.cmd';script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding='mbcs')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(script)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):key,value=line.split('=',1);env[key]=value
source=(ROOT/'examples/brookesia-minimal/main/waveshare_audio.cpp').read_text(encoding='utf-8')
playback=source[source.index('class Playback final'):source.index('class AudioDevice final')].replace('private:','public:')
cpp=r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <cstring>
#include "log_ring.hpp"
#include "esp_audio_simple_dec.h"
using namespace std::chrono_literals;
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
namespace hal::audio {
enum class PlayState {Idle,Playing,Paused};
struct PlaybackIface {using EventCallback=std::function<void(PlayState)>;virtual ~PlaybackIface()=default;
 virtual bool open(EventCallback)=0;virtual void close()=0;virtual bool play(const std::string&)=0;
 virtual bool pause()=0;virtual bool resume()=0;virtual bool stop()=0;virtual bool is_opened()const=0;};
}
struct Codec {
 struct Config {uint8_t bits,channels;uint32_t sample_rate;};
 std::atomic<size_t> writes{0};std::atomic<uint32_t> rate{0};
 bool open(Config c) {rate=c.sample_rate;return c.bits==16 && c.channels>=1 && c.channels<=2;}
 void close(){};
 bool write_data(const uint8_t*,size_t n) {require(n<=4096 && n%2==0,"Invalid PCM write");++writes;std::this_thread::sleep_for(2ms);return true;}
};
struct Semaphore {std::mutex m;std::condition_variable cv;bool ready=false;};
using SemaphoreHandle_t=Semaphore*;
constexpr int portMAX_DELAY=-1,pdPASS=1;
Semaphore *xSemaphoreCreateBinary(){return new Semaphore;}
void vSemaphoreDelete(Semaphore *s){delete s;}
void xSemaphoreGive(Semaphore *s) {std::lock_guard l(s->m);s->ready=true;s->cv.notify_one();}
void xSemaphoreTake(Semaphore *s,int){std::unique_lock l(s->m);s->cv.wait(l,[&]{return s->ready;});s->ready=false;}
void vTaskDelay(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
void vTaskDelete(void*){}
bool create_fail=false;
int xTaskCreate(void(*fn)(void*),const char*,unsigned,void *arg,unsigned,void*) {if(create_fail) return 0;std::thread([=]{fn(arg);}).detach();return pdPASS;}
#define ESP_LOGE(...) ((void)0)
namespace ouse {
std::atomic<int> leases{0};std::mutex m;std::string state,error;uint64_t position=0;
struct MediaReader {
 size_t remaining=400000;bool failure=false;~MediaReader(){--leases;}
 size_t read(void *buffer,size_t n){n=std::min(n,remaining);memset(buffer,0,n);remaining-=n;return n;}
 bool failed()const{return failure;}
};
bool io_fail=false;
std::unique_ptr<MediaReader> open_media_file(const std::string &p){
 if(p.find("missing")!=std::string::npos || p.find("..")==0) return {};
 ++leases;auto r=std::make_unique<MediaReader>();r->failure=io_fail;return r;
}
void set_media_state(const std::string&,const char *s,const std::string &e=""){std::lock_guard l(m);state=s;error=e;}
void set_media_progress(uint64_t ms,uint64_t){std::lock_guard l(m);position=ms;}
std::string last_error(){std::lock_guard l(m);return error;}
}
enum class Mode {Normal,Resize,Stall,InvalidBounds,Unsupported,IOError};
std::atomic<Mode> mode{Mode::Normal};esp_audio_simple_dec_type_t decoded_type;
esp_audio_err_t esp_mp3_dec_register(){return ESP_AUDIO_ERR_OK;}
esp_audio_err_t esp_wav_dec_register(){return ESP_AUDIO_ERR_OK;}
extern "C" esp_audio_err_t esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *c,void **p){decoded_type=c->dec_type;*p=reinterpret_cast<void*>(1);return ESP_AUDIO_ERR_OK;}
extern "C" void esp_audio_simple_dec_close(void*){}
extern "C" esp_audio_err_t esp_audio_simple_dec_process(void*,esp_audio_simple_dec_raw_t *raw,esp_audio_simple_dec_out_t *out){
 raw->consumed=0;out->decoded_size=0;
 if(mode==Mode::Resize && out->len<16384){out->needed_size=16384;return ESP_AUDIO_ERR_BUFF_NOT_ENOUGH;}
 if(mode==Mode::Stall) return ESP_AUDIO_ERR_OK;
 if(mode==Mode::InvalidBounds){raw->consumed=raw->len+1;return ESP_AUDIO_ERR_OK;}
 raw->consumed=raw->len;out->decoded_size=raw->len;memset(out->buffer,0,out->decoded_size);return ESP_AUDIO_ERR_OK;
}
extern "C" esp_audio_err_t esp_audio_simple_dec_get_info(void*,esp_audio_simple_dec_info_t *i){
 i->sample_rate=decoded_type==ESP_AUDIO_SIMPLE_DEC_TYPE_WAV?32000:44100;i->bits_per_sample=mode==Mode::Unsupported?24:16;i->channel=2;return ESP_AUDIO_ERR_OK;
}
'''+playback+r'''
void wait_done(Playback &p){for(int i=0;i<3000 && p.running_;++i) std::this_thread::sleep_for(1ms);require(!p.running_,"Worker timeout");p.stop();require(ouse::leases==0,"File lease leaked");}
int main(){
 ouse::LogRing<3,12> ring;ring.append("中文中文8");require(std::string(ring.get(1)->text)=="中文中","UTF8 truncation splits character");
 ring.append("two");ring.append("three");ring.append("four");require(!ring.get(1) && ring.first()==2,"Ring did not wrap");
 require(std::string(ring.get(4)->text)=="four","Ring sequence lookup failed");ring.clear();require(!ring.get(4),"Clear retained entry");
 ring.append("after clear");require(ring.last==5 && ring.get(5),"Clear reused cursor sequence");
 const auto codec=std::make_shared<Codec>();Playback p(codec);require(p.open([](auto){}),"Open failed");
 require(!p.play("https://example.com/song.flac"),"Unsupported format accepted");require(!p.play("/sdcard/music/missing.wav"),"Missing file accepted");
 mode=Mode::Resize;require(p.play("file:///sdcard/music/demo.WAV"),"WAV rejected");wait_done(p);require(codec->rate==32000 && ouse::last_error().empty(),"WAV output/resize failed");
 mode=Mode::Normal;require(p.play("/sdcard/music/demo.mp3"),"MP3 rejected");
 while(codec->rate!=44100 || codec->writes==0) std::this_thread::sleep_for(1ms);
 require(p.pause(),"Pause failed");std::this_thread::sleep_for(5ms);const auto writes=codec->writes.load();std::this_thread::sleep_for(20ms);require(codec->writes==writes,"Paused worker still writes");
 require(p.resume(),"Resume failed");std::this_thread::sleep_for(20ms);require(codec->writes>writes,"Resume produced no output");p.stop();require(ouse::leases==0,"Stop didn't close file");
 for(auto m:{Mode::Stall,Mode::InvalidBounds,Mode::Unsupported}) {mode=m;require(p.play("/sdcard/music/demo.mp3"),"Worker failed to start");wait_done(p);require(!ouse::last_error().empty(),"Bad decoder state ignored");}
 mode=Mode::Normal;ouse::io_fail=true;require(p.play("/sdcard/music/demo.wav"),"IO failure worker rejected");wait_done(p);require(ouse::last_error().find("SD removed")!=std::string::npos,"Read failure not reported");ouse::io_fail=false;
 create_fail=true;require(!p.play("/sdcard/music/demo.wav") && ouse::leases==0,"Task failure leaked file");create_fail=false;
 require(p.play("tone://test"),"Settings test tone rejected");wait_done(p);require(codec->rate==16000,"Test tone didn't restore sample rate");p.close();
 std::cout<<"PASS real log ring and Playback: UTF8/wrap/clear, WAV/MP3 format switch, resize/bounds/stall, pause/resume/stop, SD read failure, task failure, test tone and lease cleanup\n";
}
'''
(OUT/'check.cpp').write_text(cpp,encoding='utf-8')
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
codec=ROOT/'examples/brookesia-minimal/managed_components/espressif__esp_audio_codec/include'
command=[str(compiler),'/nologo','/std:c++latest','/EHsc','/utf-8','/MDd',f'/I{ROOT/"components/pocket_platform/include"}',f'/I{codec}',f'/I{codec/"simple_dec"}',str(OUT/'check.cpp'),f'/Fe{OUT/"check.exe"}']
r=subprocess.run(command,env=env,cwd=OUT,capture_output=True,text=True,errors='replace')
if r.returncode:print(r.stdout);print(r.stderr);raise SystemExit(r.returncode)
subprocess.run([str(OUT/'check.exe')],check=True,timeout=20)
