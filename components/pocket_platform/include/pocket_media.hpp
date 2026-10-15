#pragma once
#include <cstdio>
#include <memory>
#include <string>
#include "boost/json.hpp"
namespace ouse {
// A small file lease, not a general GPIO/USB resource manager. The SD volume
// stays mounted until every reader has closed its FILE, including error paths.
class MediaReader {
public:
    ~MediaReader();
    size_t read(void *buffer,size_t size);
    bool failed() const {return failed_;}
    uint64_t size() const {return size_;}
    MediaReader(const MediaReader&)=delete;
private:
    friend std::unique_ptr<MediaReader> open_media_file(const std::string&);
    MediaReader(FILE *file,bool sd,uint64_t size):file_(file),sd_(sd),size_(size){}
    FILE *file_;
    bool sd_,failed_=false;
    uint64_t size_;
};
std::unique_ptr<MediaReader> open_media_file(const std::string &path);
void set_media_state(const std::string &path,const char *state,const std::string &error="");
void set_media_progress(uint64_t milliseconds,uint64_t bytes);
boost::json::object pocket_media_state();
}
