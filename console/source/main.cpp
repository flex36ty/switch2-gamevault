#include "common.hpp"
#include "ui.hpp"
#include "tab_filter.hpp"
#include "connections.hpp"
#include "network_runtime.hpp"
#include "emulator_dump.hpp"
#include "web_mode.hpp"
#include "remote_storage.hpp"
#include "cartridge.hpp"
#include <set>
#include "visual.hpp"
#include "components.hpp"
#include "save_mapping.hpp"
#include "backup_delete.hpp"
#include "profiles.hpp"
#include <cstdarg>
#include <dirent.h>
#include <chrono>
#include <ctime>
#include <map>
FILE* logging=nullptr;
static PadState pad;static Framebuffer fb{};static bool display=false;
static Painter painter;static Screen screen;static RemoteSettings remoteSettings;static bool networkStatusReady=false;
static void updateNetwork(){static auto last=std::chrono::steady_clock::time_point{};auto now=std::chrono::steady_clock::now();if(now-last<std::chrono::seconds(2))return;last=now;screen.networkConnected=false;if(!networkStatusReady)return;u32 ip=0;screen.networkConnected=R_SUCCEEDED(nifmGetCurrentIpAddress(&ip))&&ip!=0;NifmInternetConnectionType type{};NifmInternetConnectionStatus state{};u32 strength=0;if(R_SUCCEEDED(nifmGetInternetConnectionStatus(&type,&strength,&state))){screen.networkWired=type==NifmInternetConnectionType_Ethernet;screen.networkStrength=std::min(strength,3u);}}
static std::chrono::steady_clock::time_point lastFrame;
static void draw(bool force=false){if(!display)return;auto now=std::chrono::steady_clock::now();if(!force&&now-lastFrame<std::chrono::milliseconds(200))return;updateNetwork();painter.draw(screen);u32 stride=0;auto*buffer=(u8*)framebufferBegin(&fb,&stride);if(buffer){for(unsigned y=0;y<720;y++)memcpy(buffer+y*stride,painter.pixels.data()+y*1280,1280*4);framebufferEnd(&fb);}lastFrame=now;}
extern "C" void vaultTransfer(const char* phase,const char* file,uint64_t done,uint64_t total){screen.transferMode=false;screen.phase=phase;screen.file=file;screen.done=std::min(done,total);screen.total=total;draw();}
extern "C" void vaultProgress(const char*){draw();}
void note(const char*fmt,...){char line[2048];va_list a;va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);if(logging){fputs(line,logging);fflush(logging);}vaultProgress(line);}
void pump(){padUpdate(&pad);require(appletMainLoop()&&!(padGetButtonsDown(&pad)&HidNpadButton_B),"Cancelled. Backups and recovery records were retained.");draw();}
extern "C" bool vaultAlive(void){try{pump();return true;}catch(...){return false;}}
void closed(u64 title){check(pmdmntInitialize(),"PM init");u64 pid=0;Result r=pmdmntGetProcessId(&pid,title);pmdmntExit();require(r==MAKERESULT(15,1),"Close this game and use a different homebrew host.");}
struct Snapshot{std::string path,label;unsigned mask=0;std::vector<SaveChoice>users;};
static bool validName(const std::string&n){return backupSnapshotName(n);}

