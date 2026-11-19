#pragma once
#include <string>
#include <vector>
#include <cstdint>
struct DumpFile {std::string path,name;uint64_t size;};
struct EmulatorDump {uint64_t title=0;uint32_t version=0;std::string metadata;std::vector<DumpFile> files;};
const EmulatorDump& prepareEmulatorDump(uint64_t title);
const EmulatorDump& currentEmulatorDump();
void closeEmulatorDump();
std::string dumpEmulatorToSd(uint64_t title);
