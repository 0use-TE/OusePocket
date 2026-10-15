#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace ouse {
// Drop a truncated UTF-8 suffix, retaining all complete code points.
inline size_t utf8_prefix(const char *text, size_t size) {
    size_t cursor=0, valid=0;
    while(cursor<size) {
        const auto c=static_cast<unsigned char>(text[cursor]);
        size_t width=c<0x80?1:(c>=0xC2 && c<=0xDF)?2:(c>=0xE0 && c<=0xEF)?3:(c>=0xF0 && c<=0xF4)?4:0;
        if(!width || cursor+width>size) break;
        bool correct=true;
        for(size_t i=1;i<width;++i) if((static_cast<unsigned char>(text[cursor+i]) & 0xC0)!=0x80) correct=false;
        if(!correct) break;
        cursor+=width; valid=cursor;
    }
    return valid;
}
// No allocation in the ESP logging callback. Caller provides synchronization.
template <size_t Capacity=64, size_t Width=384> struct LogRing {
    static_assert(Capacity>0 && Width>4);
    struct Entry { uint64_t sequence=0; char text[Width]{}; };
    std::array<Entry,Capacity> entries{};
    uint64_t last=0;
    void append(const char *text) {
        auto &entry=entries[last % Capacity]; entry.sequence=++last;
        size_t size=0;while(size<Width-1 && text[size]) ++size;
        std::memcpy(entry.text,text,size);entry.text[size]=0;
        entry.text[utf8_prefix(entry.text,size)]=0;
    }
    const Entry *get(uint64_t sequence) const {
        if(!sequence || sequence>last || last-sequence>=Capacity) return nullptr;
        const auto &entry=entries[(sequence-1)%Capacity];
        return entry.sequence==sequence ? &entry : nullptr;
    }
    uint64_t first() const {return last>Capacity ? last-Capacity+1 : 1;}
    void clear() { for(auto &e:entries) e.sequence=0; }
};
}
