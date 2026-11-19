#pragma once
#include <cstdint>
#include <vector>
#include <string>
std::vector<uint64_t> cartridgeTitles();
void backupCartridge(uint64_t title,const std::string& directory);
