#pragma once
#include <stdbool.h>
#include <string.h>
enum CycleDecision {CycleRefuse, CycleResume, CycleNew};
typedef int (*CycleRename)(const char*,const char*);
// Called only for a new cycle: the previous journal is complete or absent.
// Preserve the previous pointer before publication; Switch rename cannot replace.
// No console content is mutated until publication and readback succeed.
static inline bool cycle_publish(bool exists,const char*current,const char*previous,
                                 const char*next,CycleRename move){
 if(exists&&move(current,previous))return false;
 return move(next,current)==0;
}
static inline enum CycleDecision cycle_decision(bool exists,unsigned phase){
 if(!exists)return CycleNew;
 if(phase==1)return CycleResume;
 if(phase==2)return CycleNew;
 return CycleRefuse;
}
static inline bool cycle_name_valid(const char*name){
 if(strlen(name)!=24||memcmp(name,"restore-",8))return false;
 for(unsigned i=8;i<24;i++)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return false;
 return true;
}
