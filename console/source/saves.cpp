#include "common.hpp"
#include "safe_name.hpp"
#include "save_mapping.hpp"
#include <dirent.h>
#include <sstream>
namespace saves {
static u64 Title;static u64 OnlySave;
static PadState pad;
static void check(Result rc,const char* label){if(R_FAILED(rc)){note("SAVE_ERROR %s rc=%08x\n",label,rc);throw std::runtime_error(label);}}
static void pump(){::pump();}
using File=std::unique_ptr<FILE,decltype(&fclose)>;
struct SaveFs {FsFileSystem fs{};~SaveFs(){if(serviceIsActive(&fs.s))fsFsClose(&fs);}};
struct Dir {FsDir dir{};~Dir(){if(serviceIsActive(&dir.s))fsDirClose(&dir);}};
struct SourceFile {FsFile file{};~SourceFile(){if(serviceIsActive(&file.s))fsFileClose(&file);}};
static std::string hex(const u8* p,size_t n){static const char* d="0123456789abcdef";std::string s;for(size_t i=0;i<n;i++){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
static void saveFlush(FILE* f){if(fflush(f)||fsync(fileno(f)))throw std::runtime_error("destination flush failed");}
static void freshDir(const std::string& p){if(mkdir(p.c_str(),0777)){struct stat st{};require(companionScratch(p)&&!lstat(p.c_str(),&st)&&S_ISDIR(st.st_mode),"cannot create fresh backup directory");}}
static void bytes(const std::string& path,const void* data,size_t size){File f(fopen(path.c_str(),"wb"),fclose);if(!f||fwrite(data,1,size,f.get())!=size)throw std::runtime_error("cannot write snapshot");saveFlush(f.get());}
struct Entry {std::string path;u64 size;bool directory;};
static void backup(const FsSaveDataInfo& info,const std::string& root){
 vaultTransfer("Inspecting selected save","",0,0);
 if(info.save_data_space_id!=FsSaveDataSpaceId_User||info.save_data_rank!=FsSaveDataRank_Primary||(info.save_data_type!=FsSaveDataType_Account&&info.save_data_type!=FsSaveDataType_Device))throw std::runtime_error("unsupported selected game save type/rank/space; retained unchanged");
 FsSaveDataAttribute attr{};attr.application_id=Title;attr.uid=info.uid;attr.system_save_data_id=info.system_save_data_id;attr.save_data_type=info.save_data_type;attr.save_data_rank=info.save_data_rank;attr.save_data_index=info.save_data_index;
 SaveFs src;check(fsOpenReadOnlySaveDataFileSystem(&src.fs,FsSaveDataSpaceId_User,&attr),"read-only mount");
 char label[48];snprintf(label,sizeof(label),"/save-%016llx",(unsigned long long)info.save_data_id);std::string dest=root+label;freshDir(dest);freshDir(dest+"/files");bytes(dest+"/save-info.bin",&info,sizeof(info));bytes(dest+"/save-attribute.bin",&attr,sizeof(attr));
 std::vector<Entry> entries;std::vector<std::string> dirs={"/"};u64 total=0;
 for(size_t at=0;at<dirs.size();at++){
  pump();Dir directory;check(fsFsOpenDirectory(&src.fs,dirs[at].c_str(),FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&directory.dir),"open directory");
  for(;;){FsDirectoryEntry row{};s64 count=0;check(fsDirRead(&directory.dir,&count,1,&row),"read directory");if(!count)break;if(count!=1||!memchr(row.name,0,sizeof(row.name))||!safeSaveName(row.name))throw std::runtime_error("invalid save entry");
   std::string path=dirs[at]+row.name;bool isdir=row.type==FsDirEntryType_Dir;
   if(path.size()>=FS_MAX_PATH||entries.size()>=10000||(!isdir&&(row.type!=FsDirEntryType_File||row.file_size<0)))throw std::runtime_error("save bounds/type unsupported");
   for(const auto& previous:entries)if(foldedSavePath(previous.path)==foldedSavePath(path))throw std::runtime_error("duplicate/case-colliding save path");
   if(isdir){dirs.push_back(path+"/");freshDir(dest+"/files"+path);}else{if((u64)row.file_size>512ULL*1024*1024-total)throw std::runtime_error("save exceeds 512MiB diagnostic limit");total+=(u64)row.file_size;}
   entries.push_back({path,isdir?0:(u64)row.file_size,isdir});
  }
 }
 File manifest(fopen((dest+"/manifest.txt").c_str(),"wb"),fclose);if(!manifest)throw std::runtime_error("manifest open failed");std::vector<u8> buffer(256*1024);unsigned copied=0;
 for(const auto& entry:entries){pump();std::string encoded=hex((const u8*)entry.path.data(),entry.path.size());
  if(entry.directory){if(fprintf(manifest.get(),"DIR %s\n",encoded.c_str())<0)throw std::runtime_error("manifest write");continue;}
  SourceFile in;check(fsFsOpenFile(&src.fs,entry.path.c_str(),FsOpenMode_Read,&in.file),"open save file");s64 length=0;check(fsFileGetSize(&in.file,&length),"source size");if(length<0||(u64)length!=entry.size)throw std::runtime_error("source size changed");
  File out(companionStreaming()?nullptr:fopen((dest+"/files"+entry.path).c_str(),"wb"),fclose);if(companionStreaming())companionFileBegin(dest+"/files"+entry.path,entry.size);else if(!out)throw std::runtime_error("backup file open failed");Sha256Context hash;sha256ContextCreate(&hash);
  vaultTransfer("Backing up save / recovery copy",entry.path.c_str(),0,entry.size);for(u64 offset=0;offset<entry.size;){pump();u64 got=0;size_t amount=(size_t)std::min<u64>(buffer.size(),entry.size-offset);check(fsFileRead(&in.file,offset,buffer.data(),amount,FsReadOption_None,&got),"source read");require(got==amount,"short save read");if(companionStreaming())companionFileWrite(buffer.data(),amount);else require(fwrite(buffer.data(),1,amount,out.get())==amount,"short copy");sha256ContextUpdate(&hash,buffer.data(),amount);offset+=amount;vaultTransfer("Backing up save / recovery copy",entry.path.c_str(),offset,entry.size);}
  u8 original[32];sha256ContextGetHash(&hash,original);if(companionStreaming())companionFileEnd(hex(original,32));else {saveFlush(out.get());if(fclose(out.release()))throw std::runtime_error("backup close failed");
  File verify(fopen((dest+"/files"+entry.path).c_str(),"rb"),fclose);if(!verify)throw std::runtime_error("readback open failed");sha256ContextCreate(&hash);u64 read=0;vaultTransfer("Verifying save backup",entry.path.c_str(),0,entry.size);size_t n;
  while((n=fread(buffer.data(),1,buffer.size(),verify.get()))){pump();read+=n;vaultTransfer("Verifying save backup",entry.path.c_str(),read,entry.size);sha256ContextUpdate(&hash,buffer.data(),n);}u8 actual[32];sha256ContextGetHash(&hash,actual);if(ferror(verify.get())||read!=entry.size||memcmp(original,actual,32))throw std::runtime_error("backup checksum mismatch");}
  check(fsFileGetSize(&in.file,&length),"source final size");if(length<0||(u64)length!=entry.size)throw std::runtime_error("source changed during copy");
  if(fprintf(manifest.get(),"FILE %s %llu %s\n",hex(original,32).c_str(),(unsigned long long)entry.size,encoded.c_str())<0)throw std::runtime_error("manifest write");
  copied++;
 }
 saveFlush(manifest.get());if(fclose(manifest.release()))throw std::runtime_error("manifest close");
 static const char complete[]="Read-only source; destination checksum readback passed (SD or PC). Not a restore test.\n";
 bytes(dest+"/COMPLETE.txt",complete,sizeof(complete)-1);
 auto manifestBytes=::read(dest+"/manifest.txt",4*1024*1024);Sha256Context seal;sha256ContextCreate(&seal);sha256ContextUpdate(&seal,&attr,sizeof(attr));sha256ContextUpdate(&seal,manifestBytes.data(),manifestBytes.size());u8 sealed[32];sha256ContextGetHash(&seal,sealed);bytes(dest+"/seal.bin",sealed,32);
 note("SAVE_BACKUP_VERIFIED id=%016llx files=%u bytes=%llu\n",(unsigned long long)info.save_data_id,copied,(unsigned long long)total);
}
static bool runBackup(const char* root){
 padInitializeDefault(&pad);bool pass=false;
 try{
  check(pmdmntInitialize(),"PM init");u64 pid=0;Result result=pmdmntGetProcessId(&pid,Title);pmdmntExit();note("SELECTED_GAME_PROCESS_QUERY rc=%08x\n",result);if(result!=MAKERESULT(15,1))throw std::runtime_error("cannot prove selected game is closed");
  FsSaveDataInfoReader reader{};check(fsOpenSaveDataInfoReader(&reader,FsSaveDataSpaceId_User),"save enumeration");std::vector<FsSaveDataInfo> matching;
  try{for(unsigned page=0;;page++){pump();if(page>=4096)throw std::runtime_error("save enumeration limit");FsSaveDataInfo infos[16]{};s64 count=0;check(fsSaveDataInfoReaderRead(&reader,infos,16,&count),"save info read");if(count<0||count>16)throw std::runtime_error("bad save count");for(s64 i=0;i<count;i++)if(infos[i].application_id==Title&&(!OnlySave||infos[i].save_data_id==OnlySave))matching.push_back(infos[i]);if(count<16)break;}}catch(...){fsSaveDataInfoReaderClose(&reader);throw;}fsSaveDataInfoReaderClose(&reader);
  note("SELECTED_GAME_SAVES_FOUND=%u\n",(unsigned)matching.size());if(matching.empty())throw std::runtime_error("no selected game save found; verify account/save visibility");
  for(const auto& info:matching)backup(info,root);
  pass=true;
 }catch(const std::exception& e){note("SAVE_BACKUP_STOPPED %s; partial folders retained without completion marker.\n",e.what());}
 note("SAVE_BACKUP_ALL_VERIFIED=%u\n",pass);return pass;
}

struct Saved {std::string folder;FsSaveDataAttribute attr{};std::vector<Entry> entries;std::vector<std::string> hashes;};
static std::string decode(const std::string&s){
 require(!s.empty()&&s.size()%2==0&&s.size()<FS_MAX_PATH*2,"encoded path bounds");std::string out;
 for(size_t i=0;i<s.size();i+=2){auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("invalid hex");};out+=(char)(digit(s[i])*16+digit(s[i+1]));}
 require(out.size()>1&&out[0]=='/'&&out.back()!='/',"save path form");size_t at=1;
 while(at<out.size()){size_t end=out.find('/',at);if(end==std::string::npos)end=out.size();auto name=out.substr(at,end-at);require(name.find('\0')==std::string::npos&&safeSaveName(name.c_str()),"unsafe save path");at=end+1;}return out;
}
static std::vector<Saved> loadSaves(u64 title,const std::string&root){
 std::vector<Saved> saves;DIR*d=opendir(root.c_str());require(d,"save snapshot folder missing");std::vector<std::string>names;
 while(auto*e=readdir(d)){std::string n=e->d_name;if(n.size()==21&&n.substr(0,5)=="save-"&&n.find_first_not_of("0123456789abcdef",5)==std::string::npos)names.push_back(n);}closedir(d);
 require(!names.empty()&&names.size()<=32,"unsupported save container count");std::sort(names.begin(),names.end());
 for(const auto&name:names){Saved s;s.folder=root+"/"+name;auto attr=::read(s.folder+"/save-attribute.bin",sizeof(s.attr));require(attr.size()==sizeof(s.attr),"save attribute size");memcpy(&s.attr,attr.data(),attr.size());
  require(s.attr.application_id==title&&s.attr.save_data_rank==FsSaveDataRank_Primary&&(s.attr.save_data_type==FsSaveDataType_Account||s.attr.save_data_type==FsSaveDataType_Device)&&!s.attr.system_save_data_id,"save identity/type unsupported");
  auto data=::read(s.folder+"/manifest.txt",4*1024*1024);auto seal=::read(s.folder+"/seal.bin",32);Sha256Context h;sha256ContextCreate(&h);sha256ContextUpdate(&h,&s.attr,sizeof(s.attr));sha256ContextUpdate(&h,data.data(),data.size());u8 digest[32];sha256ContextGetHash(&h,digest);require(seal.size()==32&&!memcmp(seal.data(),digest,32),"save manifest/identity checksum");
  std::istringstream lines(std::string(data.begin(),data.end()));std::string line;u64 total=0;
  while(std::getline(lines,line)){require(s.entries.size()<10000,"save entry limit");std::istringstream row(line);std::string type,hash,path,extra;u64 size=0;row>>type;
   if(type=="DIR"){require(bool(row>>path)&&!(row>>extra),"directory manifest syntax");hash="";}
   else{require(type=="FILE"&&bool(row>>hash>>size>>path)&&!(row>>extra)&&hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos,"file manifest syntax");require(size<=512ULL*1024*1024-total,"save size limit");total+=size;}
   path=decode(path);for(const auto&e:s.entries)require(foldedSavePath(path)!=foldedSavePath(e.path),"duplicate save path");auto parent=path.substr(0,path.rfind('/'));if(!parent.empty()){bool exists=false;for(const auto&e:s.entries)exists|=e.directory&&e.path==parent;require(exists,"missing/out-of-order parent");}
   if(type=="FILE")require(hashFile(s.folder+"/files"+path,size)==hash,"save source hash mismatch");s.entries.push_back({path,size,type=="DIR"});s.hashes.push_back(hash);
  }
  for(const auto&previous:saves)require(memcmp(&previous.attr,&s.attr,sizeof(s.attr)),"duplicate save identity");saves.push_back(std::move(s));
 }return saves;
}
static void verifyLive(FsFileSystem&fs,const Saved&s){
 std::vector<std::string> dirs={"/"};unsigned observed=0;std::vector<u8>b(256*1024);
 for(size_t i=0;i<dirs.size();i++){Dir d;check(fsFsOpenDirectory(&fs,dirs[i].c_str(),FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles,&d.dir),"verify save directory");for(;;){FsDirectoryEntry e{};s64 n=0;check(fsDirRead(&d.dir,&n,1,&e),"verify save entries");if(!n)break;require(n==1&&memchr(e.name,0,sizeof(e.name))&&safeSaveName(e.name),"verify entry bounds");auto path=dirs[i]+e.name;size_t index=0;while(index<s.entries.size()&&s.entries[index].path!=path)index++;require(index<s.entries.size(),"unexpected restored file");const auto&w=s.entries[index];observed++;
   require((e.type==FsDirEntryType_Dir)==w.directory,"restored type mismatch");if(w.directory){dirs.push_back(path+"/");continue;}
   require(e.file_size>=0&&(u64)e.file_size==w.size,"restored size mismatch");SourceFile f;check(fsFsOpenFile(&fs,path.c_str(),FsOpenMode_Read,&f.file),"verify file open");Sha256Context h;sha256ContextCreate(&h);
   vaultTransfer("Verifying save contents",w.path.c_str(),0,w.size);for(u64 off=0;off<w.size;){pump();size_t amount=std::min<u64>(b.size(),w.size-off);u64 got=0;check(fsFileRead(&f.file,off,b.data(),amount,FsReadOption_None,&got),"verify read");require(got==amount,"short save read");sha256ContextUpdate(&h,b.data(),amount);off+=amount;vaultTransfer("Verifying save contents",w.path.c_str(),off,w.size);}u8 digest[32];sha256ContextGetHash(&h,digest);require(hex(digest,32)==s.hashes[index],"restored hash mismatch");
  }}require(observed==s.entries.size(),"missing restored entries");
}
static FsSaveDataAttribute destinationNow(u64 title,u64 id,const FsSaveDataAttribute&expected){
 require(id!=0,"Choose a destination save");
 FsSaveDataInfoReader reader{};check(fsOpenSaveDataInfoReader(&reader,FsSaveDataSpaceId_User),"destination enumeration");
 FsSaveDataAttribute found{};unsigned matches=0;bool ended=false;
 try{for(unsigned page=0;page<4096;page++){FsSaveDataInfo rows[16]{};s64 n=0;check(fsSaveDataInfoReaderRead(&reader,rows,16,&n),"destination info");require(n>=0&&n<=16,"destination count");
  for(s64 i=0;i<n;i++)if(rows[i].save_data_id==id){const auto&r=rows[i];require(r.application_id==title&&r.save_data_space_id==FsSaveDataSpaceId_User,"destination game/space changed");found.application_id=r.application_id;found.uid=r.uid;found.system_save_data_id=r.system_save_data_id;found.save_data_type=r.save_data_type;found.save_data_rank=r.save_data_rank;found.save_data_index=r.save_data_index;matches++;}
  if(n<16){ended=true;break;}
 }}catch(...){fsSaveDataInfoReaderClose(&reader);throw;}fsSaveDataInfoReaderClose(&reader);
 require(ended&&matches==1&&!memcmp(&found,&expected,sizeof(found)),"Destination identity changed; reopen Game Vault");return found;
}
static void restore(u64 title,const std::string&root,const std::string&onlyFolder,u64 destinationId,const FsSaveDataAttribute&expected){
 Title=title;OnlySave=destinationId;closed(title);require(!onlyFolder.empty(),"Select one source save");auto saved=loadSaves(title,root);
 saved.erase(std::remove_if(saved.begin(),saved.end(),[&](const auto&s){return s.folder!=root+"/"+onlyFolder;}),saved.end());require(saved.size()==1,"Selected save is unavailable or ambiguous");
 auto destination=destinationNow(title,destinationId,expected);
 require(compatibleSaveDestination(saved[0].attr,destination),"Incompatible destination game/save type/index");
 {SaveFs f;check(fsOpenReadOnlySaveDataFileSystem(&f.fs,FsSaveDataSpaceId_User,&destination),"Start Celeste once in the destination profile first");}
 std::string rollback="sdmc:/gamevault/backups/"+idstr(title)+"-"+idstr(armGetSystemTick());freshDir(rollback);require(runBackup(rollback.c_str()),"Destination recovery backup failed");auto recovery=loadSaves(title,rollback);
 require(recovery.size()==1&&!memcmp(&recovery[0].attr,&destination,sizeof(destination)),"Recovery identity mismatch");
 note("SAVE_RECOVERY_SNAPSHOT=%s\n",rollback.c_str());
 std::string intent="source="+root+"/"+onlyFolder+"\nrecovery="+rollback+"\ndestination_save="+idstr(destinationId)+"\nsource_uid="+hex((const u8*)&saved[0].attr.uid,sizeof(AccountUid))+"\ndestination_uid="+hex((const u8*)&destination.uid,sizeof(AccountUid))+"\n";
 ::write(rollback+"/RESTORE_INTENT.txt",intent.data(),intent.size());::write(rollback+"/destination-attribute.bin",&destination,sizeof(destination));note("SAVE_MAPPING %s",intent.c_str());closed(title);
 destinationNow(title,destinationId,expected);
 std::vector<u8>b(256*1024);
 for(const auto&s:saved){
  SaveFs target;check(fsOpenSaveDataFileSystem(&target.fs,FsSaveDataSpaceId_User,&destination),"save write mount");
  verifyLive(target.fs,recovery[0]);
  s64 freeBytes=0;check(fsFsGetFreeSpace(&target.fs,"/",&freeBytes),"destination free space");
  u64 wanted=0,current=0;for(const auto&e:s.entries)wanted+=e.size;for(const auto&e:recovery[0].entries)current+=e.size;
  require(freeBytes>=0&&wanted<=current+(u64)freeBytes,"Destination save too small; no files changed");
  check(fsFsCleanDirectoryRecursively(&target.fs,"/"),"clear selected save tree");
  for(size_t i=0;i<s.entries.size();i++){const auto&e=s.entries[i];pump();
   if(e.directory){check(fsFsCreateDirectory(&target.fs,e.path.c_str()),"restore directory");continue;}
   check(fsFsCreateFile(&target.fs,e.path.c_str(),e.size,0),"restore file create");SourceFile dest;check(fsFsOpenFile(&target.fs,e.path.c_str(),FsOpenMode_Write,&dest.file),"restore file open");File in(fopen((s.folder+"/files"+e.path).c_str(),"rb"),fclose);require(bool(in),"restore source open");Sha256Context h;sha256ContextCreate(&h);
   vaultTransfer("Restoring save",e.path.c_str(),0,e.size);for(u64 off=0;off<e.size;){pump();size_t amount=std::min<u64>(b.size(),e.size-off);require(fread(b.data(),1,amount,in.get())==amount,"source short read");sha256ContextUpdate(&h,b.data(),amount);check(fsFileWrite(&dest.file,off,b.data(),amount,FsWriteOption_None),"save write");off+=amount;vaultTransfer("Restoring save",e.path.c_str(),off,e.size);}u8 hash[32];sha256ContextGetHash(&h,hash);require(fgetc(in.get())==EOF&&!ferror(in.get())&&hex(hash,32)==s.hashes[i],"source changed before commit");check(fsFileFlush(&dest.file),"save flush");
  }
  verifyLive(target.fs,s);vaultTransfer("Committing restored save","",0,0);check(fsFsCommit(&target.fs),"save commit");fsFsClose(&target.fs);target.fs={};check(fsOpenReadOnlySaveDataFileSystem(&target.fs,FsSaveDataSpaceId_User,&destination),"reopen committed save");verifyLive(target.fs,s);note("SAVE_CONTAINER_RESTORED_VERIFIED\n");
 }
 static const char done[]="All selected save trees committed and verified.\n";::write(rollback+"/RESTORE_DONE.txt",done,sizeof(done)-1);note("SAVE_RESTORE_VERIFIED Recovery copy retained at %s\n",rollback.c_str());
}

}
void backupSaves(u64 t,const std::string&d,u64 only){saves::Title=t;saves::OnlySave=only;require(saves::runBackup(d.c_str()),"Save backup incomplete; retain partial folder");}
void validateImportedSaves(u64 t,const std::string&d){(void)saves::loadSaves(t,d);}
void restoreSaves(u64 t,const std::string&d,const std::string&folder,u64 id,const FsSaveDataAttribute&attr){saves::restore(t,d,folder,id,attr);}