static std::string kinds(unsigned m){std::string s;if(m&1)s="Base game";if(m&2)s+=(s.empty()?"":" + ")+std::string("Update");if(m&4)s+=(s.empty()?"":" + ")+std::string("Saves");return s;}
static std::vector<Snapshot> snapshots(u64 title){
 std::vector<std::string>paths;for(const auto*root:backupRoots){DIR*d=opendir(root);if(!d)continue;while(auto*e=readdir(d)){std::string n=e->d_name;if(validName(n)&&n.substr(0,16)==idstr(title))paths.push_back(std::string(root)+"/"+n);}closedir(d);}std::sort(paths.rbegin(),paths.rend());std::vector<Snapshot>out;
 for(const auto&path:paths){Snapshot s;s.path=path;
  try{auto bytes=read(path+"/catalog.bin",sizeof(Catalog));if(bytes.size()==sizeof(Catalog)){Catalog c{};memcpy(&c,bytes.data(),sizeof(c));u8 hash[32];sha256CalculateHash(hash,&c,offsetof(Catalog,digest));if(!memcmp(c.magic,"GBCAT001",8)&&c.title==title&&c.count>0&&c.count<=2&&!c.reserved&&!memcmp(hash,c.digest,32)){bool good=true;unsigned mask=0;for(unsigned i=0;i<c.count;i++){const auto&p=c.packages[i];unsigned bit=components::bit(p.key.type);if(!bit||mask&bit||p.key.id!=title+(bit==2?0x800:0))good=false;mask|=bit;auto plan=read(path+"/p"+std::to_string(i)+"/package.bin",sizeof(InstallPlan));if(plan.size()!=sizeof(InstallPlan))good=false;else{sha256CalculateHash(hash,plan.data(),plan.size()-32);if(memcmp(hash,c.hashes[i],32)||memcmp(hash,plan.data()+plan.size()-32,32))good=false;}}if(good)s.mask=mask;}}}catch(...){}
  s.users=saveChoices(title,path);if(!s.users.empty())s.mask|=4;if(!s.mask)continue;
  std::string date;try{auto b=read(path+"/created.txt",80);date.assign(b.begin(),b.end());}catch(...){}if(date.empty())date=path.substr(path.size()-16)=="0000000000000001"?"Imported backup":"Earlier backup "+std::to_string(out.size()+1);
  s.label=date+" | "+kinds(s.mask);out.push_back(std::move(s));
 }return out;
}
static bool chooseSnapshot(const std::vector<Snapshot>& list,size_t& selected){
 if(list.empty())return false;
 screen.popupTitle="Choose a backup";screen.popupSubtitle="Date and included components";
 screen.snapshotChoices.clear();for(const auto& item:list)screen.snapshotChoices.push_back(item.label);
 screen.snapshotChoice=std::min(selected,list.size()-1);screen.choosingSnapshot=true;bool accepted=false;
 while(appletMainLoop()){
  draw(true);padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
  if(down&HidNpadButton_B)break;
  if(down&HidNpadButton_A){selected=screen.snapshotChoice;accepted=true;break;}
  if(down&HidNpadButton_Up)screen.snapshotChoice=(screen.snapshotChoice+list.size()-1)%list.size();
  if(down&HidNpadButton_Down)screen.snapshotChoice=(screen.snapshotChoice+1)%list.size();
  svcSleepThread(16000000);
 }
 screen.choosingSnapshot=false;screen.snapshotChoices.clear();return accepted;
}
static const char* connectionNames[]={"PC Companion","FTP server","USB MTP","Web interface"};
static int chooseConnection(){
 std::vector<int> modes;screen.snapshotChoices.clear();
 for(int i=0;i<4;i++)if(screen.connections.enabled[i]){modes.push_back(i);screen.snapshotChoices.push_back(connectionNames[i]);}
 if(modes.empty()){screen.message="All connections are disabled. Enable one in Settings.";return -1;}
 screen.popupTitle="Connections";screen.popupSubtitle="FTP stays on while Game Vault is open. B returns to the menu.";
 screen.snapshotChoice=0;screen.choosingSnapshot=true;int result=-1;
 while(appletMainLoop()){draw(true);padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_B)break;if(down&HidNpadButton_A){result=modes[screen.snapshotChoice];break;}
  if(down&HidNpadButton_Up)screen.snapshotChoice=(screen.snapshotChoice+modes.size()-1)%modes.size();
  if(down&HidNpadButton_Down)screen.snapshotChoice=(screen.snapshotChoice+1)%modes.size();svcSleepThread(16000000);
 }
 screen.choosingSnapshot=false;screen.snapshotChoices.clear();return result;
}
static int chooseList(const std::string&title,const std::vector<std::string>&items){
 if(items.empty())return -1;screen.popupTitle=title;screen.popupSubtitle="A selects. B returns.";screen.snapshotChoices=items;screen.snapshotChoice=0;screen.choosingSnapshot=true;int result=-1;
 while(appletMainLoop()){draw(true);padUpdate(&pad);auto b=padGetButtonsDown(&pad);if(b&HidNpadButton_B)break;if(b&HidNpadButton_A){result=screen.snapshotChoice;break;}if(b&HidNpadButton_Up)screen.snapshotChoice=(screen.snapshotChoice+items.size()-1)%items.size();if(b&HidNpadButton_Down)screen.snapshotChoice=(screen.snapshotChoice+1)%items.size();svcSleepThread(16000000);}screen.choosingSnapshot=false;screen.snapshotChoices.clear();return result;
}
static std::string inputText(const char*heading,const std::string&initial,bool password=false){SwkbdConfig keyboard{};check(swkbdCreate(&keyboard,0),"Open keyboard");struct End{SwkbdConfig*k;~End(){swkbdClose(k);}}end{&keyboard};if(password)swkbdConfigMakePresetPassword(&keyboard);else swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetHeaderText(&keyboard,heading);swkbdConfigSetInitialText(&keyboard,initial.c_str());swkbdConfigSetStringLenMax(&keyboard,255);char out[1024]{};require(R_SUCCEEDED(swkbdShow(&keyboard,out,sizeof(out))),"Storage configuration cancelled");return out;}
static void configureTarget(RemoteTarget&target){auto next=target;next.host=inputText("Server hostname or IPv4",next.host);if(next.smb)next.share=inputText("SMB share name",next.share);next.path=inputText("Folder within server/share (relative)",next.path);auto port=inputText("Server port",std::to_string(next.port));require(!port.empty()&&port.size()<=5&&port.find_first_not_of("0123456789")==std::string::npos,"Invalid port");next.port=std::stoul(port);next.user=inputText("Username",next.user);next.password=inputText("Password (saved on this SD card)",next.password,true);if(next.smb)next.domain=inputText("SMB domain (blank if unused)",next.domain);validateRemoteTarget(next);target=next;}
static const RemoteTarget& selectedTarget(){require(remoteSettings.selected>0,"Select FTP or SMB storage");return remoteSettings.selected==1?remoteSettings.ftp:remoteSettings.smb;}
static void updateStorageLabel(){screen.storageLabel=remoteSettings.selected==0?"SD card":remoteSettings.selected==1?"FTP: "+remoteSettings.ftp.host:"SMB: "+remoteSettings.smb.host;}
static bool storageMenu(bool restore){
 bool imported=false;try{int choice=chooseList("Storage destinations",{"Use SD card","Use FTP server","Use SMB share","Configure FTP server","Configure SMB share","Test selected server","Download a remote backup for restore"});if(choice<0)return false;
 if(choice<3){if(choice)require((choice==1?remoteSettings.ftp:remoteSettings.smb).configured(),"Configure this server first");remoteSettings.selected=choice;}
 else if(choice==3)configureTarget(remoteSettings.ftp);else if(choice==4)configureTarget(remoteSettings.smb);
 else {screen.busy=true;screen.phase="Connecting to storage";screen.file.clear();screen.done=screen.total=0;draw(true);if(choice==5){testRemoteTarget(selectedTarget());screen.message="Remote folder is readable. Upload permissions are checked when backing up.";}else{auto list=listRemoteBackups(selectedTarget());screen.busy=false;require(!list.empty(),"No complete Game Vault snapshots on this server");int i=chooseList("Remote backups: title ID / snapshot",list);if(i>=0){screen.busy=true;auto path=downloadRemoteBackup(selectedTarget(),list[i]);screen.message="Verified download ready in Restore: "+path;screen.tab=1;imported=true;}}}
 saveRemoteSettings(remoteSettings);updateStorageLabel();if(choice<3)screen.message=restore&&choice?"Selected server. Choose Download a remote backup to import a verified snapshot.":"Storage selected: "+screen.storageLabel;
 }catch(const std::exception&e){screen.message=e.what();note("REMOTE_STORAGE_STOP %s\n",e.what());}screen.busy=false;return imported;
}
static void saveConnectionSettings(){
 auto text=screen.connections.encode();write("sdmc:/gamevault/connections.tmp",text.data(),text.size());
 VaultStorageLease access;
 const char*cfg="sdmc:/gamevault/connections.cfg",*previous="sdmc:/gamevault/connections.previous";struct stat st{};bool existed=!stat(cfg,&st);
 if(existed){if(!stat(previous,&st))require(!unlink(previous),"Remove previous settings snapshot");require(!rename(cfg,previous),"Preserve previous settings");}
 if(rename("sdmc:/gamevault/connections.tmp",cfg)){if(existed)rename(previous,cfg);throw std::runtime_error("Save connection settings failed; previous settings retained");}
}
static std::map<u64,unsigned> installed;static std::set<u64> insertedCards;
static void backupSelected(u64 title,const std::string&dir,unsigned selected){if((selected&1)&&insertedCards.count(title)){require(!companionStreaming(),"Cartridge backup uses SD or configured network storage");backupCartridge(title,dir);selected&=~1u;}if(selected)backupGame(title,dir,selected);}

