#include "common.hpp"
#include "visual.hpp"
#include "ui.hpp"
#include <zlib.h>
#include <dirent.h>
static std::string decodeName(NacpStruct*n){
 const u8*data=(const u8*)n;size_t size=0x3000;std::vector<u8>out;
 if(n->titles_data_format==1){unsigned count=n->lang_data.compressed_data.buffer_size;if(count>sizeof(n->lang_data.compressed_data.buffer))return {};out.resize(32*0x300);z_stream z{};z.next_in=n->lang_data.compressed_data.buffer;z.avail_in=count;z.next_out=out.data();z.avail_out=out.size();if(inflateInit2(&z,-15)!=Z_OK)return {};int r=inflate(&z,Z_FINISH);size=z.total_out;inflateEnd(&z);if(r!=Z_STREAM_END)return {};data=out.data();}else if(n->titles_data_format!=0)return {};
 for(size_t at=0;at+0x300<=size;at+=0x300){size_t len=strnlen((const char*)data+at,0x200);if(len&&len<0x200){std::string s((const char*)data+at,len);for(char&c:s)if((unsigned char)c<32)c=' ';return s;}}return {};
}
static std::vector<u8> fsRead(FsFileSystem&fs,const char*path,size_t max){FsFile f{};if(R_FAILED(fsFsOpenFile(&fs,path,FsOpenMode_Read,&f)))return {};s64 size=0;auto rc=fsFileGetSize(&f,&size);std::vector<u8>b;if(R_SUCCEEDED(rc)&&size>0&&(u64)size<=max){b.resize(size);u64 got=0;rc=fsFileRead(&f,0,b.data(),b.size(),FsReadOption_None,&got);if(R_FAILED(rc)||got!=b.size())b.clear();}fsFileClose(&f);return b;}
static GameVisual control(u64 id,NcmStorageId loc){
 GameVisual v;MetaDb db;Storage cs;if(R_FAILED(ncmOpenContentMetaDatabase(&db.db,loc))||R_FAILED(ncmOpenContentStorage(&cs.cs,loc)))return v;
 NcmContentMetaKey key{};if(R_FAILED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db.db,&key,id)))return v;NcmContentInfo rows[32]{};s32 n=0;if(R_FAILED(ncmContentMetaDatabaseListContentInfo(&db.db,&n,rows,32,&key,0))||n<0||n>32)return v;
 for(int i=0;i<n;i++)if(rows[i].content_type==NcmContentType_Control){char path[FS_MAX_PATH]{};if(R_FAILED(ncmContentStorageGetPath(&cs.cs,path,sizeof(path),&rows[i].content_id)))continue;FsFileSystem fs{};Result mount=fsOpenFileSystemWithId(&fs,id,FsFileSystemType_ContentControl,path,(FsContentAttributes)(rows[i].attr&15));note("VISUAL_CONTROL_MOUNT title=%s storage=%u result=%08x\n",idstr(id).c_str(),unsigned(loc),mount);if(R_FAILED(mount))continue;
  auto b=fsRead(fs,"/control.nacp",sizeof(NacpStruct));if(b.size()==sizeof(NacpStruct))v.name=decodeName((NacpStruct*)b.data());
  v.jpeg=fsRead(fs,"/icon_AmericanEnglish.dat",0x200000);
  if(v.jpeg.empty()){FsDir d{};if(R_SUCCEEDED(fsFsOpenDirectory(&fs,"/",FsDirOpenMode_ReadFiles,&d))){for(unsigned k=0;k<128;k++){FsDirectoryEntry e{};s64 count=0;if(R_FAILED(fsDirRead(&d,&count,1,&e))||count!=1)break;if(!memchr(e.name,0,sizeof(e.name)))continue;std::string name=e.name;note("VISUAL_CONTROL_FILE title=%s name=%s size=%lld\n",idstr(id).c_str(),name.c_str(),(long long)e.file_size);if(name.rfind("icon_",0)==0&&name.size()>9&&name.substr(name.size()-4)==".dat"&&name.find_first_of("/\\:")==std::string::npos){v.jpeg=fsRead(fs,("/"+name).c_str(),0x200000);if(!v.jpeg.empty())break;}}fsDirClose(&d);}}
  fsFsClose(&fs);break;
 }return v;
}
void cacheVisual(const GameVisual&v,const std::string&dir){try{if(!v.name.empty())write(dir+"/game-name.txt",v.name.data(),v.name.size());if(!v.jpeg.empty())write(dir+"/game-icon.jpg",v.jpeg.data(),v.jpeg.size());}catch(...){note("VISUAL_CACHE_FAILED %s\n",dir.c_str());}}
GameVisual gameVisual(uint64_t title,const std::vector<std::string>&snapshots){
 GameVisual v;std::string cache="sdmc:/game-vault-cache/"+idstr(title);mkdir("sdmc:/game-vault-cache",0777);mkdir(cache.c_str(),0777);
 auto merge=[&](const GameVisual& candidate,const char* source){
  if(v.name.empty()&&!candidate.name.empty())v.name=candidate.name;
  if(v.jpeg.empty()&&!candidate.jpeg.empty()){
   auto decoded=decodeIcon(candidate.jpeg);
   if(!decoded.rgba.empty()){v.jpeg=candidate.jpeg;note("VISUAL_ICON_OK title=%s source=%s bytes=%zu dimensions=%dx%d\n",idstr(title).c_str(),source,v.jpeg.size(),decoded.w,decoded.h);}
   else note("VISUAL_ICON_REJECTED title=%s source=%s bytes=%zu header=%s\n",idstr(title).c_str(),source,candidate.jpeg.size(),hex(candidate.jpeg.data(),std::min<size_t>(candidate.jpeg.size(),8)).c_str());
  }
 };
 auto paths=snapshots;paths.insert(paths.begin(),cache);
 for(const auto&dir:paths){GameVisual cached;try{auto b=read(dir+"/game-name.txt",512);if(std::find(b.begin(),b.end(),0)==b.end())cached.name=std::string(b.begin(),b.end());}catch(...){}try{cached.jpeg=read(dir+"/game-icon.jpg",0x200000);}catch(...){}merge(cached,"cache");if(!v.name.empty()&&!v.jpeg.empty())return v;}
 auto data=std::make_unique<NsApplicationControlData>();u64 actual=0;Result rc=nsGetApplicationControlData(NsApplicationControlSource_Storage,title,data.get(),sizeof(*data),&actual);
 note("VISUAL_CONTROL title=%s result=%08x bytes=%llu\n",idstr(title).c_str(),rc,(unsigned long long)actual);
 if(R_SUCCEEDED(rc)&&actual>=sizeof(NacpStruct)&&actual<=sizeof(*data)){GameVisual candidate;candidate.name=decodeName(&data->nacp);size_t off=offsetof(NsApplicationControlData,icon);if(actual>off)candidate.jpeg.assign(data->icon,data->icon+actual-off);merge(candidate,"NS");}
 if((v.name.empty()||v.jpeg.empty())&&R_SUCCEEDED(ncmInitialize())){
  for(auto loc:{NcmStorageId_GameCard,NcmStorageId_SdCard,NcmStorageId_BuiltInUser}){
   for(u64 id:{title+0x800,title}){merge(control(id,loc),id==title?"base control":"update control");if(!v.name.empty()&&!v.jpeg.empty())break;}
   if(!v.name.empty()&&!v.jpeg.empty())break;
  }ncmExit();
 }
 if(v.jpeg.empty())note("VISUAL_ICON_UNAVAILABLE title=%s name=%s\n",idstr(title).c_str(),v.name.c_str());
 cacheVisual(v,cache);return v;
}

