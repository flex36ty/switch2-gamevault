#pragma once
#include "format.h"
#include <stdbool.h>
#include <string.h>
/* Schema 1 assumed the packaged patch extended-data size described the NCM
 * database blob. Schema 2 stores the exact bounded database blob, including an
 * opaque trailing region. Never resize/reconstruct it using packaged sizes.
 * Backup additionally cross-checks NCM's content list and re-reads the record.
 * This validates layout only; callers must verify hashes and content entries. */
static inline bool vaultMetadataLayoutValid(const InstallPlan* p,uint8_t type,uint64_t app){
 if((p->schema!=1&&p->schema!=2)||!app||(app&0xfff)||!p->count||p->count>INSTALL_MAX||p->meta_size<24||p->meta_size>META_MAX)return false;
 uint16_t ext=0,count=0,deps=0;memcpy(&ext,p->metadata,2);memcpy(&count,p->metadata+2,2);memcpy(&deps,p->metadata+4,2);
 if(count!=p->count||deps)return false;
 uint32_t start=8u+ext;
 if(start>p->meta_size||24u*p->count>p->meta_size-start)return false;
 uint32_t records_end=start+24u*p->count;
 if(type==128)return p->title==app&&ext==16&&p->meta_size==records_end;
 if(type!=129||p->title!=app+0x800||ext!=24)return false;
 uint64_t base=0;uint32_t packaged_extra=0;memcpy(&base,p->metadata+8,8);memcpy(&packaged_extra,p->metadata+20,4);
 if(base!=app)return false;
 if(p->schema==1)return packaged_extra<=META_MAX&&p->meta_size==records_end+packaged_extra;
 return true;
}
