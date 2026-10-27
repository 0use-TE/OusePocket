#include "ouse_game2d.h"
#include <cmath>
#include <cstring>

static bool number(double value,double limit)
{return std::isfinite(value) && std::abs(value)<=limit;}
extern "C" bool ouse_game2d_projectiles(void *shot_buffer,size_t count,const void *target_buffer,
    size_t target_count,void *hit_buffer,double dt)
{
    auto *shots=static_cast<unsigned char *>(shot_buffer);
    auto *targets=static_cast<const unsigned char *>(target_buffer);
    auto *hits=static_cast<unsigned char *>(hit_buffer);
    if(count>128 || target_count>32 || !std::isfinite(dt) || dt<0 || dt>.125 ||
       (count && (!shots || !hits)) || (target_count && !targets))return false;
    // Validate the entire batch before changing caller-owned memory.
    for(size_t i=0;i<count;++i){double s[8];std::memcpy(s,shots+i*64,sizeof(s));
        for(unsigned k=0;k<6;++k)if(!number(s[k],1000000))return false;
        if(s[5]<0 || (s[6]!=0 && s[6]!=1) || !number(s[7],4294967295.0) || s[7]<0 || std::floor(s[7])!=s[7])return false;
    }
    for(size_t i=0;i<target_count;++i){double t[4];std::memcpy(t,targets+i*32,sizeof(t));
        if(!number(t[0],1000000) || !number(t[1],1000000) || !number(t[2],1000000) || t[2]<0 || (t[3]!=0 && t[3]!=1))return false;
    }
    for(size_t i=0;i<count;++i){double s[8];std::memcpy(s,shots+i*64,sizeof(s));uint32_t mask=0;
        if(!s[6]){std::memcpy(hits+i*4,&mask,sizeof(mask));continue;}
        const double ax=s[0],ay=s[1];
        s[0]+=s[2]*dt;s[1]+=s[3]*dt;s[4]-=dt;
        const double dx=s[0]-ax,dy=s[1]-ay,len=dx*dx+dy*dy;
        const uint32_t excluded=static_cast<uint32_t>(s[7]);
        for(size_t j=0;j<target_count;++j){double t[4];std::memcpy(t,targets+j*32,sizeof(t));const uint32_t bit=uint32_t(1)<<j;
            if(!t[3] || (excluded&bit))continue;
            double u=len?((t[0]-ax)*dx+(t[1]-ay)*dy)/len:0;
            if(u<0)u=0;else if(u>1)u=1;
            const double x=t[0]-ax-dx*u,y=t[1]-ay-dy*u,r=t[2]+s[5];
            if(x*x+y*y<r*r)mask|=bit;
        }
        std::memcpy(shots+i*64,s,sizeof(s));std::memcpy(hits+i*4,&mask,sizeof(mask));
    }
    return true;
}
