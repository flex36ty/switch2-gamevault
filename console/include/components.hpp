#pragma once
#include <cstdint>
namespace components {
constexpr unsigned Base=1,Update=2,Saves=4;
inline unsigned bit(unsigned type){return type==128?Base:type==129?Update:0;}
inline bool valid(unsigned selected,unsigned available){return selected&&!(selected&~7u)&&!(selected&~available);}
inline bool needsBase(unsigned selected,bool installedBase){return (selected&Update)&&!(selected&Base)&&!installedBase;}
}
