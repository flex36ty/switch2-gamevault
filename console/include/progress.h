#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void vaultTransfer(const char* phase,const char* file,uint64_t done,uint64_t total);
#ifdef __cplusplus
}
#endif
