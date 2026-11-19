#include "common.hpp"
#include "connections.hpp"
#include "SwitchMtpDatabase.h"
#include "MtpServer.h"
#include "MtpStorage.h"
#include <thread>
#include <atomic>
extern "C" const char* mtpUsbInitStage;

void vaultMtp(const ConnectionDisplay& display){
 using namespace android;
 display("USB MTP","Initializing USB. Connect your data cable to the PC.");
 usb_device_descriptor descriptor{};
 descriptor.bLength=USB_DT_DEVICE_SIZE;descriptor.bDescriptorType=USB_DT_DEVICE;
 descriptor.bcdUSB=0x0110;descriptor.bMaxPacketSize0=0x40;
 descriptor.idVendor=0x057e;descriptor.idProduct=0x4000;
 descriptor.bcdDevice=0x0100;descriptor.bNumConfigurations=1;
 UsbInterfaceDesc info{};USBMtpInterface usb(0,&info);
 Result rc=usbInitialize(&descriptor,1,&info);
 if(R_FAILED(rc)){note("VAULT_MTP_INIT_FAILED stage=%s result=%08x\n",mtpUsbInitStage,rc);usbExit();check(rc,"USB MTP initialization");}
 struct Cleanup {~Cleanup(){usbExit();}} cleanup;
 SwitchMtpDatabase database;
 MtpStorage storage(0x00010001,"sdmc:/","Game Vault SD card",100U*1024U*1024U,false,0xffffffffULL);
 MtpServer server(&usb,&database,false,0,0666,0777);
 database.addStoragePath("sdmc:/",storage.getDescription(),storage.getStorageID(),true);
 server.addStorage(&storage);
 display("USB MTP","Open This PC > Game Vault SD card. B stops USB and returns.");
 note("VAULT_MTP_STARTED sd_only=1 execution=main_thread\n");
 std::atomic<bool> ended{false};std::exception_ptr failure;
 // Match standalone MTP: all USB/FS requests run on the entry thread.
 // The controller only observes B/app exit and requests stop. It must not
 // draw, use Game Vault's pad, or access its filesystem/logging callbacks.
 std::thread controller([&]{
  PadState controls;padInitializeDefault(&controls);
  while(!ended.load()){
   if(!appletMainLoop()){server.stop();break;}
   padUpdate(&controls);
   if(padGetButtonsDown(&controls)&HidNpadButton_B){server.stop();break;}
   svcSleepThread(16000000);
  }
 });
 try{server.run();}
 catch(const std::exception&e){note("VAULT_MTP_RUN_FAILED error=%s\n",e.what());failure=std::current_exception();}
 catch(...){note("VAULT_MTP_RUN_FAILED unknown_exception=1\n");failure=std::current_exception();}
 ended=true;
 server.stop();controller.join();
 display("Stopping USB MTP","Waiting for pending USB transfers to finish safely...");
 note("VAULT_MTP_STOPPED main_returned=1 controller_joined=1\n");
 if(failure)std::rethrow_exception(failure);
}
