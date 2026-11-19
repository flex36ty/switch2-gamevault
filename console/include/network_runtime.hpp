#pragma once
#include <mutex>
#include <string>
#include <stdexcept>
void vaultNetworkAcquire();
void vaultNetworkRelease();
std::recursive_mutex& vaultStorageMutex();
struct VaultStorageLease {
 std::unique_lock<std::recursive_mutex> lock;
 VaultStorageLease():lock(vaultStorageMutex(),std::try_to_lock){if(!lock.owns_lock())throw std::runtime_error("A file transfer is active. Try again after it completes.");}
};
void vaultFtpStart();
void vaultFtpStop();
std::string vaultFtpStatus();
bool vaultFtpRunning();
