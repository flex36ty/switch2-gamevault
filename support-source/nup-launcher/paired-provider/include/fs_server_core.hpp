#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <stdexcept>
#include "../../native/exefs.h"

// Normalized server operations. A future HIPC transport must validate descriptors,
// copy input before nested IPC, and publish real child sessions transactionally.
// These numeric tokens are NOT kernel handles or FS-process object pointers.
namespace providerfs {
constexpr uint32_t Bad=0xd401, Missing=0xd601;
struct Mount {
 knup_storage storage{};knup_exefs index{};uint64_t title=0;
 unsigned char pair[32]{};
 virtual ~Mount()=default;
 static uint32_t enter(void*){return 0;}
 static void leave(void*){}
 uint32_t initialize(uint64_t id,const unsigned char*identity){
  if(!id||!identity)return Bad;title=id;memcpy(pair,identity,32);
  return knup_exefs_index(&index,&storage);
 }
};
class Server {
 struct Object {std::shared_ptr<Mount> mount;bool file=false;knup_object view{};};
 std::map<uint64_t,Object> objects;uint64_t next=1;
 uint32_t insert(Object o,uint64_t*out){
  if(!out)return Bad;*out=0;
  if(objects.size()>=32||!next)return Bad;
  auto id=next++;objects.emplace(id,std::move(o));*out=id;return 0;
 }
public:
 uint32_t connect(std::shared_ptr<Mount> mount,uint64_t title,const unsigned char*pair,uint64_t*out){
  if(out)*out=0;
  if(!mount||!mount->index.count||title!=mount->title||!pair||memcmp(pair,mount->pair,32))return Bad;
  return insert({std::move(mount),false,{}},out);
 }
 // IFileSystem command 8: read-only open, returns a child object.
 uint32_t open(uint64_t root,const char*path,uint32_t mode,uint64_t*out){
  if(out)*out=0;auto it=objects.find(root);
  if(it==objects.end()||it->second.file||mode!=1)return Bad;
  Object child{it->second.mount,true,{}};auto*m=child.mount.get();
  auto rc=knup_exefs_open(&m->index,path,m->title,m,Mount::enter,Mount::leave,&child.view);
  return rc?rc:insert(std::move(child),out);
 }
 // IFileSystem command 7: the supported namespace contains regular files only.
 uint32_t entryType(uint64_t root,const char*path,uint32_t*out){
  if(!out)return Bad;auto it=objects.find(root);if(it==objects.end()||it->second.file)return Bad;
  auto*m=it->second.mount.get();knup_object file{};
  auto rc=knup_exefs_open(&m->index,path,m->title,m,Mount::enter,Mount::leave,&file);
  if(!rc)*out=1;return rc;
 }
 // IFile command 0: {u32 option,u32 padding,s64 offset,u64 count}.
 uint32_t read(uint64_t id,uint32_t option,uint32_t padding,int64_t offset,uint64_t count,void*buffer,uint64_t capacity,uint64_t*out){
  if(out)*out=0;auto it=objects.find(id);
  if(it==objects.end()||!it->second.file||padding||count>INT64_MAX)return Bad;
  knup_buffer b{buffer,capacity};return knup_file_read(&it->second.view,out,offset,&b,int64_t(count),option);
 }
 // IFile command 4.
 uint32_t size(uint64_t id,uint64_t*out){auto it=objects.find(id);if(it==objects.end()||!it->second.file)return Bad;return knup_object_size(&it->second.view,out);}
 bool close(uint64_t id){return objects.erase(id)!=0;}
 size_t live()const{return objects.size();}
};
}
