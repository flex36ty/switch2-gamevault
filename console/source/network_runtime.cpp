#ifdef GAMEVAULT_NETWORK_HOST_TEST
#include "../../tests/ftp_host.hpp"
using Result=int;
#define R_FAILED(rc) ((rc)!=0)
#else
#include "common.hpp"
#endif
#include "network_runtime.hpp"
#include "connections.hpp"
#include <thread>
#include <atomic>
namespace {
std::mutex networkMutex,statusMutex;std::recursive_mutex storageMutex;
unsigned networkUsers=0;std::thread ftpThread;std::atomic<bool> stopFtp{false},running{false};std::string status="FTP stopped";
}
std::recursive_mutex& vaultStorageMutex(){return storageMutex;}
void vaultNetworkAcquire(){std::lock_guard<std::mutex> hold(networkMutex);if(!networkUsers){Result rc=socketInitializeDefault();if(R_FAILED(rc))throw std::runtime_error("Network socket initialization failed");}++networkUsers;}
void vaultNetworkRelease(){std::lock_guard<std::mutex> hold(networkMutex);if(networkUsers&&!--networkUsers)socketExit();}
std::string vaultFtpStatus(){std::lock_guard<std::mutex> hold(statusMutex);return status;}
bool vaultFtpRunning(){return running;}
void vaultFtpStop(){stopFtp=true;if(ftpThread.joinable())ftpThread.join();running=false;}
void vaultFtpStart(){
 if(running)return;if(ftpThread.joinable())ftpThread.join();stopFtp=false;running=true;
 try{ftpThread=std::thread([]{
  try{vaultFtp([](const std::string&a,const std::string&b){std::lock_guard<std::mutex> hold(statusMutex);status=a+"\n"+b;},[]{if(stopFtp)throw std::runtime_error("FTP stopped");});}
  catch(const std::exception&e){std::lock_guard<std::mutex> hold(statusMutex);status=e.what();}
  running=false;
 });}catch(...){running=false;throw;}
}
