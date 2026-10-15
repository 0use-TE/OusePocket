#include "pocket_logs.hpp"
#include "log_ring.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <cmath>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ouse {
namespace {
LogRing<> ring;
portMUX_TYPE ring_lock=portMUX_INITIALIZER_UNLOCKED;
std::mutex disk_mutex;
std::once_flag capture_initialized;
std::atomic<vprintf_like_t> previous{nullptr};
std::atomic<bool> console_suppressed{false};
std::atomic<int> console_writers{0};
uint64_t persisted=0;
constexpr const char *DIRECTORY="/littlefs/user/logs";
constexpr size_t FILE_LIMIT=64*1024;
std::atomic<bool> writer_started{false};
std::atomic<bool> disk_ok{true};
int capture(const char *format,va_list args) {
    va_list copy; va_copy(copy,args);
    char text[384]{}; const int formatted=vsnprintf(text,sizeof(text),format,copy); va_end(copy);
    if(formatted<0) text[0]=0;
    // The UART keeps its colors; JSON/LVGL receives plain text.
    size_t out=0;
    for(size_t i=0;text[i];) {
        if(text[i]=='\x1b' && text[i+1]=='[') {
            i+=2; while(text[i] && !(text[i]>='@' && text[i]<='~')) ++i;
            if(text[i]) ++i;
        } else text[out++]=text[i++];
    }
    text[out]=0;
    if(formatted>=static_cast<int>(sizeof(text))) {
        const auto cut=utf8_prefix(text,std::min(out,sizeof(text)-5));
        std::memcpy(text+cut,"...\n",5); // Keep truncated records readable and line-separated.
    }
    portENTER_CRITICAL(&ring_lock); ring.append(text); portEXIT_CRITICAL(&ring_lock);
    // Every task logs through here; a blocking lock would serialize them all
    // behind the UART. Writers are counted instead so suppression can drain them.
    const auto console=previous.load();
    if(!console || console_suppressed.load()) return formatted;
    console_writers.fetch_add(1);
    const int written=console_suppressed.load() ? formatted : console(format,args);
    console_writers.fetch_sub(1);
    return written;
}
void flush_logs() {
    // Copy one entry at a time; never call a filesystem or JSON allocator under
    // the spinlock and never log recursively from this writer.
    std::lock_guard lock(disk_mutex);
    for(size_t n=0;n<64;++n) {
        LogRing<>::Entry entry;
        portENTER_CRITICAL(&ring_lock);
        const auto first=ring.first();
        if(persisted+1<first) persisted=first-1;
        auto *source=ring.get(persisted+1);
        if(source) entry=*source;
        portEXIT_CRITICAL(&ring_lock);
        if(!entry.sequence) break;
        const std::string path=std::string(DIRECTORY)+"/current.log";
        std::error_code ec;
        const bool exists=std::filesystem::exists(path,ec);
        if(ec) {disk_ok=false;break;}
        const auto size=exists?std::filesystem::file_size(path,ec):0;
        if(ec) {disk_ok=false;break;}
        if(exists && size>=FILE_LIMIT) {
            std::filesystem::remove(std::string(DIRECTORY)+"/previous-2.log",ec);
            if(ec) {disk_ok=false;break;}
            const auto previous_path=std::string(DIRECTORY)+"/previous-1.log";
            const bool previous_exists=std::filesystem::exists(previous_path,ec);
            if(ec) {disk_ok=false;break;}
            if(previous_exists) {
                std::filesystem::rename(previous_path,std::string(DIRECTORY)+"/previous-2.log",ec);
                if(ec) {disk_ok=false;break;}
            }
            std::filesystem::rename(path,std::string(DIRECTORY)+"/previous-1.log",ec);
            if(ec) {disk_ok=false;break;} // Never grow the active file if rotation fails.
        }
        FILE *file=fopen(path.c_str(),"ab");
        if(!file) { disk_ok=false; break; }
        const auto length=std::strlen(entry.text);
        const bool written=fwrite(entry.text,1,length,file)==length;
        const bool closed=fclose(file)==0;
        if(!written || !closed) { disk_ok=false; break; }
        disk_ok=true;
        persisted=entry.sequence;
    }
}
void writer(void*) { for(;;) { flush_logs(); vTaskDelay(pdMS_TO_TICKS(2000)); } }
}
void initialize_log_capture() {
    // Reinstalling capture would save itself as the console sink and recurse.
    std::call_once(capture_initialized,[] {previous.store(esp_log_set_vprintf(capture));});
}
void set_console_output_suppressed(bool suppressed) {
    // Finish any already-running console write before the USB hello reply.
    // Later logs are still captured above, but cannot enter the UART stream.
    console_suppressed.store(suppressed);
    if(!suppressed) return;
    for(int waited=0;console_writers.load()>0 && waited<50;++waited) vTaskDelay(pdMS_TO_TICKS(2));
}
void start_log_writer() {
    std::lock_guard lock(disk_mutex);
    if(writer_started) return;
    std::error_code ec; std::filesystem::create_directories(DIRECTORY,ec);
    const bool started=!ec && xTaskCreate(writer,"pocket_logs",4096,nullptr,2,nullptr)==pdPASS;
    writer_started=started;
    disk_ok=started;
}
boost::json::object log_request(const boost::json::object &request) {
    auto op=request.if_contains("Op");
    if(!op || !op->is_string()) return {{"ok",false},{"error","Missing log operation"}};
    if(op->as_string()=="LogsFlush") {flush_logs();return {{"ok",disk_ok.load()},{"path",DIRECTORY},
        {"error",disk_ok.load()?"":"Cannot persist logs"}};}
    if(op->as_string()=="LogsClear") {
        std::lock_guard lock(disk_mutex);
        portENTER_CRITICAL(&ring_lock); ring.clear(); persisted=ring.last; portEXIT_CRITICAL(&ring_lock);
        std::error_code ec;
        for(const auto *name:{"current.log","previous-1.log","previous-2.log"}) {
            std::filesystem::remove(std::string(DIRECTORY)+"/"+name,ec);
            if(ec) {disk_ok=false;return {{"ok",false},{"error",ec.message()}};}
        }
        disk_ok=true;
        return {{"ok",true}};
    }
    if(op->as_string()=="LogsSaved") {
        // Fetch a bounded tail of persistent files, also after reboot.
        std::lock_guard lock(disk_mutex);
        std::string content;
        for(const auto *name:{"previous-2.log","previous-1.log","current.log"}) {
            FILE *file=fopen((std::string(DIRECTORY)+"/"+name).c_str(),"rb");
            if(!file) continue;
            fseek(file,0,SEEK_END); auto size=ftell(file);
            if(size<0) {fclose(file);continue;}
            const auto offset=std::max(0L,size-4096); fseek(file,offset,SEEK_SET);
            char buffer[4097]{}; auto count=fread(buffer,1,4096,file); fclose(file);
            size_t start=0;
            if(offset) {while(start<count && (static_cast<unsigned char>(buffer[start])&0xC0)==0x80) ++start;}
            content.append(buffer+start,utf8_prefix(buffer+start,count-start));
        }
        if(content.size()>8192) {
            size_t start=content.size()-8192;
            while(start<content.size() && (static_cast<unsigned char>(content[start])&0xC0)==0x80) ++start;
            content.erase(0,start);
        }
        // Start on a line boundary instead of splitting a UTF-8 character.
        auto newline=content.find('\n'); if(newline!=std::string::npos && content.size()>4096) content.erase(0,newline+1);
        return {{"ok",true},{"text",content},{"path",DIRECTORY}};
    }
    if(op->as_string()!="LogsRead") return {{"ok",false},{"error","Unknown log operation"}};
    uint64_t after=0;
    if(auto value=request.if_contains("After"); value) {
        if(!value->is_number()) return {{"ok",false},{"error","After must be a nonnegative integer"}};
        const double number=value->to_number<double>();
        if(!std::isfinite(number) || number<0 || number>9007199254740991.0 || std::floor(number)!=number)
            return {{"ok",false},{"error","Invalid log cursor"}};
        after=static_cast<uint64_t>(number);
    }
    std::string filter;
    if(auto value=request.if_contains("Filter"); value && value->is_string()) filter=std::string(value->as_string());
    boost::json::array entries;
    uint64_t last=0, first=0;
    portENTER_CRITICAL(&ring_lock); last=ring.last; first=ring.first(); portEXIT_CRITICAL(&ring_lock);
    for(auto sequence=std::max(after+1,first);sequence<=last;++sequence) {
        LogRing<>::Entry copy;
        portENTER_CRITICAL(&ring_lock); if(auto *entry=ring.get(sequence)) copy=*entry; portEXIT_CRITICAL(&ring_lock);
        if(copy.sequence && (filter.empty() || std::strstr(copy.text,filter.c_str())))
            entries.push_back({{"sequence",copy.sequence},{"text",copy.text}});
    }
    return {{"ok",true},{"entries",std::move(entries)},{"last",last},{"missed",after && after+1<first},
        {"persistent",writer_started.load() && disk_ok.load()},{"path",DIRECTORY}};
}
}
