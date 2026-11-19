#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "format.h"
#include "metadata_layout.h"
#include "progress.h"
#include "recovery.h"
#include "cycle.h"
static char workdir[256];
static char sourcedir[160];
static char planfile[FS_MAX_PATH];
static u64 application;
static u8 package_type;
static InstallPlan plan;
static InstallJournal journal;
static NcmContentStorage cs;
static NcmContentMetaDatabase db;
static NcmContentMetaKey key;
static PadState pad;
static FILE* logf;
static unsigned char buffer[1024*1024],metabuf[META_MAX];
static bool have_journal=false;
extern void vaultProgress(const char*);
static void note(const char* fmt,...){char line[1024];va_list a;va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);if(logf){fputs(line,logf);fflush(logf);}vaultProgress(line);}
static bool ok(const char* name,Result rc){if(R_FAILED(rc))note("ERROR %s=%08x\n",name,rc);return R_SUCCEEDED(rc);}
extern bool vaultAlive(void);
static bool alive(void){return vaultAlive();}
static bool digest_equal(const void* data,size_t size,const u8 hash[32]){u8 actual[32];sha256CalculateHash(actual,data,size);return !memcmp(actual,hash,32);}
static void content_id(unsigned i,NcmContentId* id){memcpy(id,plan.entries[i].id,16);}
static void placeholder(unsigned i,NcmPlaceHolderId* id){memcpy(id,journal.entries[i].placeholder,16);}
static bool save(void){
 journal.sequence++;sha256CalculateHash(journal.hash,&journal,offsetof(InstallJournal,hash));
 char path[FS_MAX_PATH];snprintf(path,sizeof(path),"%s/journal-%u.bin",workdir,(unsigned)(journal.sequence&1));
 FILE* f=fopen(path,"wb");if(!f)return false;
 bool good=fwrite(&journal,1,sizeof(journal),f)==sizeof(journal);if(fflush(f)||fsync(fileno(f)))good=false;if(fclose(f))good=false;
 InstallJournal back;f=fopen(path,"rb");if(!f)return false;
 good=good&&fread(&back,1,sizeof(back),f)==sizeof(back)&&fgetc(f)==EOF&&!ferror(f)&&!memcmp(&back,&journal,sizeof(back));fclose(f);
 if(!good)note("JOURNAL WRITE/READBACK FAILED.\n");return good;
}
static bool load_journal(void){
 bool any=false,valid=false;InstallJournal candidate;
 for(unsigned i=0;i<2;i++){char path[FS_MAX_PATH];snprintf(path,sizeof(path),"%s/journal-%u.bin",workdir,i);errno=0;FILE* f=fopen(path,"rb");if(!f){if(errno!=ENOENT)return false;continue;}any=true;
  bool good=fread(&candidate,1,sizeof(candidate),f)==sizeof(candidate)&&fgetc(f)==EOF&&!ferror(f);fclose(f);
  good=good&&journal_valid(&candidate,digest_equal);
  if(good&&(!valid||candidate.sequence>journal.sequence)){journal=candidate;valid=true;}
 }
 if(any&&!valid){note("Both journal slots invalid; no writes permitted.\n");return false;}
 if(valid&&memcmp(journal.plan_hash,plan.hash,32)){note("Package differs from journal. Restore original package before recovery.\n");return false;}
 have_journal=valid;return true;
}
static bool load_plan(void){
 FILE* f=fopen(planfile,"rb");if(!f)return false;
 bool good=fread(&plan,1,sizeof(plan),f)==sizeof(plan)&&fgetc(f)==EOF&&!ferror(f);fclose(f);
 if(!good||memcmp(plan.magic,"GBPLAN01",8)||(plan.schema!=1&&plan.schema!=2)||!plan.count||plan.count>INSTALL_MAX||plan.meta_size<24||plan.meta_size>META_MAX||!digest_equal(&plan,offsetof(InstallPlan,hash),plan.hash))return false;
 if(!memchr(plan.root,0,sizeof(plan.root))||plan.root[0]!='/'||strstr(plan.root,".."))return false;
 for(const char* p=plan.root;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='-'))return false;
 NcmContentMetaHeader hdr;memcpy(&hdr,plan.metadata,sizeof(hdr));
 
 unsigned start=8+hdr.extended_header_size;
 if(!vaultMetadataLayoutValid(&plan,package_type,application))return false;

 unsigned meta=0,program=0;
 for(unsigned i=0;i<plan.count;i++){NcmContentInfo ci;memcpy(&ci,plan.metadata+start+i*24,sizeof(ci));u64 size=0;ncmContentInfoSizeToU64(&ci,&size);InstallEntry* e=&plan.entries[i];
  if(memcmp(e->id,&ci.content_id,16)||e->size!=size||!size||e->type!=ci.content_type||e->attr!=ci.attr||ci.id_offset||e->reserved)return false;
  if(e->type!=0&&e->type!=1&&e->type!=2&&e->type!=3&&e->type!=5)return false;
  meta+=e->type==0;program+=e->type==1;
  for(unsigned j=0;j<i;j++)if(!memcmp(e->id,plan.entries[j].id,16))return false;
 }
 if(meta!=1||!program)return false;
 key=(NcmContentMetaKey){.id=plan.title,.version=plan.version,.type=package_type};return true;
}
static void source_path(unsigned i,char path[256]){char id[33];for(unsigned j=0;j<16;j++)snprintf(id+j*2,3,"%02x",plan.entries[i].id[j]);snprintf(path,256,"%s/%s%s",sourcedir,id,plan.entries[i].type==0?".cnmt.nca":".nca");}
static bool verify(unsigned i,bool registered){
 InstallEntry* e=&plan.entries[i];NcmContentId id;content_id(i,&id);FILE* f=NULL;
 if(registered){s64 size=0;if(!ok("ContentSize",ncmContentStorageGetSizeFromContentId(&cs,&size,&id))||size!=(s64)e->size)return false;}
 else{char path[256];source_path(i,path);f=fopen(path,"rb");if(!f){note("Missing source %s\n",path);return false;}}
 char progress_path[256];source_path(i,progress_path);vaultTransfer(registered?"Verifying installed archive":"Checking source archive",progress_path,0,e->size);Sha256Context h;sha256ContextCreate(&h);bool good=true;
 for(u64 off=0;off<e->size;){size_t n=e->size-off>sizeof(buffer)?sizeof(buffer):(size_t)(e->size-off);
  if(!alive()){good=false;break;}
  if(registered){if(!ok("Readback",ncmContentStorageReadContentIdFile(&cs,buffer,n,&id,off))){good=false;break;}}
  else if(fread(buffer,1,n,f)!=n){good=false;break;}
  sha256ContextUpdate(&h,buffer,n);off+=n;vaultTransfer(registered?"Verifying installed archive":"Checking source archive",progress_path,off,e->size);
  if(off%(256*1024*1024)==0)note("VERIFY %u %llu/%llu\n",i,(unsigned long long)off,(unsigned long long)e->size);
 }
 if(f){if(fgetc(f)!=EOF||ferror(f))good=false;fclose(f);}
 u8 hash[32];sha256ContextGetHash(&h,hash);good=good&&!memcmp(hash,e->hash,32);
 note("%s_HASH entry=%u passed=%u\n",registered?"REGISTERED":"SOURCE",i,good);return good;
}
static bool has(unsigned i,bool* exists){NcmContentId id;content_id(i,&id);return ok("HasContent",ncmContentStorageHas(&cs,exists,&id));}
static bool meta_matches(void){u64 size=0;return ok("GetMetadata",ncmContentMetaDatabaseGet(&db,&key,&size,metabuf,sizeof(metabuf)))&&size==plan.meta_size&&!memcmp(metabuf,plan.metadata,plan.meta_size);}
static bool begin(void){
 // Verify every source before recording ownership or modifying managed storage.
 for(unsigned i=0;i<plan.count;i++)if(!verify(i,false))return false;
 if(have_journal)return true;
 vaultTransfer("Committing archive metadata","",0,0);
 bool present=false;if(!ok("MetadataHas",ncmContentMetaDatabaseHas(&db,&present,&key))){return false;}
 if(present&&!meta_matches()){note("Different metadata; stopped.\n");return false;}
 memset(&journal,0,sizeof(journal));memcpy(journal.magic,"MSDJRNL1",8);memcpy(journal.plan_hash,plan.hash,32);journal.phase=RunActive;if(present)journal.meta_phase=MetaCommitted;
 u64 required=64*1024*1024ULL;
 for(unsigned i=0;i<plan.count;i++){if(!has(i,&present))return false;if(present){if(!verify(i,true))return false;journal.entries[i].phase=EntryExternal;}else{if(UINT64_MAX-required<plan.entries[i].size)return false;required+=plan.entries[i].size;}}
 s64 freebytes=0;if(!ok("FreeSpace",ncmContentStorageGetFreeSpaceSize(&cs,&freebytes))||freebytes<0||(u64)freebytes<required){note("Insufficient SD space.\n");return false;}
 if(!save())return false;have_journal=true;return true;
}
static bool stage(unsigned i){
 EntryState* st=&journal.entries[i];NcmContentId id;content_id(i,&id);NcmPlaceHolderId ph;placeholder(i,&ph);bool present=false,ph_present=false;
 if(!has(i,&present))return false;
 if(st->phase==EntryExternal){if(!present)return false;return verify(i,true);}
 if(present){
  if(st->phase<EntryRegisterIntent){note("Unexpected content appeared; stop for review.\n");return false;}
  if(!verify(i,true))return false;st->phase=EntryVerified;return save();
 }
 if(st->phase>=EntryRegistered){note("Recorded registered content disappeared; stop for review.\n");return false;}
 if(st->phase==EntryAbsent){if(!ok("GeneratePlaceholder",ncmContentStorageGeneratePlaceHolderId(&cs,&ph)))return false;memcpy(st->placeholder,&ph,16);st->phase=EntryCreateIntent;if(!save())return false;}
 if(!ok("HasPlaceholder",ncmContentStorageHasPlaceHolder(&cs,&ph_present,&ph)))return false;
 if(!ph_present){if(st->phase!=EntryCreateIntent){note("Placeholder missing after recorded creation.\n");return false;}if(!ok("CreatePlaceholder",ncmContentStorageCreatePlaceHolder(&cs,&id,&ph,plan.entries[i].size)))return false;}
 st->phase=EntryCreated;if(!save())return false;
 char path[256];source_path(i,path);FILE* f=fopen(path,"rb");if(!f)return false;bool good=true;Sha256Context digest;sha256ContextCreate(&digest);
 vaultTransfer(package_type==129?"Restoring update":"Restoring base game",path,0,plan.entries[i].size);for(u64 off=0;off<plan.entries[i].size;){size_t n=plan.entries[i].size-off>sizeof(buffer)?sizeof(buffer):(size_t)(plan.entries[i].size-off);
  if(!alive()||fread(buffer,1,n,f)!=n||!ok("WritePlaceholder",ncmContentStorageWritePlaceHolder(&cs,&ph,off,buffer,n))){good=false;break;}
  sha256ContextUpdate(&digest,buffer,n);off+=n;vaultTransfer(package_type==129?"Restoring update":"Restoring base game",path,off,plan.entries[i].size);if(off%(256*1024*1024)==0)note("COPY %u %llu\n",i,(unsigned long long)off);
 }
 u8 hash[32];sha256ContextGetHash(&digest,hash);if(fgetc(f)!=EOF||ferror(f)||memcmp(hash,plan.entries[i].hash,32))good=false;fclose(f);if(!good)return false;
 if(!ok("FlushPlaceholder",ncmContentStorageFlushPlaceHolder(&cs)))return false;
 st->phase=EntryRegisterIntent;if(!save())return false;
 if(!ok("Register",ncmContentStorageRegister(&cs,&id,&ph)))return false;
 st->phase=EntryRegistered;if(!save()||!verify(i,true))return false;st->phase=EntryVerified;return save();
}
static bool install(void){
 if(have_journal&&(journal.phase==RunCleanup||journal.phase==RunCleaned)){note("Cleanup transaction: installation refused.\n");return false;}
 if(!begin())return false;
 for(unsigned i=0;i<plan.count;i++){note("STAGE %u/%u\n",i+1,plan.count);if(!stage(i))return false;}
 for(unsigned i=0;i<plan.count;i++)if(plan.entries[i].type==NcmContentType_Program){
  NcmContentId id;content_id(i,&id);char path[FS_MAX_PATH]={0};u64 title=0;
  if(!ok("ProgramPath",ncmContentStorageGetPath(&cs,path,sizeof(path),&id))||strncmp(path,"@SdCardContent://",17)||!ok("ProgramIdentity",fsGetProgramId(&title,path,(FsContentAttributes)(plan.entries[i].attr&15)))||title!=application){note("Program identity does not match package; metadata not committed.\n");return false;}
 }
 vaultTransfer("Committing archive metadata","",0,0);
 bool present=false;if(!ok("MetadataHas",ncmContentMetaDatabaseHas(&db,&present,&key)))return false;
 if(present){if(journal.meta_phase==MetaAbsent||!meta_matches()){note("Unexpected metadata; stopped.\n");return false;}}
 else{if(journal.meta_phase==MetaCommitted){note("Committed metadata disappeared; stopped.\n");return false;}journal.meta_phase=MetaSetIntent;if(!save())return false;
  if(!ok("SetMetadata",ncmContentMetaDatabaseSet(&db,&key,plan.metadata,plan.meta_size)))return false;
 }
 if(!ok("CommitMetadata",ncmContentMetaDatabaseCommit(&db)))return false;
 ncmContentMetaDatabaseClose(&db);
 if(!ok("ReopenCommittedDatabase",ncmOpenContentMetaDatabase(&db,NcmStorageId_SdCard))||!meta_matches())return false;
 journal.meta_phase=MetaCommitted;journal.phase=RunInstalled;if(!save())return false;
 note("MANAGED_SD_INSTALL_VERIFIED: archives and exact original NCM record.\nHOME publication and launch are NOT implemented or claimed.\n");return true;
}

