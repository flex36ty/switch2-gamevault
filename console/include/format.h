#pragma once
#include <stdint.h>
#define INSTALL_MAX 32
#define META_MAX 65536
#pragma pack(push,1)
typedef struct {uint8_t id[16],hash[32];uint64_t size;uint8_t type,attr;uint16_t reserved;} InstallEntry;
typedef struct {char magic[8];uint32_t schema,count,meta_size;uint64_t title;uint32_t version;char root[128];InstallEntry entries[INSTALL_MAX];uint8_t metadata[META_MAX],hash[32];} InstallPlan;
typedef struct {uint32_t phase;uint8_t placeholder[16];} EntryState;
typedef struct {char magic[8];uint64_t sequence;uint8_t plan_hash[32];uint32_t phase,meta_phase;EntryState entries[INSTALL_MAX];uint8_t hash[32];} InstallJournal;
#pragma pack(pop)
enum {RunActive=1,RunInstalled=2,RunCleanup=3,RunCleaned=4};
enum {EntryAbsent,EntryExternal,EntryCreateIntent,EntryCreated,EntryRegisterIntent,EntryRegistered,EntryVerified,EntryDeleteIntent,EntryDeleted};
enum {MetaAbsent,MetaSetIntent,MetaCommitted,MetaRemoveIntent,MetaRemoved};
