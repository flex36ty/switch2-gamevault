#pragma once
#include "MtpServer.h"
#include "MtpDatabase.h"
void prepareCelesteBackup();
void addCelesteBackup(android::MtpServer*,android::MtpDatabase*);
void closeCelesteBackup();

u64 selectedBackupTitle();