static std::vector<u64> scan(){
 std::vector<u64>ids;installed.clear();insertedCards.clear();bool ended=false;
 for(const auto&s:profileSaveInventory())ids.push_back(s.application_id);
 for(int off=0;off<8192;off+=32){NsApplicationRecord rows[32]{};s32 n=0;check(nsListApplicationRecord(rows,32,off,&n),"Game library");require(n>=0&&n<=32,"Game library count");for(int i=0;i<n;i++)if(rows[i].application_id&&!(rows[i].application_id&0xfff)){u64 id=rows[i].application_id;NsApplicationContentMetaStatus st[16]{};s32 count=0;unsigned mask=0;if(R_SUCCEEDED(nsListApplicationContentMetaStatus(id,0,st,16,&count))&&count>=0&&count<=16)for(int j=0;j<count;j++)mask|=components::bit(st[j].meta_type);installed[id]=mask;ids.push_back(id);}if(n<32){ended=true;break;}}require(ended,"Game library scan incomplete");
 for(const auto*root:backupRoots){DIR*d=opendir(root);if(d){while(auto*e=readdir(d)){std::string n=e->d_name;if(validName(n))ids.push_back(strtoull(n.substr(0,16).c_str(),nullptr,16));}closedir(d);}}try{for(auto id:cartridgeTitles()){insertedCards.insert(id);installed[id]|=1;ids.push_back(id);}}catch(const std::exception&e){note("CARTRIDGE_SCAN_FAILED %s\n",e.what());}std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());return ids;
}
static bool review(){screen.confirm=true;draw(true);while(appletMainLoop()){padUpdate(&pad);u64 b=padGetButtonsDown(&pad);if(b&HidNpadButton_B){screen.confirm=false;return false;}if(b&HidNpadButton_Plus){screen.confirm=false;return true;}svcSleepThread(16000000);}screen.confirm=false;return false;}
static void deleteBackup(const std::string&root,const std::string&title){
 require(deletableBackupPath(root,title),"Refused backup path outside selected snapshot");
 // Inventory first. Reject links, special files and traversal before deleting anything.
 std::vector<std::string>dirs={root},files;uint64_t bytes=0;
 for(size_t at=0;at<dirs.size();at++){
  pump();struct stat st{};require(!lstat(dirs[at].c_str(),&st)&&S_ISDIR(st.st_mode),"Backup directory changed or is a link");
  DIR*d=opendir(dirs[at].c_str());require(d,"Cannot inspect backup directory");
  try{while(auto*e=readdir(d)){std::string name=e->d_name;if(name=="."||name=="..")continue;require(backupChildName(name),"Unsafe backup filename");std::string path=dirs[at]+"/"+name;require(path.size()<FS_MAX_PATH&&dirs.size()+files.size()<200000,"Backup deletion bounds exceeded");require(!lstat(path.c_str(),&st),"Cannot inspect backup entry");if(S_ISDIR(st.st_mode))dirs.push_back(path);else{require(S_ISREG(st.st_mode)&&st.st_size>=0,"Backup contains link or special file");require(UINT64_MAX-bytes>=(uint64_t)st.st_size,"Backup size overflow");bytes+=st.st_size;files.push_back(path);}}closedir(d);}catch(...){closedir(d);throw;}
 }
 uint64_t done=0;vaultTransfer("Deleting selected backup",root.c_str(),0,bytes);
 for(const auto&path:files){pump();struct stat st{};require(!lstat(path.c_str(),&st)&&S_ISREG(st.st_mode),"Backup file changed; deletion stopped");require(!unlink(path.c_str()),"Cannot remove backup file");done+=st.st_size;vaultTransfer("Deleting selected backup",path.c_str(),done,bytes);}
 for(auto it=dirs.rbegin();it!=dirs.rend();++it){pump();require(!rmdir(it->c_str()),"Backup directory is not empty; deletion stopped");}
 note("BACKUP_DELETED path=%s\n",root.c_str());
}

