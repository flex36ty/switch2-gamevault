#pragma once
#include <stdint.h>
// Bounds and overflow checks before RomFS allocates its four metadata tables.
inline bool gameMetadataFits(uint64_t archiveSize,const uint64_t offsets[4],const uint64_t sizes[4],uint64_t used,uint64_t budget,uint64_t* required) {
    if(used>budget)return false;
    uint64_t total=0;
    for(unsigned i=0;i<4;i++) {
        if(offsets[i]>archiveSize || sizes[i]>archiveSize-offsets[i])return false;
        if(sizes[i]>budget-used-total)return false;
        total+=sizes[i];
    }
    *required=total;return true;
}
