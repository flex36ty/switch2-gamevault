// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstring>
// Caller owns this non-executable buffer exclusively; no atomicity is implied.
template<class Flush> bool stagingCompare(void* dst, const void* expected,
 const void* replacement, size_t size, Flush flush) {
 if(!dst || !expected || !replacement || !size || size>256) return false;
 if(std::memcmp(dst,expected,size)) return false;
 std::memcpy(dst,replacement,size); flush(dst,size);
 return std::memcmp(dst,replacement,size)==0;
}
template<class Flush> unsigned stagingSelfTest(Flush flush) {
 unsigned mask=0; unsigned char data[256],expected[256],replacement[256];
 std::memset(data,0x35,sizeof(data));std::memset(expected,0x35,sizeof(expected));
 std::memset(replacement,0xa7,sizeof(replacement));
 expected[0]=0;
 if(!stagingCompare(data,expected,replacement,sizeof(data),flush) && data[0]==0x35 && data[255]==0x35)mask|=1;
 expected[0]=0x35;
 if(stagingCompare(data,expected,replacement,sizeof(data),flush) && !std::memcmp(data,replacement,sizeof(data)))mask|=2;
 if(stagingCompare(data,replacement,expected,sizeof(data),flush) && !std::memcmp(data,expected,sizeof(data)))mask|=4;
 if(!stagingCompare(data,expected,replacement,0,flush) && !stagingCompare(data,expected,replacement,257,flush) && !std::memcmp(data,expected,sizeof(data)))mask|=8;
 return mask;
}
