#pragma once
#include <cstdint>
struct ProviderInfo{uint64_t pid,boot,heartbeat,reads;uint32_t version,ready,result,reserved;};
static_assert(sizeof(ProviderInfo)==48);
