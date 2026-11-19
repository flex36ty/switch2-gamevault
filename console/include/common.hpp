#pragma once
#include "progress.h"
#include "companion.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include "format.h"
extern FILE* logging;
void note(const char*,...);
void pump();
void closed(u64 title);
using File=std::unique_ptr<FILE,decltype(&fclose)>;
inline void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
inline void check(Result r,const char* m){if(R_FAILED(r)){note("ERROR %s rc=%08x\n",m,r);throw std::runtime_error(m);}}
inline std::string hex(const u8* p,size_t n){const char*d="0123456789abcdef";std::string s;for(size_t i=0;i<n;i++){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
inline std::string idstr(u64 id){char b[17];snprintf(b,sizeof(b),"%016llx",(unsigned long long)id);return b;}
inline void fresh(const std::string&p){if(mkdir(p.c_str(),0777)){struct stat st{};require(companionScratch(p)&&!lstat(p.c_str(),&st)&&S_ISDIR(st.st_mode),"fresh directory failed");}}
inline void flush(FILE*f){require(!fflush(f)&&!fsync(fileno(f)),"flush failed");}
inline std::vector<u8> read(const std::string&p,size_t limit){File f(fopen(p.c_str(),"rb"),fclose);require(bool(f),"open read failed");std::vector<u8>b;u8 buf[4096];size_t n;while((n=fread(buf,1,sizeof(buf),f.get()))){require(b.size()+n<=limit,"file too large");b.insert(b.end(),buf,buf+n);}require(!ferror(f.get()),"read failed");return b;}
inline void write(const std::string&p,const void*d,size_t n){File f(fopen(p.c_str(),"wb"),fclose);require(bool(f)&&fwrite(d,1,n,f.get())==n,"write failed");flush(f.get());require(!fclose(f.release()),"close failed");auto b=read(p,n);require(b.size()==n&&!memcmp(b.data(),d,n),"readback failed");}
inline std::string hashFile(const std::string&p,u64 expected){File f(fopen(p.c_str(),"rb"),fclose);require(bool(f),"hash open failed");std::vector<u8>b(1024*1024);Sha256Context h;sha256ContextCreate(&h);u64 total=0;vaultTransfer("Verifying backup file",p.c_str(),0,expected);size_t n;while((n=fread(b.data(),1,b.size(),f.get()))){pump();require(total+n>=total&&total+n<=expected,"file too large");total+=n;sha256ContextUpdate(&h,b.data(),n);vaultTransfer("Verifying backup file",p.c_str(),total,expected);}require(!ferror(f.get())&&total==expected,"hash size mismatch");u8 d[32];sha256ContextGetHash(&h,d);return hex(d,32);}
struct MetaDb{NcmContentMetaDatabase db{};~MetaDb(){if(serviceIsActive(&db.s))ncmContentMetaDatabaseClose(&db);}};
struct Storage{NcmContentStorage cs{};~Storage(){if(serviceIsActive(&cs.s))ncmContentStorageClose(&cs);}};
struct Package{NcmContentMetaKey key{};u8 storage=0;};
struct Catalog{char magic[8];u64 title;u32 count,reserved;Package packages[2];u8 hashes[2][32];u8 digest[32];};
void backupGame(u64,const std::string&,unsigned components=3);
void restoreGame(u64,const std::string&,unsigned components=3);
void backupSaves(u64,const std::string&,u64 onlySave=0);
void validateImportedSaves(u64,const std::string&);
void restoreSaves(u64,const std::string&,const std::string&onlyFolder,u64 destinationId,const FsSaveDataAttribute&destination);
