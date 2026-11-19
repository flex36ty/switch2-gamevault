#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
namespace plan {
enum Op:uint32_t {Log,Delay,NcmOpen,NcmCount,SdProbe,CelesteLicense,CelesteRomfs,Close};
struct Instruction {Op op{};uint32_t value=0;char text[80]{};};
struct Program {uint32_t count=0;Instruction instructions[32]{};};
// Parse into a temporary. Never modify the live plan on malformed input.
inline bool parse(const char* bytes,size_t size,Program& out){
 if(!bytes||!size||size>4096||std::memchr(bytes,0,size))return false;
 Program candidate{};size_t off=0;bool header=false,ended=false;uint32_t delay=0;
 while(off<size){size_t start=off;while(off<size&&bytes[off]!='\n')off++;size_t length=off-start;if(off<size)off++;
  if(length&&bytes[start+length-1]=='\r')length--;
  if(length>100)return false;
  char line[101]{};std::memcpy(line,bytes+start,length);
  for(size_t i=0;i<length;i++)if((unsigned char)line[i]<32||(unsigned char)line[i]>126)return false;
  if(!length||line[0]=='#')continue;
  if(!header){if(std::strcmp(line,"KAIHEN_PLAN 1"))return false;header=true;continue;}
  if(ended)return false;
  if(!std::strcmp(line,"END")){ended=true;continue;}
  if(candidate.count==32)return false;
  Instruction instruction{};
  if(!std::strncmp(line,"LOG ",4)){if(length-4>=sizeof(instruction.text))return false;instruction.op=Log;std::strcpy(instruction.text,line+4);}
  else if(!std::strncmp(line,"DELAY ",6)){
   const char* number=line+6;if(!*number||std::strlen(number)>5||std::strspn(number,"0123456789")!=std::strlen(number))return false;
   unsigned long n=std::strtoul(number,nullptr,10);if(n>60000)return false;
   delay+=(uint32_t)n;if(delay>120000)return false;instruction.op=Delay;instruction.value=(uint32_t)n;
  }else if(!std::strcmp(line,"NCM_OPEN"))instruction.op=NcmOpen;
  else if(!std::strcmp(line,"NCM_COUNT"))instruction.op=NcmCount;
  else if(!std::strcmp(line,"SD_PROBE"))instruction.op=SdProbe;
  else if(!std::strcmp(line,"CELESTE_LICENSE"))instruction.op=CelesteLicense;
  else if(!std::strcmp(line,"CELESTE_ROMFS"))instruction.op=CelesteRomfs;
  else if(!std::strcmp(line,"CLOSE"))instruction.op=Close;
  else return false;
  candidate.instructions[candidate.count++]=instruction;
 }
 if(!header||!ended||!candidate.count)return false;
 out=candidate;return true;
}
struct Store {
 Program active{},previous{};bool loaded=false,previous_valid=false;uint32_t generation=0;
 bool reload(const char* bytes,size_t size,bool running){if(running)return false;Program next{};if(!parse(bytes,size,next))return false;previous=active;previous_valid=loaded;active=next;loaded=true;generation++;return true;}
 bool rollback(bool running){if(running||!previous_valid)return false;Program temp=active;active=previous;previous=temp;generation++;return true;}
};
}
