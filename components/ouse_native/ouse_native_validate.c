// SPDX-License-Identifier: MIT
#include "ouse_native_validate.h"
#include "ouse_native_api.h"
#include <string.h>

typedef struct {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} header_t;
typedef struct { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; } program_t;
typedef struct { uint32_t name, type, flags, addr, offset, size, link, info, align, entsize; } section_t;
typedef struct { uint32_t name, value, size; uint8_t info, other; uint16_t shndx; } symbol_t;
typedef struct { uint32_t offset, info; int32_t addend; } rela_t;
_Static_assert(sizeof(header_t)==52 && sizeof(program_t)==32 && sizeof(section_t)==40 && sizeof(symbol_t)==16 && sizeof(rela_t)==12, "ELF32 layout");

static int range(size_t total, uint32_t offset, uint32_t bytes)
{ return offset<=total && bytes<=total-offset; }
static int string_at(const uint8_t *data, const section_t *table, uint32_t offset)
{ return offset<table->size && memchr(data+table->offset+offset,0,table->size-offset)!=NULL; }
static int mapped(const uint8_t *data, const header_t *h, uint32_t addr, uint32_t bytes, uint32_t flags)
{
    for(unsigned i=0;i<h->phnum;++i){
        program_t p;memcpy(&p,data+h->phoff+i*sizeof(p),sizeof(p));
        if(p.type==1 && (p.flags&flags)==flags && addr>=p.vaddr &&
           bytes<=p.memsz && addr-p.vaddr<=p.memsz-bytes)return 1;
    }
    return 0;
}

int ouse_native_validate(const uint8_t *data, size_t length)
{
    header_t h;
    if(!data || length<sizeof(h) || length>OUSE_NATIVE_FILE_LIMIT)return 0;
    memcpy(&h,data,sizeof(h));
    if(memcmp(h.ident,"\177ELF\1\1\1",7) || h.type!=3 || h.machine!=243 || h.version!=1 ||
       h.ehsize!=sizeof(h) || h.phentsize!=sizeof(program_t) || h.shentsize!=sizeof(section_t) ||
       !h.phnum || h.phnum>32 || !h.shnum || h.shnum>128 || h.shstrndx>=h.shnum ||
       (h.phoff&3) || (h.shoff&3) ||
       (h.flags&~1u)!=2u || !range(length,h.phoff,h.phnum*sizeof(program_t)) ||
       !range(length,h.shoff,h.shnum*sizeof(section_t)))return 0;
    section_t sections[128];
    memcpy(sections,data+h.shoff,h.shnum*sizeof(section_t));
    for(unsigned i=0;i<h.shnum;++i){
        const section_t *s=&sections[i];
        if(s->type!=8 && !range(length,s->offset,s->size))return 0;
        // No TLS, constructors, destructors, REL or compressed sections in v1.
        if((s->flags&(0x400u|0x800u)) || s->type==9 || s->type==14 || s->type==15 || s->type==16)return 0;
        if(s->align && (s->align&(s->align-1)))return 0;
    }
    const section_t *names=&sections[h.shstrndx];
    if(names->type!=3 || !names->size)return 0;
    unsigned abi_count=0,loads=0;uint32_t end=0;
    for(unsigned i=0;i<h.phnum;++i){
        program_t p;memcpy(&p,data+h.phoff+i*sizeof(p),sizeof(p));
        if(p.type==3 || p.type==7)return 0; // interpreter or TLS
        if(p.type!=1)continue;
        // The P4 loader allocates from virtual address zero to the last LOAD.
        if(p.filesz>p.memsz || !range(length,p.offset,p.filesz) || p.vaddr<end ||
           p.vaddr>OUSE_NATIVE_IMAGE_LIMIT || p.memsz>OUSE_NATIVE_IMAGE_LIMIT-p.vaddr)return 0;
        end=p.vaddr+p.memsz;++loads;
    }
    if(!loads || !end || !mapped(data,&h,h.entry,2,1) || (h.entry&1))return 0;
    for(unsigned i=0;i<h.shnum;++i){
        const section_t *s=&sections[i];
        if(!string_at(data,names,s->name))return 0;
        if(!strcmp((const char *)data+names->offset+s->name,".ouse_abi")){
            uint32_t abi[3];
            if(s->type!=1 || s->size!=sizeof(abi))return 0;
            memcpy(abi,data+s->offset,sizeof(abi));
            if(abi[0]!=OUSE_NATIVE_MAGIC || abi[1]!=OUSE_NATIVE_ABI || abi[2]!=28)return 0;
            ++abi_count;
        }
        if(s->type!=4)continue;
        if(s->entsize!=sizeof(rela_t) || (s->offset&3) || s->size%sizeof(rela_t) || s->link>=h.shnum)return 0;
        const section_t *syms=&sections[s->link];
        if((syms->type!=11 && syms->type!=2) || (syms->offset&3) || syms->entsize!=sizeof(symbol_t) ||
           syms->size%sizeof(symbol_t) || syms->link>=h.shnum)return 0;
        const section_t *strings=&sections[syms->link];
        if(strings->type!=3)return 0;
        for(uint32_t j=0;j<s->size;j+=sizeof(rela_t)){
            rela_t r;symbol_t sym;
            memcpy(&r,data+s->offset+j,sizeof(r));
            uint32_t type=r.info&255u,index=r.info>>8;
            if((type!=1 && type!=3 && type!=5) || index>=syms->size/sizeof(symbol_t) ||
               (r.offset&3) || !mapped(data,&h,r.offset,4,2))return 0;
            memcpy(&sym,data+syms->offset+index*sizeof(sym),sizeof(sym));
            if(!string_at(data,strings,sym.name))return 0;
            const char *name=(const char *)data+strings->offset+sym.name;
            if(type==3){
                if(index || r.addend<0 || !mapped(data,&h,(uint32_t)r.addend,1,0))return 0;
            }else{
                // A single versioned host API import; modules use its callbacks.
                if(sym.shndx || sym.value || strcmp(name,"ouse_native_get_api") || r.addend)return 0;
            }
        }
    }
    return abi_count==1;
}
