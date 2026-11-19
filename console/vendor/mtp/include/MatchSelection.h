#pragma once
#include <stdint.h>
#include <string.h>
// No SDK dependency: the rollback state machine is exercised by host tests.
typedef struct { int present; unsigned char attributes[16]; char path[0x300]; } MatchLocation;
typedef struct {
 void* context;
 uint32_t (*read)(void*,int,MatchLocation*);
 uint32_t (*write)(void*,int,const MatchLocation*);
 uint32_t (*erase)(void*,int);
 uint32_t (*open)(void*);
 void (*close)(void*);
} MatchOperations;
static inline int match_equal(const MatchLocation* a,const MatchLocation* b){
 return a->present==b->present && (!a->present || (!memcmp(a->attributes,b->attributes,16) && !strcmp(a->path,b->path)));
}
static inline uint32_t match_base_transaction(MatchOperations* op,const MatchLocation* selected,uint32_t mismatch,uint32_t* restore_result){
 MatchLocation previous[3]={{0}},current={0};int changed[3]={0};uint32_t result=0;int opened=0;
 *restore_result=0;
 for(int i=0;i<3;i++){result=op->read(op->context,i,&previous[i]);if(result)return result;}
 for(int i=0;i<2;i++){
  if(match_equal(&previous[i],selected))continue;
  changed[i]=1;result=op->write(op->context,i,selected);if(result)goto cleanup;
 }
 if(previous[2].present){changed[2]=1;result=op->erase(op->context,2);if(result)goto cleanup;}
 for(int i=0;i<3;i++){
  memset(&current,0,sizeof(current));result=op->read(op->context,i,&current);if(result)goto cleanup;
  if((i<2 && !match_equal(&current,selected)) || (i==2 && current.present)){result=mismatch;goto cleanup;}
 }
 result=op->open(op->context);opened=!result;
cleanup:
 // Restore normal before debug, then registered override, even after a failed write.
 for(int i=0;i<3;i++)if(changed[i]){
  uint32_t rc=op->erase(op->context,i);
  if(!rc){memset(&current,0,sizeof(current));rc=op->read(op->context,i,&current);}
  if(!rc && !match_equal(&current,&previous[i])){
   if(previous[i].present)rc=op->write(op->context,i,&previous[i]);else rc=mismatch;
  }
  if(!rc){memset(&current,0,sizeof(current));rc=op->read(op->context,i,&current);if(!rc&&!match_equal(&current,&previous[i]))rc=mismatch;}
  if(rc && !*restore_result)*restore_result=rc;
 }
 if(*restore_result){if(opened)op->close(op->context);return *restore_result;}
 return result;
}
