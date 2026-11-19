#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include "connection_settings.hpp"
struct Icon {int w=0,h=0;std::vector<unsigned char> rgba;};
Icon decodeIcon(const std::vector<unsigned char>&);
struct Tile {std::string title,id;Icon icon;bool installed=false;unsigned backups=0;bool cartridge=false;};
struct Screen {
 bool networkConnected=false,networkWired=false;unsigned networkStrength=0;std::string storageLabel="SD card";
 ConnectionSettings connections;int setting=0;bool transferMode=false;
 std::string popupTitle="Choose a backup",popupSubtitle="Date and included components";
 bool choosingSnapshot=false;size_t snapshotChoice=0;std::vector<std::string> snapshotChoices;
 int tab=0,focus=-1;size_t game=0;std::vector<Tile> games;
 unsigned available=0,selected=0;std::string snapshot="No complete backup",user="No matching save",message;
 std::string destination="Choose destination (A)";
 std::string phase="Preparing",file;uint64_t done=0,total=0;bool passed=false;
 bool busy=false,confirm=false,finished=false;std::vector<std::string> details;
};
class Painter {
 struct Glyph{int w,h,x,y,advance;std::vector<unsigned char>pixels;};
 std::map<uint64_t,Glyph> glyphs;
public:
 std::vector<uint32_t> pixels;
 Painter();
 void rect(int,int,int,int,uint32_t,int radius=0);
 void text(int,int,const std::string&,int,uint32_t,int maxwidth=1200);
 void icon(int,int,int,const Icon&,const std::string&);
 void draw(const Screen&);
};
