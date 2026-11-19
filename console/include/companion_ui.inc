   if(down&HidNpadButton_ZL){
    CompanionCallbacks cb;
    cb.snapshots=[&](){std::vector<CompanionItem>items;for(auto id:scan())for(auto&s:snapshots(id))items.push_back({s.path.substr(s.path.size()-33),s.label,s.path,s.mask});return items;};
    cb.library=[&](){std::string out="{\"version\":1,\"games\":[";bool first=true;for(u64 id:scan()){
     auto saved=snapshots(id);std::vector<std::string>paths;for(auto&s:saved)paths.push_back(s.path);auto v=gameVisual(id,paths);
     if(!first)out+=",";first=false;out+="{\"id\":"+companionJson(idstr(id))+",\"name\":"+companionJson(v.name.empty()?idstr(id):v.name)+",\"icon\":"+companionJson(hex(v.jpeg.data(),v.jpeg.size()))+",\"installed\":"+std::to_string(installed[id])+",\"saves\":[";
     bool one=true;for(auto&u:saveChoices(id)){if(!one)out+=",";one=false;out+="{\"id\":"+companionJson(idstr(u.id))+",\"name\":"+companionJson(u.label)+"}";}out+="],\"snapshots\":[";one=true;
     for(auto&s:saved){if(!one)out+=",";one=false;out+="{\"id\":"+companionJson(s.path.substr(s.path.size()-33))+",\"label\":"+companionJson(s.label)+",\"components\":"+std::to_string(s.mask)+",\"saves\":[";bool two=true;for(auto&u:s.users){if(!two)out+=",";two=false;out+="{\"folder\":"+companionJson(u.folder)+",\"name\":"+companionJson(u.label)+"}";}out+="]}";}out+="]}";
    }return out+"]}";};
    cb.display=[&](const std::string&title,const std::string&message){screen.busy=true;screen.phase=title;screen.file=message;screen.message=message;screen.done=screen.total=0;draw(true);};
    cb.backup=[&](u64 id,unsigned selected,u64 save,const std::string&dir){
     scan();auto users=saveChoices(id);require(components::valid(selected,installed[id]|(users.empty()?0:4)),"Components not available");if(selected&4)require(save&&std::any_of(users.begin(),users.end(),[&](const auto&u){return u.id==save;}),"Choose an existing source save");closed(id);fresh(dir);
     auto v=gameVisual(id,{});cacheVisual(v,dir);std::string date="PC export "+idstr(armGetSystemTick());struct stat stamp{};if(stat((dir+"/created.txt").c_str(),&stamp))write(dir+"/created.txt",date.data(),date.size());
     if(selected&3)backupGame(id,dir,selected&3);if(selected&4)backupSaves(id,dir,save);
    };
    cb.restore=[&](const std::string&dir,unsigned selected,const std::string&folder,u64 destinationSave){
     u64 id=strtoull(dir.substr(dir.size()-33,16).c_str(),nullptr,16);SaveChoice source,target;
     if(selected&4){auto users=saveChoices(id,dir);auto dest=saveChoices(id);auto a=std::find_if(users.begin(),users.end(),[&](const auto&u){return u.folder==folder;});auto b=std::find_if(dest.begin(),dest.end(),[&](const auto&u){return u.id==destinationSave;});require(a!=users.end()&&b!=dest.end()&&compatibleSaveDestination(a->attr,b->attr),"Choose compatible source and destination save profiles");source=*a;target=*b;}
     screen.busy=false;screen.details={"PC requested restore",idstr(id),kinds(selected),"Source: "+dir};if(selected&4){screen.details.push_back("Source save: "+source.label);screen.details.push_back("Destination save: "+target.label);}screen.message="+ confirms restore. B cancels. Existing save gets a recovery copy.";require(review(),"Restore cancelled on console");screen.busy=true;closed(id);
     if(selected&3)restoreGame(id,dir,selected&3);if(selected&4)restoreSaves(id,dir,folder,target.id,target.attr);
     note("COMPANION_RESTORE_VERIFIED title=%s components=%u\n",idstr(id).c_str(),selected);
    };
    try{companionMode(cb);}catch(const std::exception&e){note("COMPANION_EXIT %s\n",e.what());}
    screen.busy=false;screen.confirm=false;screen.message="PC Companion closed. Reopen Game Vault to refresh all games.";refresh=true;continue;
   }
