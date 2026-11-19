#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "plugin_ipc.h"
int main(void){
 consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
 Service ro={0},hen={0},plugin={0};Result rc=workbenchWireSelfTest();
 if(R_SUCCEEDED(rc))rc=kaihenGetRoSession(&ro);
 if(R_SUCCEEDED(rc))rc=kaihenOpenHomebrewSession(&ro,&hen);
 HenPluginName name={0};strcpy(name.name,"backup-workbench");
 if(R_SUCCEEDED(rc))rc=henOpenPlugin(&hen,&name,&plugin);
 Result last=rc;u64 next=0;HelperInfo info={0};
 while(appletMainLoop()){
  padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_B)break;
  if(R_SUCCEEDED(rc)){
   unsigned cmd=0;if(down&HidNpadButton_X)cmd=HelperReload;else if(down&HidNpadButton_A)cmd=HelperRun;else if(down&HidNpadButton_Y)cmd=HelperCancel;else if(down&HidNpadButton_L)cmd=HelperRollback;
   if(cmd)last=workbenchDispatch(&plugin,cmd,NULL);
   if(armGetSystemTick()>=next){Result status=workbenchDispatch(&plugin,HelperStatus,&info);if(R_FAILED(status)){rc=status;last=status;}next=armGetSystemTick()+armNsToTicks(250000000);}
  }
  printf("\x1b[2J\x1b[HBackup Workbench controller v1.1 (diagnostic, no game launch)\n\nX reload active.plan | A run | Y cancel | L rollback | B exit\n\nConnection: %08x  last request: %08x\nPID: %llu heartbeat: %llu\nState: %u (0 idle, 1 pending, 2 running)\nPlan loaded: %u generation: %u step: %u/%u\nTest result: %08x  reload/control result: %08x\n\nPlans: /kaihen/reload-helper/active.plan\nLog: /kaihen/reload-helper/run.log\n\nB closes only this controller. Background test continues.\n",rc,last,(unsigned long long)info.pid,(unsigned long long)info.heartbeat,info.state,info.loaded,info.generation,info.step,info.count,info.last_result,info.control_result);
  consoleUpdate(NULL);svcSleepThread(50000000);
 }
 if(serviceIsActive(&plugin))serviceClose(&plugin);if(serviceIsActive(&hen))serviceClose(&hen);
 consoleExit(NULL);return 0;
}