static std::string userLabel(const FsSaveDataAttribute&a,unsigned index){if(a.save_data_type==FsSaveDataType_Device)return "Device save";std::string name="User "+std::to_string(index+1);if(R_SUCCEEDED(accountInitialize(AccountServiceType_Administrator))){AccountProfile profile{};if(R_SUCCEEDED(accountGetProfile(&profile,a.uid))){AccountProfileBase base{};if(R_SUCCEEDED(accountProfileGet(&profile,nullptr,&base))){size_t n=strnlen(base.nickname,sizeof(base.nickname));if(n&&n<sizeof(base.nickname))name=std::string(base.nickname,n);}accountProfileClose(&profile);}accountExit();}return name;}
std::vector<SaveChoice> saveChoices(uint64_t title,const std::string&snapshot){
 std::vector<SaveChoice> out;
 if(snapshot.empty()){FsSaveDataInfoReader reader{};if(R_FAILED(fsOpenSaveDataInfoReader(&reader,FsSaveDataSpaceId_User)))return out;for(unsigned p=0;p<4096;p++){FsSaveDataInfo rows[16]{};s64 n=0;if(R_FAILED(fsSaveDataInfoReaderRead(&reader,rows,16,&n))||n<0||n>16)break;for(int i=0;i<n;i++){auto&r=rows[i];if(r.application_id!=title||r.save_data_rank!=FsSaveDataRank_Primary||(r.save_data_type!=FsSaveDataType_Account&&r.save_data_type!=FsSaveDataType_Device))continue;FsSaveDataAttribute a{};a.application_id=r.application_id;a.uid=r.uid;a.system_save_data_id=r.system_save_data_id;a.save_data_type=r.save_data_type;a.save_data_rank=r.save_data_rank;a.save_data_index=r.save_data_index;out.push_back({r.save_data_id,"",userLabel(a,out.size())+" ["+idstr(r.save_data_id).substr(8)+"]",a});}if(n<16)break;}fsSaveDataInfoReaderClose(&reader);
 }else{DIR*d=opendir(snapshot.c_str());if(!d)return out;std::vector<std::string>names;while(auto*e=readdir(d)){std::string n=e->d_name;if(n.size()==21&&n.rfind("save-",0)==0&&n.find_first_not_of("0123456789abcdef",5)==std::string::npos)names.push_back(n);}closedir(d);std::sort(names.begin(),names.end());for(auto&n:names)try{auto b=read(snapshot+"/"+n+"/save-attribute.bin",sizeof(FsSaveDataAttribute));auto seal=read(snapshot+"/"+n+"/seal.bin",32);if(b.size()!=sizeof(FsSaveDataAttribute)||seal.size()!=32)continue;auto manifest=read(snapshot+"/"+n+"/manifest.txt",4*1024*1024);Sha256Context digest;sha256ContextCreate(&digest);sha256ContextUpdate(&digest,b.data(),b.size());sha256ContextUpdate(&digest,manifest.data(),manifest.size());u8 actual[32];sha256ContextGetHash(&digest,actual);if(memcmp(actual,seal.data(),32))continue;FsSaveDataAttribute a{};memcpy(&a,b.data(),b.size());if(a.application_id==title)out.push_back({strtoull(n.c_str()+5,nullptr,16),n,userLabel(a,out.size())+" ["+n.substr(n.size()-8)+"]",a});}catch(...){}}
 return out;
}
