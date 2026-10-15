"""Exercise the real log capture/RPC/rotation code using host filesystem I/O."""
from pathlib import Path
import os,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8',errors='replace')
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'examples/brookesia-minimal/build/host-log-backend';OUT.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
script=OUT/'environment.cmd';script.write_text(f'@echo off\ncall "{vs}/VC/Auxiliary/Build/vcvars64.bat" >nul\nset\n',encoding='mbcs')
env=dict(os.environ)
for line in subprocess.check_output(['cmd.exe','/d','/c',str(script)],encoding='mbcs').splitlines():
    if '=' in line and not line.startswith('='):key,value=line.split('=',1);env[key]=value
source=(ROOT/'components/pocket_platform/pocket_logs.cpp').read_text(encoding='utf-8')
source='\n'.join(line for line in source.splitlines() if not line.startswith(('#include "esp_log.h"','#include "freertos/')))
source=source.replace('"/littlefs/user/logs"','"'+(OUT/'logs').as_posix()+'"')
cpp=r'''
#include "boost/json/src.hpp"
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <iostream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
using vprintf_like_t=int(*)(const char*,va_list);
std::atomic<int> uart_calls{0};
int uart(const char *format,va_list a){++uart_calls;return vsnprintf(nullptr,0,format,a);}
std::atomic<vprintf_like_t> output{uart};
std::atomic<int> hook_calls{0},task_calls{0},task_result{1};
vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn){++hook_calls;std::this_thread::sleep_for(std::chrono::milliseconds(2));return output.exchange(fn);}
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
void portENTER_CRITICAL(std::mutex *m){m->lock();}
void portEXIT_CRITICAL(std::mutex *m){m->unlock();}
void vTaskDelay(int){}
int pdMS_TO_TICKS(int ms){return ms;}
constexpr int pdPASS=1;
int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){++task_calls;std::this_thread::sleep_for(std::chrono::milliseconds(2));return task_result.load();}
void print_log(const char *format,...) {va_list a;va_start(a,format);output.load()(format,a);va_end(a);}
void require(bool b,const char *s){if(!b) throw std::runtime_error(s);}
template<class Function> void simultaneous(Function fn) {
 std::atomic<int> ready{0};std::atomic<bool> start{false};std::vector<std::thread> threads;
 for(int i=0;i<24;++i) threads.emplace_back([&]{++ready;while(!start.load()) std::this_thread::yield();fn();});
 while(ready.load()!=24) std::this_thread::yield();start=true;
 for(auto &thread:threads) thread.join();
}
int checked_vsnprintf(char *buffer,size_t size,const char *format,va_list args) {
 if(std::strcmp(format,"__format_failure__")==0) {std::memset(buffer,'X',size);return -1;}
 return vsnprintf(buffer,size,format,args);
}
#define vsnprintf checked_vsnprintf
'''+source+r'''
#undef vsnprintf
namespace bj=boost::json;
int main(int argc,char**){
 if(argc>1) {
  const auto saved=ouse::log_request({{"Op","LogsSaved"}});
  require(std::string(saved.at("text").as_string()).find("reboot-marker")!=std::string::npos,"Fresh process could not read persistent logs");
  std::cout<<"PASS saved logs survive a fresh process with an empty RAM ring\n";return 0;
 }
 simultaneous([]{ouse::initialize_log_capture();});
 require(hook_calls==1,"Concurrent capture initialization installed the hook more than once");
 task_result=0;ouse::start_log_writer();
 require(!ouse::log_request({{"Op","LogsRead"}}).at("persistent").as_bool(),"Writer creation failure was hidden");
 require(ouse::log_request({{"Op","LogsClear"}}).at("ok").as_bool(),"Clear after failed writer startup failed");
 require(!ouse::log_request({{"Op","LogsRead"}}).at("persistent").as_bool(),"Clear falsely advertised an absent writer");
 task_result=pdPASS;simultaneous([]{ouse::start_log_writer();});
 require(task_calls==2,"Concurrent startup created duplicate writers or failed to retry");
 require(ouse::log_request({{"Op","LogsRead"}}).at("persistent").as_bool(),"Writer retry did not recover");
 ouse::log_request({{"Op","LogsClear"}});
 print_log("\x1b[31mE (1) test: 中文错误 %d\x1b[0m\n",8);
 auto read=ouse::log_request({{"Op","LogsRead"}});auto entries=read.at("entries").as_array();
 require(entries.size()==1,"Capture missing");std::string text(entries[0].as_object().at("text").as_string());
 require(text=="E (1) test: 中文错误 8\n","ANSI or UTF8 capture corrupted");
 print_log("__format_failure__");
 read=ouse::log_request({{"Op","LogsRead"}});entries=read.at("entries").as_array();
 require(entries.back().as_object().at("text").as_string().empty(),"Failed formatting captured uninitialized bytes");
 require(ouse::utf8_prefix("a\xE0\x80\x80",4)==1,"Overlong UTF8 accepted");
 require(ouse::utf8_prefix("a\xED\xA0\x80",4)==1,"UTF8 surrogate accepted");
 require(ouse::utf8_prefix("a\xF0\x80\x80\x80",5)==1,"Overlong four-byte UTF8 accepted");
 require(ouse::utf8_prefix("a\xF4\x90\x80\x80",5)==1,"Out-of-range UTF8 accepted");
 require(ouse::utf8_prefix("\xF4\x8F\xBF\xBF",4)==4,"Largest valid UTF8 codepoint rejected");
 const auto cursor=read.at("last").as_uint64();
 print_log("I (2) next\n");require(ouse::log_request({{"Op","LogsRead"},{"After",cursor}}).at("entries").as_array().size()==1,"Cursor duplicated entries");
 require(!ouse::log_request({{"Op","LogsRead"},{"After",-1}}).at("ok").as_bool(),"Negative cursor accepted");
 require(!ouse::log_request({{"Op","LogsRead"},{"After",1.5}}).at("ok").as_bool(),"Fractional cursor accepted");
 require(!ouse::log_request({{"Op","LogsRead"},{"After",1e20}}).at("ok").as_bool(),"Overflow cursor accepted");
 require(!ouse::log_request({{"Op","LogsUnknown"}}).at("ok").as_bool(),"Unknown operation accepted");
 for(int i=0;i<130;i++) print_log("I (%d) overflow 中文\n",i);
 auto overflow=ouse::log_request({{"Op","LogsRead"},{"After",cursor}});
 require(overflow.at("missed").as_bool() && overflow.at("entries").as_array().size()==64,"Lost records not reported");
 require(ouse::log_request({{"Op","LogsFlush"}}).at("ok").as_bool(),"Flush failed");
 auto saved=ouse::log_request({{"Op","LogsSaved"}});
 require(std::string(saved.at("text").as_string()).find("overflow")!=std::string::npos,"Saved logs missing");
 const auto before=overflow.at("last").as_uint64();ouse::log_request({{"Op","LogsClear"}});
 require(ouse::log_request({{"Op","LogsRead"}}).at("entries").as_array().empty(),"Clear retained live entries");
 require(ouse::log_request({{"Op","LogsSaved"}}).at("text").as_string().empty(),"Clear retained files");
 std::string long_line;
 for(int i=0;i<120;i++) long_line+="中文";
 for(int batch=0;batch<20;batch++) {
  for(int line=0;line<32;line++) print_log("I (%d) %s\n",line,long_line.c_str());
  ouse::log_request({{"Op","LogsFlush"}});
 }
 read=ouse::log_request({{"Op","LogsRead"},{"After",before}});
 require(read.at("last").as_uint64()>before,"Clear reused sequence numbers");
 uint64_t total=0;unsigned files=0;
 for(auto &entry:std::filesystem::directory_iterator(ouse::DIRECTORY)){total+=entry.file_size();++files;}
 require(files==3 && total<=3*(64*1024+384),"Rotation failed or files unbounded");
 saved=ouse::log_request({{"Op","LogsSaved"}});text=std::string(saved.at("text").as_string());
 require(text.size()<=8192 && ouse::utf8_prefix(text.data(),text.size())==text.size(),"Saved UTF8 tail invalid/unbounded");
 const auto directory=std::filesystem::path(ouse::DIRECTORY);
 std::filesystem::remove(directory/"previous-2.log");std::filesystem::create_directory(directory/"previous-2.log");
 FILE *block=fopen((directory/"previous-2.log/block").string().c_str(),"wb");fputc('x',block);fclose(block);
 FILE *current=fopen((directory/"current.log").string().c_str(),"ab");std::string fill(65536,'x');fwrite(fill.data(),1,fill.size(),current);fclose(current);
 const auto before_failure=std::filesystem::file_size(directory/"current.log");
 print_log("E (99) rotation failure test\n");require(!ouse::log_request({{"Op","LogsFlush"}}).at("ok").as_bool(),"Rotation failure was ignored");
 require(std::filesystem::file_size(directory/"current.log")==before_failure,"File kept growing after rotation failure");
 require(!ouse::log_request({{"Op","LogsRead"}}).at("persistent").as_bool(),"Disk failure not reported");
 std::filesystem::remove(directory/"previous-2.log/block");std::filesystem::remove(directory/"previous-2.log");
 print_log("I (100) recovery\n");require(ouse::log_request({{"Op","LogsFlush"}}).at("ok").as_bool(),"Rotation recovery failed");
 std::filesystem::remove(directory/"current.log");std::filesystem::create_directory(directory/"current.log");
 print_log("E (101) write failure\n");require(!ouse::log_request({{"Op","LogsFlush"}}).at("ok").as_bool(),"Write failure was ignored");
 require(ouse::log_request({{"Op","LogsClear"}}).at("ok").as_bool(),"Clear recovery failed");
 require(ouse::log_request({{"Op","LogsRead"}}).at("persistent").as_bool(),"Successful clear did not reset disk failure");
 const auto console_before=uart_calls.load();ouse::set_console_output_suppressed(true);
 print_log("I (102) console suppressed\n");
 require(uart_calls==console_before,"Suppressed logs still reached the UART");
 read=ouse::log_request({{"Op","LogsRead"}});entries=read.at("entries").as_array();
 require(entries.size()==1 && entries.back().as_object().at("text").as_string()=="I (102) console suppressed\n","Console suppression lost the RAM log");
 ouse::set_console_output_suppressed(false);print_log("I (103) console resumed\n");
 require(uart_calls==console_before+1,"Console output did not resume");
 print_log("I (104) reboot-marker\n");require(ouse::log_request({{"Op","LogsFlush"}}).at("ok").as_bool(),"Flush after clear failed");
 std::cout<<"PASS real log backend: concurrent capture/writer startup, failed formatting, UTF8 validation, ANSI stripping, cursor validation/wrap/clear, disk flush, rotation bounds/failure/recovery, console suppression, saved tail\n";
}
'''
(OUT/'check.cpp').write_text(cpp,encoding='utf-8')
compiler=Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
boost=ROOT/'examples/brookesia-minimal/managed_components/espressif__esp-boost/src'
command=[str(compiler),'/nologo','/std:c++latest','/EHsc','/utf-8','/MDd','/DBOOST_ALL_NO_LIB','/DBOOST_NO_USER_CONFIG',f'/I{boost}',f'/I{ROOT/"components/pocket_platform/include"}',str(OUT/'check.cpp'),f'/Fe{OUT/"check.exe"}']
r=subprocess.run(command,env=env,cwd=OUT,capture_output=True,text=True,errors='replace')
if r.returncode:print(r.stdout);print(r.stderr);raise SystemExit(r.returncode)
subprocess.run([str(OUT/'check.exe')],check=True,timeout=30)
subprocess.run([str(OUT/'check.exe'),'read-existing'],check=True,timeout=10)