int main(){
 padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);mkdir("sdmc:/gamevault",0777);mkdir("sdmc:/gamevault/backups",0777);logging=fopen("sdmc:/game-vault.log","a");
 try{require(logging,"Cannot open log");check(framebufferCreate(&fb,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2),"Display creation");check(framebufferMakeLinear(&fb),"Display linear mode");display=true;screen.message="Loading your library...";draw(true);check(nsInitialize(),"NS init");note("GAME_VAULT_VERSION=0.4.3-cartridge-icons\n");
  try{auto b=read("sdmc:/gamevault/connections.cfg",256);screen.connections=ConnectionSettings::decode(std::string(b.begin(),b.end()));}catch(...){}
  remoteSettings=loadRemoteSettings();updateStorageLabel();networkStatusReady=R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
  int autoMode=screen.connections.automatic;
  if(screen.connections.enabled[1])vaultFtpStart();
  std::vector<std::vector<Snapshot>> copies;std::vector<GameVisual>visuals;std::vector<std::vector<SaveChoice>>liveUsers;
  auto reloadLibrary=[&](){auto ids=scan();screen.games.clear();copies.clear();visuals.clear();liveUsers.clear();screen.game=0;
  for(u64 id:ids){auto list=snapshots(id);if(!installed.count(id)&&list.empty())continue;std::vector<std::string>paths;for(const auto&s:list)paths.push_back(s.path);auto v=gameVisual(id,paths);Tile tile;tile.id=idstr(id);tile.title=v.name.empty()?"Game "+tile.id:v.name;tile.icon=decodeIcon(v.jpeg);tile.installed=installed.count(id)&&installed.at(id)!=0;tile.backups=list.size();tile.cartridge=insertedCards.count(id);screen.games.push_back(std::move(tile));copies.push_back(std::move(list));visuals.push_back(std::move(v));liveUsers.push_back(saveChoices(id));screen.message="Loading game names and icons...";draw();}
  if(screen.tab==0)for(size_t i=0;i<screen.games.size();++i)if(screen.games[i].cartridge){screen.game=i;break;}
  if(screen.games.empty()){Tile empty;empty.id="0000000000000000";empty.title="No games yet - ZL opens PC Companion";screen.games.push_back(empty);copies.push_back({});visuals.push_back({});liveUsers.push_back({});}
  };reloadLibrary();
  require(!screen.games.empty(),"No installed games or complete backups found");size_t snapshot=0,user=0;int destination=-1;bool refresh=true,resetSelection=true;screen.message="Choose a game, then select individual components.";
  while(appletMainLoop()){
   auto visible=tabItems(screen.games,screen.tab);
   if(!visible.empty()&&std::find(visible.begin(),visible.end(),screen.game)==visible.end()){screen.game=visible.front();snapshot=user=0;destination=-1;refresh=resetSelection=true;}
   size_t g=screen.game;u64 title=strtoull(screen.games[g].id.c_str(),nullptr,16);auto&list=copies[g];if(snapshot>=list.size())snapshot=0;bool restore=screen.tab==1;auto&users=restore&&!list.empty()?list[snapshot].users:liveUsers[g];if(user>=users.size())user=0;
   std::vector<SaveChoice> destinations;if(restore&&!users.empty())for(const auto&candidate:liveUsers[g])if(compatibleSaveDestination(users[user].attr,candidate.attr))destinations.push_back(candidate);
   if(destination>=int(destinations.size()))destination=-1;
   if(refresh){screen.destination=destination<0?(destinations.empty()?"Start game once in target profile":"Choose destination (A)"):destinations[destination].label;screen.available=restore?(list.empty()?0:list[snapshot].mask):(installed[title]|(liveUsers[g].empty()?0:4));screen.snapshot=list.empty()?"No complete backup":list[snapshot].label;screen.user=users.empty()?"No matching save":users[user].label;if(resetSelection){screen.selected=screen.available&1?1:screen.available&2?2:screen.available&4;resetSelection=false;}screen.selected&=screen.available;draw(true);refresh=false;}
   draw();padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(autoMode>=0)down|=HidNpadButton_ZL;if(down&HidNpadButton_B){if(screen.focus>=0){screen.focus=-1;refresh=true;}else break;}
   if(down&HidNpadButton_Plus){reloadLibrary();snapshot=user=0;destination=-1;refresh=resetSelection=true;continue;}
   if(down&HidNpadButton_Minus){if(storageMenu(restore)){reloadLibrary();snapshot=user=0;destination=-1;}refresh=resetSelection=true;continue;}
   if(down&HidNpadButton_ZL){
    int mode=autoMode>=0?autoMode:chooseConnection();autoMode=-1;
    if(mode<0){refresh=true;continue;}
    if(mode==1||mode==2){
     screen.busy=true;screen.transferMode=true;
     auto status=[&](const std::string& title,const std::string& info){screen.phase=title;screen.file=info;draw(true);};
     
     try{if(mode==1){vaultFtpStart();for(;;){status("Persistent FTP",vaultFtpStatus());pump();svcSleepThread(100000000);}}else {VaultStorageLease access;vaultMtp(status);}screen.message="Connection stopped.";}
     catch(const std::exception&e){screen.message=e.what();note("CONNECTION_STOP mode=%d error=%s\n",mode,e.what());}
     if(!logging)logging=fopen("sdmc:/game-vault.log","a");
     screen.busy=false;screen.transferMode=false;reloadLibrary();snapshot=user=0;destination=-1;refresh=resetSelection=true;continue;
    }

    CompanionCallbacks cb;
    cb.profiles=[](){return profileInventoryJson();};
    cb.snapshots=[&](){std::vector<CompanionItem>items;for(auto id:scan())for(auto&s:snapshots(id))items.push_back({s.path.substr(s.path.size()-33),s.label,s.path,s.mask});return items;};
    cb.library=[&](){std::string out="{\"version\":1,\"games\":[";bool first=true;for(u64 id:scan()){
     auto saved=snapshots(id);std::vector<std::string>paths;for(auto&s:saved)paths.push_back(s.path);auto v=gameVisual(id,paths);
     if(!first)out+=",";first=false;out+="{\"id\":"+companionJson(idstr(id))+",\"name\":"+companionJson(v.name.empty()?idstr(id):v.name)+",\"icon\":"+companionJson(hex(v.jpeg.data(),v.jpeg.size()))+",\"installed\":"+std::to_string(installed[id])+",\"saves\":[";
     bool one=true;for(auto&u:saveChoices(id)){if(!one)out+=",";one=false;out+="{\"id\":"+companionJson(idstr(u.id))+",\"name\":"+companionJson(u.label)+"}";}out+="],\"snapshots\":[";one=true;
     for(auto&s:saved){if(!one)out+=",";one=false;out+="{\"id\":"+companionJson(s.path.substr(s.path.size()-33))+",\"label\":"+companionJson(s.label)+",\"components\":"+std::to_string(s.mask)+",\"saves\":[";bool two=true;for(auto&u:s.users){if(!two)out+=",";two=false;out+="{\"folder\":"+companionJson(u.folder)+",\"name\":"+companionJson(u.label)+"}";}out+="]}";}out+="]}";
    }return out+"]}";};
    cb.display=[&](const std::string&title,const std::string&message){screen.busy=true;screen.transferMode=mode==3;screen.phase=title;screen.file=message;screen.message=message;screen.done=screen.total=0;draw(true);};
    cb.backup=[&](u64 id,unsigned selected,u64 save,const std::string&dir){
     VaultStorageLease access;
     scan();auto users=saveChoices(id);require(components::valid(selected,installed[id]|(users.empty()?0:4)),"Components not available");if(selected&4)require(save&&std::any_of(users.begin(),users.end(),[&](const auto&u){return u.id==save;}),"Choose an existing source save");closed(id);fresh(dir);
     auto v=gameVisual(id,{});cacheVisual(v,dir);std::string date="PC export "+idstr(armGetSystemTick());struct stat stamp{};if(stat((dir+"/created.txt").c_str(),&stamp))write(dir+"/created.txt",date.data(),date.size());
     if(selected&3)backupSelected(id,dir,selected&3);if(selected&4)backupSaves(id,dir,save);
    };
    cb.restore=[&](const std::string&dir,unsigned selected,const std::string&folder,u64 destinationSave){
     VaultStorageLease access;
     u64 id=strtoull(dir.substr(dir.size()-33,16).c_str(),nullptr,16);SaveChoice source,target;
     if(selected&4){auto users=saveChoices(id,dir);auto dest=saveChoices(id);auto a=std::find_if(users.begin(),users.end(),[&](const auto&u){return u.folder==folder;});auto b=std::find_if(dest.begin(),dest.end(),[&](const auto&u){return u.id==destinationSave;});require(a!=users.end()&&b!=dest.end()&&compatibleSaveDestination(a->attr,b->attr),"Choose compatible source and destination save profiles");source=*a;target=*b;}
     screen.busy=false;screen.transferMode=false;screen.details={"PC requested restore",idstr(id),kinds(selected),"Source: "+dir};if(selected&4){screen.details.push_back("Source save: "+source.label);screen.details.push_back("Destination save: "+target.label);}screen.message="+ confirms restore. B cancels. Existing save gets a recovery copy.";require(review(),"Restore cancelled on console");screen.busy=true;closed(id);
     if(selected&3)restoreGame(id,dir,selected&3);if(selected&4)restoreSaves(id,dir,folder,target.id,target.attr);
     note("COMPANION_RESTORE_VERIFIED title=%s components=%u\n",idstr(id).c_str(),selected);
    };
    try{if(mode==3)vaultWeb(cb);else companionMode(cb);}catch(const std::exception&e){note("COMPANION_EXIT %s\n",e.what());}
    screen.busy=false;screen.transferMode=false;screen.confirm=false;screen.message="Connection closed. Library refreshed.";reloadLibrary();snapshot=user=0;destination=-1;refresh=resetSelection=true;continue;
   }

   if(down&(HidNpadButton_L|HidNpadButton_R)){screen.tab=(screen.tab+(down&HidNpadButton_R?1:4))%5;screen.focus=-1;snapshot=user=0;destination=-1;refresh=resetSelection=true;continue;}
   if(screen.tab==4){
    if(down&HidNpadButton_Up)screen.setting=(screen.setting+6)%7;
    if(down&HidNpadButton_Down)screen.setting=(screen.setting+1)%7;
    if(down&HidNpadButton_A){
     if(screen.setting<4){int i=screen.setting;screen.connections.enabled[i]=!screen.connections.enabled[i];if(!screen.connections.enabled[i]&&screen.connections.automatic==i)screen.connections.automatic=-1;}
     else if(screen.setting==4){int next=screen.connections.automatic;do{next++;if(next>3)next=-1;}while(next>=0&&!screen.connections.enabled[next]);screen.connections.automatic=next;}
     else if(screen.setting==5){int choice=chooseConnection();if(choice>=0)autoMode=choice;}else{if(storageMenu(false)){reloadLibrary();snapshot=user=0;destination=-1;}}
     try{if(screen.connections.enabled[1])vaultFtpStart();else vaultFtpStop();saveConnectionSettings();screen.message="Settings saved.";}catch(const std::exception&e){screen.message=e.what();}
    }
    draw();svcSleepThread(16000000);continue;
   }
   if(visible.empty()){screen.focus=-1;screen.available=screen.selected=0;draw();svcSleepThread(16000000);continue;}
   if(screen.tab==2){if(down&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right)){int step=down&HidNpadButton_Right?1:down&HidNpadButton_Left?-1:down&HidNpadButton_Down?3:-3;screen.game=(screen.game+screen.games.size()+step%int(screen.games.size()))%screen.games.size();refresh=resetSelection=true;}if(down&HidNpadButton_A){screen.tab=1;screen.focus=0;snapshot=user=0;destination=-1;refresh=resetSelection=true;}svcSleepThread(16000000);continue;}
   if(screen.tab==3){
    if(down&(HidNpadButton_Up|HidNpadButton_Down)){size_t pos=std::find(visible.begin(),visible.end(),screen.game)-visible.begin();screen.game=visible[(pos+(down&HidNpadButton_Down?1:visible.size()-1))%visible.size()];refresh=true;}
    if(down&HidNpadButton_X){if(screen.connections.enabled[3])autoMode=3;else screen.message="Enable Web interface in Settings.";refresh=true;}
    if(down&HidNpadButton_A){
     screen.details={"Dump matched base ExeFS + RomFS to SD",screen.games[g].title,"Includes executable files and SHA-256 checksums.","Installed updates are excluded.","Destination: /gamevault/emulator-dumps"};screen.message="Close the selected game; use another homebrew host.";
     if(review()){screen.busy=true;screen.passed=false;screen.phase="Preparing matched base";screen.file.clear();screen.done=screen.total=0;draw(true);
      try{auto path=dumpEmulatorToSd(title);if(remoteSettings.selected)uploadRemoteBackup(selectedTarget(),path,true);screen.passed=true;screen.details={"Emulator dump verified",path,"ExeFS and RomFS use the same original base program.","SHA256SUMS.txt and export.json included."};screen.message="Transfer this folder to your PC for packaging.";}
      catch(const std::exception&e){screen.details={"Emulator dump stopped",e.what(),"Incomplete files are retained with an INCOMPLETE marker."};screen.message="Keep the log for review.";note("EMULATOR_SD_STOP %s\n",e.what());}
      closeEmulatorDump();screen.busy=false;screen.finished=true;draw(true);while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&(HidNpadButton_A|HidNpadButton_B))break;svcSleepThread(16000000);}screen.finished=false;
     }refresh=true;
    }svcSleepThread(16000000);continue;
   }
   if(screen.focus<0){if(down&(HidNpadButton_Up|HidNpadButton_Down)){size_t pos=std::find(visible.begin(),visible.end(),screen.game)-visible.begin();screen.game=visible[(pos+(down&HidNpadButton_Down?1:visible.size()-1))%visible.size()];snapshot=user=0;destination=-1;refresh=resetSelection=true;continue;}if(down&(HidNpadButton_Right|HidNpadButton_A)){screen.focus=1;refresh=true;continue;}}
   else{if(down&HidNpadButton_Left){screen.focus=-1;refresh=true;}if(down&HidNpadButton_Up){screen.focus=(screen.focus+(restore?6:5))%(restore?7:6);refresh=true;}if(down&HidNpadButton_Down){screen.focus=(screen.focus+1)%(restore?7:6);refresh=true;}}
   if((down&HidNpadButton_ZR)&&restore&&!list.empty()){
    const auto path=list[snapshot].path;
    screen.details={"Delete the entire selected backup?",list[snapshot].label,path,"Includes its backed-up base, update, saves and recovery records.","Installed software and live saves stay unchanged."};screen.message="Permanent deletion. + Confirm, or B cancel before deleting.";
    if(review()){
     screen.busy=true;screen.phase="Inspecting selected backup";screen.file=path;screen.done=screen.total=0;screen.message="B stops deletion; any remaining files are retained.";screen.passed=false;draw(true);
     try{VaultStorageLease access;deleteBackup(path,screen.games[g].id);screen.passed=true;screen.details={"Selected backup deleted.","Installed software and live saves were not changed."};screen.message="Return to choose another backup.";}
     catch(const std::exception&e){screen.details={"Deletion stopped",e.what(),"Some backup files may already have been removed.",path};screen.message="Remaining files stay at this path. Keep logs for review.";note("BACKUP_DELETE_STOPPED %s path=%s\n",e.what(),path.c_str());}
     screen.busy=false;screen.finished=true;draw(true);while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&(HidNpadButton_A|HidNpadButton_B))break;svcSleepThread(16000000);}screen.finished=false;
     copies[g]=snapshots(title);screen.games[g].backups=copies[g].size();snapshot=user=0;destination=-1;
    }refresh=resetSelection=true;continue;
   }
   if((down&HidNpadButton_Y)&&restore&&!list.empty()){if(chooseSnapshot(list,snapshot)){user=0;destination=-1;resetSelection=true;}refresh=true;continue;}
   if((down&HidNpadButton_X)&&!users.empty()){user=(user+1)%users.size();destination=-1;refresh=true;continue;}
   if((down&HidNpadButton_A)&&screen.focus>=0){
    if(screen.focus==0&&restore&&!list.empty()){if(chooseSnapshot(list,snapshot)){user=0;destination=-1;resetSelection=true;}refresh=true;continue;}
    if(screen.focus>=1&&screen.focus<=3){unsigned bit=1u<<(screen.focus-1);if(screen.available&bit)screen.selected^=bit;refresh=true;continue;}
    if(screen.focus==4&&!users.empty()){user=(user+1)%users.size();destination=-1;refresh=true;continue;}
    if(restore&&screen.focus==5){if(!destinations.empty())destination=(destination+1)%destinations.size();refresh=true;continue;}
    if(screen.focus==(restore?6:5)){
     if(!components::valid(screen.selected,screen.available)){screen.message="Select at least one available component.";refresh=true;continue;}
     unsigned selected=screen.selected;SaveChoice chosen;if(selected&4){if(users.empty()){screen.message="No supported save is available.";refresh=true;continue;}chosen=users[user];}
     SaveChoice target;if(restore&&(selected&4)){if(destination<0){screen.message="Choose a destination profile with an existing save.";refresh=true;continue;}target=destinations[destination];}
     screen.details={std::string(restore?"Restore: ":"Back up: ")+kinds(selected)};
     if(!restore&&(selected&1)&&insertedCards.count(title))screen.details.push_back("Cartridge base archives; digital restore is not supported.");if(selected&4)screen.details.push_back("Source save: "+chosen.label);
     if(restore&&(selected&4))screen.details.push_back("Destination: "+target.label);
     screen.details.push_back(restore?"Unselected components stay unchanged.":"Backup destination: "+screen.storageLabel+" (SD staging retained)");
     if(restore&&(selected&4))screen.details.push_back("Current saves will be backed up before replacement.");
     screen.message=restore?(selected&4?"Replaces ONLY the selected destination save. Test profile recommended.":"Only the selected software components will be restored."):"Checksums will be verified after copying.";
     if(!review()){refresh=true;continue;}
     screen.phase="Checking selected files";screen.file.clear();screen.done=screen.total=0;screen.passed=false;screen.busy=true;screen.message="Each file is copied and verified separately.";draw(true);bool passed=false;
     try{VaultStorageLease access;closed(title);std::string dir;
      if(restore){require(!list.empty(),"No complete backup selected");dir=list[snapshot].path;note("VAULT_RESTORE title=%s components=%u snapshot=%s\n",idstr(title).c_str(),selected,dir.c_str());if(selected&3)restoreGame(title,dir,selected&3);if(selected&4)restoreSaves(title,dir,chosen.folder,target.id,target.attr);}
      else{dir="sdmc:/gamevault/backups/"+idstr(title)+"-"+idstr(armGetSystemTick());fresh(dir);std::time_t now=std::time(nullptr);char date[80]{};auto*t=std::gmtime(&now);if(t){std::strftime(date,sizeof(date),"%Y-%m-%d %H:%M UTC",t);write(dir+"/created.txt",date,strlen(date));}cacheVisual(visuals[g],dir);note("VAULT_BACKUP title=%s components=%u snapshot=%s\n",idstr(title).c_str(),selected,dir.c_str());if(selected&3)backupSelected(title,dir,selected&3);if(selected&4)backupSaves(title,dir,chosen.id);if(remoteSettings.selected)uploadRemoteBackup(selectedTarget(),dir);}
      screen.details={restore?"Selected components restored and verified.":"Selected components backed up and verified.",kinds(selected),"Existing backup and recovery records were retained."};screen.message="Reopen FTP if you want the logs checked.";passed=true;
     }catch(const std::exception&e){screen.details={"Operation stopped",e.what(),"Keep the backup and recovery files."};screen.message="Reopen FTP so the log can be checked.";note("VAULT_STOPPED %s\n",e.what());}
     screen.passed=passed;screen.busy=false;screen.finished=true;draw(true);while(appletMainLoop()){padUpdate(&pad);u64 b=padGetButtonsDown(&pad);if(b&HidNpadButton_A)break;if(b&HidNpadButton_B)goto finish;svcSleepThread(16000000);}screen.finished=false;
     copies[g]=snapshots(title);liveUsers[g]=saveChoices(title);screen.games[g].backups=copies[g].size();if(passed&&restore&&(selected&3)){installed[title]|=selected&3;screen.games[g].installed=true;}snapshot=user=0;destination=-1;refresh=resetSelection=true;
    }
   }svcSleepThread(16000000);
  }
finish:nsExit();
 }catch(const std::exception&e){note("VAULT_FATAL %s\n",e.what());screen.message=e.what();screen.finished=true;screen.details={"Unable to continue",e.what()};draw(true);while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_B)break;svcSleepThread(16000000);}}
 vaultFtpStop();closeEmulatorDump();if(networkStatusReady)nifmExit();
 if(display)framebufferClose(&fb);if(logging)fclose(logging);return 0;
}
