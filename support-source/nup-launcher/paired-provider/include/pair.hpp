#pragma once
#include "../../console/source/sd_storage.hpp"
#include <memory>
#include <sstream>
#include <map>
namespace paired {
using Digest=void(*)(const void*,size_t,unsigned char*);
struct Descriptor{
 char magic[8];uint32_t schema,flags;uint64_t title;uint32_t version,reserved;
 unsigned char content[16],inventory[32],files[4][32];
};
static_assert(sizeof(Descriptor)==208);
struct Info{uint64_t title;uint32_t version,flags;unsigned char identity[32];uint64_t exefsSize,romfsSize;};
static_assert(sizeof(Info)==64);
struct Bind{uint32_t route,reserved;unsigned char identity[32];};
static_assert(sizeof(Bind)==40);
inline void need(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
inline std::vector<unsigned char> bytes(const std::string&p,size_t max){
 FILE*f=fopen(p.c_str(),"rb");need(f,"pair file missing");
 std::unique_ptr<FILE,decltype(&fclose)> file(f,fclose);std::vector<unsigned char>b;unsigned char block[4096];size_t n;
 while((n=fread(block,1,sizeof(block),f))){need(b.size()+n<=max,"pair file bounds");b.insert(b.end(),block,block+n);}need(!ferror(f),"pair read failed");return b;
}
class Pair{
 Descriptor descriptor{};Info info{};
 std::unique_ptr<nup::Storage> code,rom;
 std::map<uint64_t,unsigned> bindings;
public:
 Pair(const std::string&root,Digest digest,nup::HashCheck verify){
  auto b=bytes(root+"/pair.bin",sizeof(Descriptor));need(b.size()==sizeof(Descriptor),"pair descriptor length");memcpy(&descriptor,b.data(),b.size());
  need(!memcmp(descriptor.magic,"KNUPPAIR",8)&&descriptor.schema==1&&(descriptor.flags==1||descriptor.flags==2)&&descriptor.title&&!descriptor.reserved,"pair descriptor format");
  digest(b.data(),b.size(),info.identity);info.title=descriptor.title;info.version=descriptor.version;info.flags=descriptor.flags;
  auto inv=bytes(root+"/files.sha256",64*1024*1024);need(verify(inv.data(),inv.size(),descriptor.inventory),"pair inventory hash");
  // The bounded test fixture is fully verified. Larger production inventories
  // need a streaming verifier before they may be enabled in this prototype.
  need(inv.size()<=1024*1024,"production-size pair not enabled");
  std::istringstream input(std::string(inv.begin(),inv.end()));std::string line;std::map<std::string,bool> seen;
  while(std::getline(input,line)){
   std::istringstream fields(line);std::string hash,pathHex,extra;uint64_t size;
   need(bool(fields>>hash>>size>>pathHex)&&!(fields>>extra)&&hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos&&pathHex.size()%2==0,"pair inventory row");
   auto digit=[](char c)->unsigned{need((c>='0'&&c<='9')||(c>='a'&&c<='f'),"invalid inventory hex");return c<='9'?c-'0':c-'a'+10;};
   std::string path;for(size_t i=0;i<pathHex.size();i+=2)path+=char(digit(pathHex[i])*16+digit(pathHex[i+1]));
   need(nup::safePath(path)&&(path.rfind("exefs/",0)==0||path.rfind("romfs/",0)==0)&&!seen.count(path),"pair inventory path");seen[path]=true;
   need(size<=8*1024*1024,"production payload not enabled");auto payload=bytes(root+"/"+path,8*1024*1024);need(payload.size()==size,"pair payload size");unsigned char expected[32];
   for(unsigned i=0;i<32;i++)expected[i]=digit(hash[i*2])*16+digit(hash[i*2+1]);
   need(verify(payload.data(),payload.size(),expected),"pair payload hash");
  }
  need(seen.count("exefs/main")&&seen.count("exefs/main.npdm")&&seen.count("exefs/rtld"),"required executable files absent");
  const char*names[]={"exefs.vfs","romfs.vfs","exefs.meta","romfs.meta"};
  for(unsigned i=0;i<4;i++){auto data=bytes(root+"/.launch/"+names[i],8*1024*1024);need(verify(data.data(),data.size(),descriptor.files[i]),"pair map/metadata mismatch");}
  auto npdm=bytes(root+"/exefs/main.npdm",0x20000);need(npdm.size()>=0x80&&!memcmp(npdm.data(),"META",4),"NPDM header");
  auto off=nup::u32(npdm.data()+0x70),size=nup::u32(npdm.data()+0x74);need(off<=npdm.size()&&size<=npdm.size()-off&&size>=24&&!memcmp(npdm.data()+off,"ACI0",4)&&nup::u64(npdm.data()+off+16)==descriptor.title,"NPDM title mismatch");
  code=std::make_unique<nup::Storage>(root,".launch/exefs.vfs",verify);rom=std::make_unique<nup::Storage>(root,".launch/romfs.vfs",verify);
  unsigned char h[80];code->read(0,h,16);need(!memcmp(h,"PFS0",4),"ExeFS storage header");rom->read(0,h,80);need(nup::u64(h)==80,"RomFS storage header");
  info.exefsSize=code->size();info.romfsSize=rom->size();
 }
 const Info&describe()const{return info;}
 bool bind(uint64_t session,const Bind&b){
  if(!session||b.reserved||(b.route!=1&&b.route!=2)||memcmp(b.identity,info.identity,32))return false;
  auto it=bindings.find(session);if(it!=bindings.end())return it->second==b.route;
  if(bindings.size()>=16)return false;
  bindings[session]=b.route;return true;
 }
 void close(uint64_t session){bindings.erase(session);}
 nup::Storage&route(uint64_t session){auto it=bindings.find(session);need(it!=bindings.end(),"unbound storage session");return it->second==1?*code:*rom;}
};
}
