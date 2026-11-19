#pragma once
#include "MtpServer.h"
#include "MtpDatabase.h"
void addInternalGameMounts(android::MtpServer* server, android::MtpDatabase* database);
void closeInternalGameMounts();
