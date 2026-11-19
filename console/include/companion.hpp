#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
struct CompanionItem {std::string id,name,path;unsigned components;};
struct CompanionCallbacks {
 std::function<std::string()> library;
 std::function<std::string()> profiles;
 std::function<std::vector<CompanionItem>()> snapshots;
 std::function<void(uint64_t,unsigned,uint64_t,const std::string&)> backup;
 std::function<void(const std::string&,unsigned,const std::string&,uint64_t)> restore;
 std::function<void(const std::string&,const std::string&)> display;
};
std::string companionJson(const std::string&);
void companionMode(const CompanionCallbacks&);
bool companionStreaming();
bool companionScratch(const std::string&);
void companionFileBegin(const std::string&,uint64_t);
void companionFileWrite(const void*,size_t);
void companionFileEnd(const std::string&);
void companionValidateImport(const std::string&);
