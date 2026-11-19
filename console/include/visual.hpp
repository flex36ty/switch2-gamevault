#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <cstdint>
struct GameVisual {std::string name;std::vector<unsigned char> jpeg;};
GameVisual gameVisual(uint64_t,const std::vector<std::string>&);
void cacheVisual(const GameVisual&,const std::string&);
struct SaveChoice{uint64_t id=0;std::string folder,label;FsSaveDataAttribute attr{};};
std::vector<SaveChoice> saveChoices(uint64_t,const std::string&snapshot="");
