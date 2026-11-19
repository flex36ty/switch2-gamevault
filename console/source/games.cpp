#include "common.hpp"
#include <cstddef>
#include "components.hpp"
#include "home_selection.hpp"
#include "paths.hpp"
extern "C" bool restorePackage(const char*,u64,u8,FILE*);
struct ServiceOwner{Service s{};~ServiceOwner(){if(serviceIsActive(&s))serviceClose(&s);}};
struct HomeRow{NcmContentMetaKey key;u8 storage,pad[7];};
static_assert(sizeof(HomeRow)==24);
static std::vector<HomeRow> home(u64 title,bool strictRestore=true){
 bool ended=false;unsigned found=0;
 for(int off=0;off<8192;off+=32){NsApplicationRecord r[32]{};s32 n=0;check(nsListApplicationRecord(r,32,off,&n),"HOME list");require(n>=0&&n<=32,"HOME bounds");for(int i=0;i<n;i++)found+=r[i].application_id==title;if(n<32){ended=true;break;}}
 require(ended&&found<=1,"HOME scan incomplete/ambiguous");if(!found)return {};
 ServiceOwner m;check(nsGetApplicationManagerInterface(&m.s),"HOME manager");std::vector<HomeRow> out;ended=false;
 for(u64 off=0;off<256;off+=16){
  HomeRow rows[16]{};s32 n=0;const struct{u64 off,id;} in={off,title};
  check(serviceDispatchInOut(&m.s,17,in,n,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},.buffers={{rows,sizeof(rows)}}),"HOME content records");
  note("HOME_CONTENT_PAGE title=%s offset=%llu count=%d\n",idstr(title).c_str(),(unsigned long long)off,n);
  require(n>=0&&n<=16,"HOME content page count invalid");
  for(int i=0;i<n;i++){const auto&r=rows[i];note("HOME_CONTENT_ROW key=%s type=%u version=%u storage=%u\n",idstr(r.key.id).c_str(),unsigned(r.key.type),unsigned(r.key.version),unsigned(r.storage));out.push_back(r);}
  if(n<16){ended=true;break;}
 }
 require(ended&&!out.empty(),"HOME content enumeration empty or incomplete");
 if(strictRestore)require(out.size()<=2,"Restore supports base/update HOME records only; extra records preserved by stopping");
 std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){if(a.key.type!=b.key.type)return a.key.type<b.key.type;if(a.key.id!=b.key.id)return a.key.id<b.key.id;if(a.key.version!=b.key.version)return a.key.version<b.key.version;return a.storage<b.storage;});return out;
}
static void validateKey(const Package&p,u64 title,unsigned){
 unsigned i=p.key.type==129;require(p.key.type==128||p.key.type==129,"Unsupported package type");
 require(title&&!(title&0xfff)&&p.key.id==title+(i?0x800:0)&&p.key.type==(i?129:128)&&!p.key.install_type&&!p.key.padding[0]&&!p.key.padding[1]&&(p.storage==4||p.storage==5),"Unsupported title/key/storage");
}
static std::string archiveName(const InstallEntry&e){return hex(e.id,16)+(e.type==0?".cnmt.nca":".nca");}
static void validatePlan(const InstallPlan&p,const Package&pkg,u64 title){
 u8 digest[32];sha256CalculateHash(digest,&p,offsetof(InstallPlan,hash));
 require(!memcmp(p.magic,"GBPLAN01",8)&&p.schema==1&&p.count>0&&p.count<=INSTALL_MAX&&p.meta_size>=32&&p.meta_size<=META_MAX&&!memcmp(digest,p.hash,32),"Invalid package digest/format");
 require(p.title==pkg.key.id&&p.version==pkg.key.version&&memchr(p.root,0,sizeof(p.root))&&backupRootValid(p.root),"Package identity/path mismatch");
 NcmContentMetaHeader hdr{};memcpy(&hdr,p.metadata,sizeof(hdr));unsigned start=8+hdr.extended_header_size;
 require(hdr.content_count==p.count&&!hdr.content_meta_count,"Unsupported metadata dependencies");
 if(pkg.key.type==128)require(hdr.extended_header_size==16&&p.meta_size==start+24*p.count,"Unsupported base metadata");
 else{u64 base=0;u32 extra=0;memcpy(&base,p.metadata+8,8);memcpy(&extra,p.metadata+20,4);require(hdr.extended_header_size==24&&base==title&&extra<=META_MAX&&p.meta_size==start+24*p.count+extra,"Unsupported update metadata");}
 unsigned meta=0,program=0;
 for(unsigned i=0;i<p.count;i++){NcmContentInfo ci{};memcpy(&ci,p.metadata+start+i*24,sizeof(ci));u64 size=0;ncmContentInfoSizeToU64(&ci,&size);const auto&e=p.entries[i];
 require(!memcmp(e.id,&ci.content_id,16)&&size==e.size&&size&&e.type==ci.content_type&&e.attr==ci.attr&&!ci.id_offset&&!e.reserved,"Unsupported multi-program/content layout");
 require(e.type==0||e.type==1||e.type==2||e.type==3||e.type==5,"Unsupported content type");
 for(unsigned j=0;j<i;j++)require(memcmp(e.id,p.entries[j].id,16),"Duplicate content ID");meta+=e.type==0;program+=e.type==1;
 }require(meta==1&&program==1,"Expected one metadata and one program archive");
}
void backupGame(u64 title,const std::string&dir,unsigned selected){
 closed(title);auto original=home(title,false);auto rows=selectBackupHomeRows(original,selected);note("HOME_BACKUP_SCOPE total=%zu selected=%zu excluded=%zu base_update_only=1\n",original.size(),rows.size(),original.size()-rows.size());Catalog catalog{};memcpy(catalog.magic,"GBCAT001",8);catalog.title=title;catalog.count=rows.size();
 check(ncmInitialize(),"NCM init");struct Exit{~Exit(){ncmExit();}} exit;
 std::vector<std::unique_ptr<InstallPlan>> plans;
 u64 needed=64*1024*1024ULL;
 // Validate all metadata and reserve space before copying any archive.
 for(unsigned i=0;i<catalog.count;i++){
  auto&pkg=catalog.packages[i];pkg.key=rows[i].key;pkg.storage=rows[i].storage;validateKey(pkg,title,i);
  auto p=std::make_unique<InstallPlan>();memset(p.get(),0,sizeof(*p));memcpy(p->magic,"GBPLAN01",8);p->schema=1;p->title=pkg.key.id;p->version=pkg.key.version;
  MetaDb db;check(ncmOpenContentMetaDatabase(&db.db,(NcmStorageId)pkg.storage),"source metadata");u64 size=0;check(ncmContentMetaDatabaseGet(&db.db,&pkg.key,&size,p->metadata,META_MAX),"read metadata");require(size>=32&&size<=META_MAX,"metadata size");p->meta_size=size;
  NcmContentMetaHeader hdr{};memcpy(&hdr,p->metadata,sizeof(hdr));p->count=hdr.content_count;unsigned start=8+hdr.extended_header_size;require(p->count>0&&p->count<=INSTALL_MAX&&start<=size&&p->count*24<=size-start,"metadata bounds");
  std::string source=dir.substr(5)+"/p"+std::to_string(i);require(source.size()<sizeof(p->root),"path too long");strcpy(p->root,source.c_str());
  for(unsigned k=0;k<p->count;k++){NcmContentInfo ci{};memcpy(&ci,p->metadata+start+24*k,sizeof(ci));auto&e=p->entries[k];memcpy(e.id,&ci.content_id,16);ncmContentInfoSizeToU64(&ci,&e.size);e.type=ci.content_type;e.attr=ci.attr;require(e.size<=(1ULL<<40)&&needed+e.size>=needed,"archive size overflow");needed+=e.size;}
  sha256CalculateHash(p->hash,p.get(),offsetof(InstallPlan,hash));validatePlan(*p,pkg,title);plans.push_back(std::move(p));
 }
 if(!companionStreaming()){FsFileSystem sd{};check(fsOpenSdCardFileSystem(&sd),"SD space open");s64 free=0;Result space=fsFsGetFreeSpace(&sd,"/",&free);fsFsClose(&sd);check(space,"SD space");require(free>=0&&(u64)free>=needed,"Not enough SD backup space");}
 std::vector<u8> buffer(1024*1024);
 for(unsigned i=0;i<catalog.count;i++){
  auto&p=*plans[i];std::string path=dir+"/p"+std::to_string(i);fresh(path);Storage storage;check(ncmOpenContentStorage(&storage.cs,(NcmStorageId)catalog.packages[i].storage),"source storage");
  for(unsigned k=0;k<p.count;k++){
   pump();auto&e=p.entries[k];NcmContentId cid{};memcpy(&cid,e.id,16);s64 size=0;check(ncmContentStorageGetSizeFromContentId(&storage.cs,&size,&cid),"source size");require(size>=0&&(u64)size==e.size,"archive size mismatch");
   std::string file=path+"/"+archiveName(e);File out(companionStreaming()?nullptr:fopen(file.c_str(),"wb"),fclose);if(companionStreaming())companionFileBegin(file,e.size);else require(bool(out),"archive destination");Sha256Context hash;sha256ContextCreate(&hash);
   vaultTransfer(catalog.packages[i].key.type==129?"Backing up update":"Backing up base game",file.c_str(),0,e.size);for(u64 off=0;off<e.size;){pump();size_t n=std::min<u64>(buffer.size(),e.size-off);check(ncmContentStorageReadContentIdFile(&storage.cs,buffer.data(),n,&cid,off),"archive read");if(companionStreaming())companionFileWrite(buffer.data(),n);else require(fwrite(buffer.data(),1,n,out.get())==n,"archive write");sha256ContextUpdate(&hash,buffer.data(),n);off+=n;vaultTransfer(catalog.packages[i].key.type==129?"Backing up update":"Backing up base game",file.c_str(),off,e.size);if(off%(256*1024*1024)==0)note("BACKUP %u/%u bytes=%llu\n",i,k,(unsigned long long)off);}
   sha256ContextGetHash(&hash,e.hash);if(companionStreaming())companionFileEnd(hex(e.hash,32));else {flush(out.get());require(!fclose(out.release()),"archive close");require(hashFile(file,e.size)==hex(e.hash,32),"archive readback hash");}note("ARCHIVE_BACKUP_VERIFIED %s\n",archiveName(e).c_str());
  }
  sha256CalculateHash(p.hash,&p,offsetof(InstallPlan,hash));write(path+"/package.bin",&p,sizeof(p));memcpy(catalog.hashes[i],p.hash,32);
 }
 vaultTransfer("Finalizing backup metadata","",0,0);
 // Snapshot records again to detect version changes during the export.
 auto final=home(title,false);require(final.size()==original.size()&&!memcmp(final.data(),original.data(),original.size()*sizeof(HomeRow)),"HOME changed during backup");
 sha256CalculateHash(catalog.digest,&catalog,offsetof(Catalog,digest));write(dir+"/catalog.bin",&catalog,sizeof(catalog));note("GAME_BACKUP_VERIFIED title=%s packages=%u (base/update only)\n",idstr(title).c_str(),catalog.count);
}
static bool equal(const std::vector<HomeRow>&a,const std::vector<HomeRow>&b){return a.size()==b.size()&&(a.empty()||!memcmp(a.data(),b.data(),a.size()*sizeof(HomeRow)));}
void companionValidateImport(const std::string&dir){
 struct stat st{};if(stat((dir+"/catalog.bin").c_str(),&st))return;
 auto bytes=read(dir+"/catalog.bin",sizeof(Catalog));require(bytes.size()==sizeof(Catalog),"Imported catalog size");Catalog c{};memcpy(&c,bytes.data(),sizeof(c));u8 digest[32];sha256CalculateHash(digest,&c,offsetof(Catalog,digest));
 require(!memcmp(c.magic,"GBCAT001",8)&&c.count>0&&c.count<=2&&!c.reserved&&!memcmp(digest,c.digest,32)&&dir.substr(dir.size()-33,16)==idstr(c.title),"Imported catalog invalid");
 
 for(unsigned i=0;i<c.count;i++){validateKey(c.packages[i],c.title,i);auto b=read(dir+"/p"+std::to_string(i)+"/package.bin",sizeof(InstallPlan));require(b.size()==sizeof(InstallPlan),"Imported plan size");auto plan=std::make_unique<InstallPlan>();memcpy(plan.get(),b.data(),sizeof(*plan));validatePlan(*plan,c.packages[i],c.title);require(!memcmp(plan->hash,c.hashes[i],32),"Imported plan checksum");
  for(unsigned j=0;j<plan->count;j++)require(hashFile(dir+"/p"+std::to_string(i)+"/"+archiveName(plan->entries[j]),plan->entries[j].size)==hex(plan->entries[j].hash,32),"Imported archive checksum");

 }
}
static void push(u64 title,const std::vector<HomeRow>&rows){ServiceOwner m;check(nsGetApplicationManagerInterface(&m.s),"HOME manager");const struct{u8 event,pad[7];u64 title;}in={3,{},title};check(serviceDispatchIn(&m.s,16,in,.buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},.buffers={{rows.data(),rows.size()*sizeof(HomeRow)}}),"HOME publish");}
void restoreGame(u64 title,const std::string&dir,unsigned selected){
 closed(title);auto raw=read(dir+"/catalog.bin",sizeof(Catalog));require(raw.size()==sizeof(Catalog),"catalog size");Catalog c{};memcpy(&c,raw.data(),sizeof(c));u8 hash[32];sha256CalculateHash(hash,&c,offsetof(Catalog,digest));require(!memcmp(c.magic,"GBCAT001",8)&&c.title==title&&c.count>0&&c.count<=2&&!c.reserved&&!memcmp(hash,c.digest,32),"catalog invalid");
 unsigned available=0;for(unsigned i=0;i<c.count;i++){validateKey(c.packages[i],title,i);unsigned b=components::bit(c.packages[i].key.type);require(!(available&b),"duplicate package type");available|=b;}require(components::valid(selected,available),"Selected software not in this backup");
 auto before=home(title);std::vector<HomeRow>chosen;
 for(const auto&r:before){Package p{};p.key=r.key;p.storage=r.storage;validateKey(p,title,0);}
 bool installedBase=std::any_of(before.begin(),before.end(),[](const auto&r){return r.key.type==128;});
 require(!components::needsBase(selected,installedBase),"Restore the base game before restoring this update");
 std::vector<std::unique_ptr<InstallPlan>> plans;std::vector<unsigned> indices;
 for(unsigned i=0;i<c.count;i++){
  if(!(selected&components::bit(c.packages[i].key.type)))continue;
  auto data=read(dir+"/p"+std::to_string(i)+"/package.bin",sizeof(InstallPlan));require(data.size()==sizeof(InstallPlan),"plan size");auto p=std::make_unique<InstallPlan>();memcpy(p.get(),data.data(),sizeof(*p));validatePlan(*p,c.packages[i],title);require(!memcmp(p->hash,c.hashes[i],32),"catalog/plan mismatch");
  for(unsigned k=0;k<p->count;k++)require(hashFile(dir+"/p"+std::to_string(i)+"/"+archiveName(p->entries[k]),p->entries[k].size)==hex(p->entries[k].hash,32),"source hash mismatch");
  HomeRow row{};row.key=c.packages[i].key;row.storage=5;chosen.push_back(row);
  plans.push_back(std::move(p));indices.push_back(i);
 }
 auto wanted=mergeHomeRows(before,chosen);
 check(ncmInitialize(),"NCM preflight");try{
  // Verify every unselected component remains installed at its current location.
  for(const auto&r:before)if(!(selected&components::bit(r.key.type))){MetaDb db;check(ncmOpenContentMetaDatabase(&db.db,(NcmStorageId)r.storage),"retained component database");bool present=false;check(ncmContentMetaDatabaseHas(&db.db,&present,&r.key),"retained component exists");require(present,"Unselected installed component disappeared");}
  MetaDb db;check(ncmOpenContentMetaDatabase(&db.db,NcmStorageId_SdCard),"SD database");
  for(unsigned index:indices){const auto&key=c.packages[index].key;NcmContentMetaKey keys[16]{};s32 total=0,n=0;check(ncmContentMetaDatabaseList(&db.db,&total,&n,keys,16,(NcmContentMetaType)key.type,key.id,0,UINT64_MAX,NcmContentInstallType_Full),"SD versions");require(total==n&&n>=0&&n<=16,"version listing incomplete");for(int i=0;i<n;i++)require(!memcmp(&keys[i],&key,sizeof(key)),"Competing SD version present");}
 }catch(...){ncmExit();throw;}ncmExit();closed(title);
 for(unsigned index:indices)require(restorePackage((dir+"/p"+std::to_string(index)).c_str(),title,c.packages[index].key.type,logging),"SD restore stopped; retain backup and journals");
 check(ncmInitialize(),"NCM final verification");try{Storage storage;check(ncmOpenContentStorage(&storage.cs,NcmStorageId_SdCard),"SD storage");std::vector<u8>b(1024*1024);for(const auto&p:plans)for(unsigned k=0;k<p->count;k++){const auto&e=p->entries[k];NcmContentId id{};memcpy(&id,e.id,16);Sha256Context h;sha256ContextCreate(&h);for(u64 off=0;off<e.size;){pump();size_t n=std::min<u64>(b.size(),e.size-off);check(ncmContentStorageReadContentIdFile(&storage.cs,b.data(),n,&id,off),"SD final read");sha256ContextUpdate(&h,b.data(),n);off+=n;vaultTransfer("Verifying installed archive",archiveName(e).c_str(),off,e.size);}sha256ContextGetHash(&h,hash);require(!memcmp(hash,e.hash,32),"SD final hash mismatch");}}catch(...){ncmExit();throw;}ncmExit();
 vaultTransfer("Registering and checking HOME records","",0,0);
 require(equal(home(title),before),"HOME changed during restore");closed(title);
 struct Intent{char magic[8];u8 catalog[32];u32 count,selected;HomeRow rows[2];u8 hash[32];}intent{};
 memcpy(intent.magic,"GVHOME02",8);memcpy(intent.catalog,c.digest,32);intent.count=before.size();intent.selected=selected;if(!before.empty())memcpy(intent.rows,before.data(),before.size()*sizeof(HomeRow));sha256CalculateHash(intent.hash,&intent,offsetof(Intent,hash));std::string stamp=idstr(armGetSystemTick());write(dir+"/home-intent-"+stamp+".bin",&intent,sizeof(intent));
 if(!equal(before,wanted))push(title,wanted);require(equal(home(title),wanted),"HOME record readback failed");
 NsApplicationContentMetaStatus status[3]{};s32 count=0;check(nsListApplicationContentMetaStatus(title,0,status,3,&count),"HOME status");require(count==(s32)wanted.size(),"HOME status count");for(const auto&r:wanted){unsigned hits=0;for(int i=0;i<count;i++)hits+=status[i].application_id==r.key.id&&status[i].version==r.key.version&&status[i].meta_type==r.key.type&&status[i].storageID==r.storage;require(hits==1,"HOME location status mismatch");note("HOME_COMPONENT_VERIFIED type=%u version=%u storage=%u\n",r.key.type,r.key.version,r.storage);}
 write(dir+"/RESTORED-"+std::to_string(selected)+"-"+stamp+".bin",c.digest,32);note("GAME_RESTORE_VERIFIED title=%s selected=%u packages=%zu\n",idstr(title).c_str(),selected,plans.size());
}
