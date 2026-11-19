#pragma once
#include <cstdint>
namespace provider {
constexpr uint64_t MaxRead=1024*1024;
inline bool validRead(int64_t offset,uint64_t count,uint64_t capacity,uint64_t size){
 return offset>=0&&uint64_t(offset)<=size&&count<=size-uint64_t(offset)&&count<=capacity&&count<=MaxRead;
}
}
