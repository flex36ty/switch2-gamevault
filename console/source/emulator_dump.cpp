#include "common.hpp"
#include "emulator_dump.hpp"
#include "network_runtime.hpp"
#include "GameMountBudget.h"
#include <dirent.h>
extern "C" Result gamePrepareInstalledLicense(NcmContentStorage*,const NcmContentInfo*,const char*,u64);
extern "C" void gameCloseLicenseContexts(void);
extern "C" Result gameOpenRomfsPath(FsStorage*,const char*,u8,u32);
extern "C" Result gameOpenMatchedBaseCode(FsFileSystem*,u64,u8,u8,const char*,Result*);
namespace {
EmulatorDump active;bool codeMounted=false,romMounted=false,blocked=false;
void collect(const std::string& root,const std::string& prefix){
 std::vector<std::string> dirs={""};
 for(size_t at=0;at<dirs.size();++at){pump();std::string base=root+dirs[at];DIR*d=opendir(base.c_str());require(d,"Cannot enumerate emulator dump");
  try{while(auto*e=readdir(d)){std::string n=e->d_name;if(n=="."||n=="..")continue;require(n.find_first_of("\\:\r\n")==std::string::npos,"Invalid export filename");std::string rel=dirs[at]+n,path=root+rel;struct stat st{};require(!lstat(path.c_str(),&st),"Export entry unavailable");if(S_ISDIR(st.st_mode))dirs.push_back(rel+"/");else{require(S_ISREG(st.st_mode)&&st.st_size>=0,"Unsupported export entry");active.files.push_back({path,prefix+rel,(uint64_t)st.st_size});}require(dirs.size()+active.files.size()<=100000,"Export file limit exceeded");}closedir(d);}catch(...){closedir(d);throw;}
 }
}
void identity(FsFileSystem& code,u64 title){
 FsFile f{};check(fsFsOpenFile(&code,"/main.npdm",FsOpenMode_Read,&f),"Open executable metadata");
 std::vector<u8>b(0x10000);u64 got=0;Result rc=fsFileRead(&f,0,b.data(),b.size(),FsReadOption_None,&got);fsFileClose(&f);check(rc,"Read executable metadata");require(got>=0x80&&!memcmp(b.data(),"META",4),"Executable metadata invalid");u32 off;memcpy(&off,b.data()+0x70,4);require(off<=got&&got-off>=0x18&&!memcmp(b.data()+off,"ACI0",4),"Executable identity missing");u64 id;memcpy(&id,b.data()+off+0x10,8);require(id==title,"Executable title does not match RomFS");
 check(fsFsOpenFile(&code,"/main",FsOpenMode_Read,&f),"Open executable");u8 magic[4];got=0;rc=fsFileRead(&f,0,magic,4,FsReadOption_None,&got);fsFileClose(&f);check(rc,"Read executable header");require(got==4&&!memcmp(magic,"NSO0",4),"Executable is not NSO");
}
}
const EmulatorDump& currentEmulatorDump(){return active;}
void closeEmulatorDump(){if(codeMounted)fsdevUnmountDevice("gvdcode");if(romMounted)romfsUnmount("gvdrom");codeMounted=romMounted=false;gameCloseLicenseContexts();active={};}
const EmulatorDump& prepareEmulatorDump(uint64_t title){
 VaultStorageLease access;require(!blocked,"Resolver recovery is required before another export");require(title&&!(title&0xfff),"Invalid application ID");closed(title);
 if(active.title==title&&!active.files.empty())return active;
 closeEmulatorDump();check(ncmInitialize(),"NCM for emulator export");struct Exit{~Exit(){ncmExit();}}exit;
 try{
  bool found=false;NcmContentMetaKey key{};NcmContentInfo program{};NcmStorageId location=NcmStorageId_None;std::string source;
  for(auto loc:{NcmStorageId_SdCard,NcmStorageId_BuiltInUser}){
   MetaDb db;Storage cs;if(R_FAILED(ncmOpenContentMetaDatabase(&db.db,loc))||R_FAILED(ncmOpenContentStorage(&cs.cs,loc)))continue;
   if(R_FAILED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db.db,&key,title)))continue;
   if(key.type!=NcmContentMetaType_Application)continue;
   NcmContentInfo rows[64]{};s32 count=0;check(ncmContentMetaDatabaseListContentInfo(&db.db,&count,rows,64,&key,0),"List program content");require(count>0&&count<64,"Unsupported multi-package content list");unsigned programs=0;
   for(int i=0;i<count;i++)if(rows[i].content_type==NcmContentType_Program){program=rows[i];++programs;}
   require(programs==1&&program.id_offset==0,"Multi-program titles are not supported for matched dumps");
   char path[FS_MAX_PATH]{};check(ncmContentStorageGetPath(&cs.cs,path,sizeof(path),&program.content_id),"Resolve original base program");source=path;
   FsStorage rom{};Result rc=gameOpenRomfsPath(&rom,path,program.attr&15,1);
   if((rc&0x3fffff)==0x7d802){check(gamePrepareInstalledLicense(&cs.cs,&program,path,title),"Prepare existing installed license");rc=gameOpenRomfsPath(&rom,path,program.attr&15,1);}
   check(rc,"Base RomFS access unavailable");
   s64 size=0;romfs_header header{};rc=fsStorageGetSize(&rom,&size);if(R_SUCCEEDED(rc))rc=fsStorageRead(&rom,0,&header,sizeof(header));
   uint64_t offsets[]={header.dirHashTableOff,header.dirTableOff,header.fileHashTableOff,header.fileTableOff};uint64_t sizes[]={header.dirHashTableSize,header.dirTableSize,header.fileHashTableSize,header.fileTableSize};uint64_t needed=0;
   if(R_FAILED(rc)||size<=0||header.headerSize!=sizeof(header)||header.fileDataOff>(u64)size||!gameMetadataFits(size,offsets,sizes,0,128ULL*1024*1024,&needed)){fsStorageClose(&rom);throw std::runtime_error("RomFS metadata invalid or exceeds memory budget");}
   check(romfsMountFromStorage(rom,0,"gvdrom"),"Mount matched base RomFS");romMounted=true;
   FsFileSystem code{};Result restored=0;rc=gameOpenMatchedBaseCode(&code,title,(u8)loc,program.attr&15,path,&restored);
   note("EMULATOR_MATCH title=%s code=%08x resolver_restore=%08x version=%u\n",idstr(title).c_str(),rc,restored,key.version);
   if(R_FAILED(restored)){blocked=true;if(R_SUCCEEDED(rc))fsFsClose(&code);throw std::runtime_error("Resolver restoration failed; keep logs and restart before another launch");}
   check(rc,"Executable access unavailable in the running HEN");try{identity(code,title);}catch(...){fsFsClose(&code);throw;}
   // fsdevMountDevice takes ownership and closes the service even on failure.
   if(fsdevMountDevice("gvdcode",code)<0)throw std::runtime_error("Cannot mount executable filesystem");codeMounted=true;
   location=loc;found=true;break;
  }
  require(found,"No installed base program available");active.title=title;active.version=key.version;
  collect("gvdcode:/","exefs/");collect("gvdrom:/","romfs/");
  active.metadata="{\"schema\":1,\"title_id\":\""+idstr(title)+"\",\"scope\":\"base-only\",\"version\":"+std::to_string(key.version)+",\"storage\":"+std::to_string(location)+",\"program_content_id\":\""+hex((u8*)&program.content_id,16)+"\",\"selection_verified\":true,\"resolver_restored\":true,\"updates_included\":false}";
  return active;
 }catch(...){closeEmulatorDump();throw;}
}
std::string dumpEmulatorToSd(uint64_t title){
 VaultStorageLease access;const auto& dump=prepareEmulatorDump(title);
 mkdir("sdmc:/gamevault/emulator-dumps",0777);std::string root="sdmc:/gamevault/emulator-dumps/"+idstr(title)+"-"+idstr(armGetSystemTick());fresh(root);
 uint64_t bytes=0;for(const auto&f:dump.files){require(UINT64_MAX-bytes>=f.size,"Export size overflow");bytes+=f.size;}
 FsFileSystem sd{};check(fsOpenSdCardFileSystem(&sd),"Open SD capacity");s64 free=0;Result rc=fsFsGetFreeSpace(&sd,"/",&free);fsFsClose(&sd);check(rc,"Read SD capacity");require(free>=0&&(u64)free>=bytes+16*1024*1024,"Insufficient SD space");
 write(root+"/INCOMPLETE.txt","Dump in progress",16);std::string sums;std::vector<u8> buffer(262144);
 for(const auto&f:dump.files){pump();for(size_t at=0;(at=f.name.find('/',at))!=std::string::npos;++at)mkdir((root+"/"+f.name.substr(0,at)).c_str(),0777);
  std::string target=root+"/"+f.name;File input(fopen(f.path.c_str(),"rb"),fclose),output(fopen(target.c_str(),"wb"),fclose);require(bool(input)&&bool(output),"Open dump file");Sha256Context hash;sha256ContextCreate(&hash);uint64_t done=0;
  while(done<f.size){pump();size_t n=fread(buffer.data(),1,std::min<uint64_t>(buffer.size(),f.size-done),input.get());require(n>0,"Short source read");require(fwrite(buffer.data(),1,n,output.get())==n,"SD write failed");sha256ContextUpdate(&hash,buffer.data(),n);done+=n;vaultTransfer("Dumping emulator files",f.name.c_str(),done,f.size);}
  flush(output.get());require(!fclose(output.release()),"Close dump file");u8 digest[32];sha256ContextGetHash(&hash,digest);std::string expected=hex(digest,32);require(hashFile(target,f.size)==expected,"SD dump checksum mismatch");sums+=expected+"  "+f.name+"\n";
 }
 write(root+"/export.json",dump.metadata.data(),dump.metadata.size());write(root+"/SHA256SUMS.txt",sums.data(),sums.size());require(!unlink((root+"/INCOMPLETE.txt").c_str()),"Finish dump marker");note("EMULATOR_SD_VERIFIED title=%s files=%zu path=%s\n",idstr(title).c_str(),dump.files.size(),root.c_str());return root;
}
