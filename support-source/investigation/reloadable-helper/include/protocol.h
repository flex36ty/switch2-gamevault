#pragma once
#include <stdint.h>
enum {HelperStatus=0,HelperReload=1,HelperRun=2,HelperCancel=3,HelperRollback=4};
typedef struct {uint32_t version,state,generation,step,count,last_result,control_result,loaded;uint64_t pid,heartbeat;} HelperInfo;
