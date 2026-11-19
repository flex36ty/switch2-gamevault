#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "format.h"
typedef bool (*JournalDigestCheck)(const void*,size_t,const uint8_t[32]);
static inline bool journal_valid(const InstallJournal* j,JournalDigestCheck check){
 if(memcmp(j->magic,"MSDJRNL1",8)||!j->sequence||j->phase<RunActive||j->phase>RunCleaned||j->meta_phase>MetaRemoved)return false;
 for(unsigned i=0;i<INSTALL_MAX;i++)if(j->entries[i].phase>EntryDeleted)return false;
 return check(j,offsetof(InstallJournal,hash),j->hash);
}
static inline bool owned_content_can_delete(uint32_t run_phase,uint32_t entry_phase){
 return run_phase==RunCleanup&&entry_phase>=EntryRegisterIntent&&entry_phase<=EntryDeleteIntent;
}
