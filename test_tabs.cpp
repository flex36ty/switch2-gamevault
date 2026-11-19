#include "console/include/ui.hpp"
#include "console/include/tab_filter.hpp"
#include <cassert>
#include "console/include/ftp_path.hpp"
#include "console/include/connection_settings.hpp"
#include "console/vendor/mtp/include/usb_chunk.h"
int main(){
 // Regression: the log showed a 32-byte MTP command split into receives of
 // 32 and 480 bytes. A full-size first receive preserves the short packet.
 for(size_t tail=1;tail<512;tail++){
  (void)tail;size_t requested=usbBounceChunk(512);size_t returned=32;
  assert(requested==512&&returned<requested);
 }
 assert(usbBounceChunk(8192)==4096);
 std::vector<Tile> games(4);
 games[0].id="installed";games[0].installed=true;
 games[1].id="backup-only";games[1].backups=1;
 games[2].id="both";games[2].installed=true;games[2].backups=2;
 games[3].id="0000000000000000";
 assert((tabItems(games,0)==std::vector<size_t>{0,2}));
 assert((tabItems(games,1)==std::vector<size_t>{1,2}));
 assert((tabItems(games,2)==std::vector<size_t>{0,1,2}));
 assert((tabItems(games,3)==std::vector<size_t>{0,2}));
 games[2].cartridge=true;
 assert((tabItems(games,0)==std::vector<size_t>{2,0}));
 assert((tabItems(games,1)==std::vector<size_t>{1,2}));
 games[2].cartridge=false;
 games[1].backups=games[2].backups=0;
 assert(tabItems(games,1).empty());
 games.clear();assert(tabItems(games,0).empty());
 ConnectionSettings settings;settings.enabled[1]=false;settings.automatic=2;
 auto loaded=ConnectionSettings::decode(settings.encode());assert(!loaded.enabled[1]&&loaded.automatic==2);
 assert(ConnectionSettings::decode("GVCONN1\n1 0 1 1\n").automatic==-1);
 assert(ConnectionSettings::decode("bad").automatic==-1);
 settings.automatic=3;settings.enabled[3]=true;
 assert(ConnectionSettings::decode(settings.encode()).automatic==3);
 assert(ConnectionSettings::decode("GVCONN1\n1 1 1 2\n").enabled[3]);
 assert(ftpPath("/gamevault","../switch")=="/switch");
 for(auto bad:{"/../../etc","/sdmc:/escape","/bad\\path"}){bool rejected=false;try{ftpPath("/",bad);}catch(...){rejected=true;}assert(rejected);}
}