bool restorePackage(const char* directory,u64 title,u8 type,FILE* output){
 if(strlen(directory)>sizeof(workdir)-32||strlen(directory)>=sizeof(sourcedir))return false;
 strcpy(sourcedir,directory);
 strcpy(workdir,directory);snprintf(planfile,sizeof(planfile),"%s/package.bin",directory);application=title;package_type=type;logf=output;
 memset(&plan,0,sizeof(plan));memset(&journal,0,sizeof(journal));memset(&cs,0,sizeof(cs));memset(&db,0,sizeof(db));have_journal=false;
 padInitializeDefault(&pad);
 if(!load_plan())return false;
 char pointer[FS_MAX_PATH],name[25]={0};snprintf(pointer,sizeof(pointer),"%s/restore-current.txt",directory);
 FILE* f=fopen(pointer,"rb");bool pointer_present=f!=NULL;
 if(f){bool good=fread(name,1,24,f)==24&&fgetc(f)==EOF&&!ferror(f);fclose(f);
 if(!good||!cycle_name_valid(name))return false;
 snprintf(workdir,sizeof(workdir),"%s/%s",directory,name);if(!load_journal())return false;
 }else if(errno!=ENOENT)return false;
 enum CycleDecision decision=cycle_decision(have_journal,journal.phase);if(decision==CycleRefuse)return false;
 if(decision==CycleNew){
 snprintf(name,sizeof(name),"restore-%016llx",(unsigned long long)armGetSystemTick());snprintf(workdir,sizeof(workdir),"%s/%s",directory,name);
 if(mkdir(workdir,0777))return false;
 char next[FS_MAX_PATH];snprintf(next,sizeof(next),"%s/restore-current.next",directory);f=fopen(next,"wb");if(!f)return false;
 bool good=fwrite(name,1,24,f)==24;if(fflush(f)||fsync(fileno(f)))good=false;if(fclose(f))good=false;if(!good){note("CYCLE_POINTER_WRITE_FAILED errno=%d\n",errno);return false;}
 char previous[FS_MAX_PATH];snprintf(previous,sizeof(previous),"%s/previous-current.txt",workdir);
 if(!cycle_publish(pointer_present,pointer,previous,next,rename)){note("CYCLE_POINTER_PUBLISH_FAILED errno=%d\n",errno);return false;}
 note("CYCLE_POINTER_PUBLISHED previous_preserved=%u\n",pointer_present);
 char back[25]={0};f=fopen(pointer,"rb");if(!f)return false;good=fread(back,1,24,f)==24&&fgetc(f)==EOF&&!ferror(f)&&!memcmp(back,name,24);fclose(f);if(!good)return false;
 memset(&journal,0,sizeof(journal));have_journal=false;
 }
 note("RESTORE_TRANSACTION=%s\n",workdir);

 bool good=false;if(!ok("NCM",ncmInitialize()))return false;
 if(ok("OpenSD",ncmOpenContentStorage(&cs,NcmStorageId_SdCard))){
 if(ok("OpenDB",ncmOpenContentMetaDatabase(&db,NcmStorageId_SdCard))){good=install();ncmContentMetaDatabaseClose(&db);}ncmContentStorageClose(&cs);}
 ncmExit();return good;
}
