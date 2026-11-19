#include "profiles.hpp"
#include "visual.hpp"
#include <map>

std::vector<FsSaveDataInfo> profileSaveInventory(){
 FsSaveDataInfoReader reader{};check(fsOpenSaveDataInfoReader(&reader,FsSaveDataSpaceId_User),"Profile save enumeration");
 struct Close {FsSaveDataInfoReader* r;~Close(){fsSaveDataInfoReaderClose(r);}}close{&reader};
 std::vector<FsSaveDataInfo> result;
 for(unsigned page=0;page<4096;page++){
  pump();FsSaveDataInfo rows[16]{};s64 n=0;check(fsSaveDataInfoReaderRead(&reader,rows,16,&n),"Profile save inventory read");require(n>=0&&n<=16,"Profile save count");
  for(s64 i=0;i<n;i++)if(rows[i].application_id&&rows[i].save_data_space_id==FsSaveDataSpaceId_User&&rows[i].save_data_rank==FsSaveDataRank_Primary&&rows[i].save_data_type==FsSaveDataType_Account&&!rows[i].system_save_data_id)result.push_back(rows[i]);
  if(n<16)return result;
 }throw std::runtime_error("Profile save inventory incomplete");
}

std::string profileInventoryJson(){
 auto saves=profileSaveInventory();check(accountInitialize(AccountServiceType_Administrator),"Profile service");
 struct Exit{~Exit(){accountExit();}}exit;
 AccountUid ids[ACC_USER_LIST_SIZE]{};s32 count=0;check(accountListAllUsers(ids,ACC_USER_LIST_SIZE,&count),"List profiles");require(count>=0&&count<=ACC_USER_LIST_SIZE,"Profile count");
 std::map<u64,std::string> names;std::string out="{\"version\":1,\"scope\":\"local-profile-and-account-saves\",\"profiles\":[";
 for(s32 i=0;i<count;i++){
  pump();AccountProfile profile{};check(accountGetProfile(&profile,ids[i]),"Read profile");struct Close{AccountProfile*p;~Close(){accountProfileClose(p);}}close{&profile};
  AccountProfileBase base{};check(accountProfileGet(&profile,nullptr,&base),"Read profile nickname");require(!memcmp(&base.uid,&ids[i],sizeof(AccountUid)),"Profile identity mismatch");
  u32 size=0;check(accountProfileGetImageSize(&profile,&size),"Profile icon size");require(size<=0x20000,"Profile icon bounds");std::vector<u8> icon(size);u32 actual=0;
  if(size){check(accountProfileLoadImage(&profile,icon.data(),icon.size(),&actual),"Read profile icon");require(actual==size,"Profile icon changed");}
  if(i)out+=",";out+="{\"uid\":"+companionJson(hex((const u8*)&base.uid,sizeof(base.uid)))+",\"name\":"+companionJson(std::string(base.nickname,strnlen(base.nickname,sizeof(base.nickname))))+",\"icon\":"+companionJson(hex(icon.data(),icon.size()))+",\"saves\":[";
  bool first=true;for(const auto&s:saves)if(!memcmp(&s.uid,&ids[i],sizeof(AccountUid))){
   if(!names.count(s.application_id)){auto visual=gameVisual(s.application_id,{});names[s.application_id]=visual.name.empty()?idstr(s.application_id):visual.name;}
   if(!first)out+=",";first=false;out+="{\"title\":"+companionJson(idstr(s.application_id))+",\"id\":"+companionJson(idstr(s.save_data_id))+",\"index\":"+std::to_string(s.save_data_index)+",\"name\":"+companionJson(names[s.application_id])+"}";
  }out+="]}";
 }out+="]}";note("PROFILE_INVENTORY_READ_ONLY users=%d account_saves=%u credentials=excluded\n",count,unsigned(saves.size()));return out;
}
