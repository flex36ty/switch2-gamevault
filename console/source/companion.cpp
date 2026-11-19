#include "common.hpp"
#include "network_runtime.hpp"
#include "companion.hpp"
#include "backup_delete.hpp"
#include "companion_paths.hpp"
#include "companion_io.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sstream>
#include <cerrno>
namespace {
int client=-1;std::string streamRoot;uint64_t position=0,skip=0,expected=0;
struct Socket {int fd=-1;~Socket(){if(fd>=0)::close(fd);}};
const char* ioStage="idle";
void transferBytes(void*data,size_t n,bool sending){
 auto*p=(char*)data;
 auto result=vaultio::transfer(n,[&](size_t off,size_t remaining){pump();int k=sending?send(client,p+off,std::min<size_t>(remaining,262144),0):recv(client,p+off,std::min<size_t>(remaining,262144),0);int error=k<0?errno:0;return vaultio::Attempt{k,error};},[]{pump();svcSleepThread(1000000);},[]{return armGetSystemTick();},armNsToTicks(180000000000ULL));
 if(result.status!=vaultio::Status::Complete){
  note("COMPANION_IO_FAILURE stage=%s operation=%s status=%u errno=%d completed=%llu expected=%llu\n",ioStage,sending?"send":"receive",unsigned(result.status),result.error,(unsigned long long)result.done,(unsigned long long)n);
  throw std::runtime_error(std::string("PC socket ")+(sending?"send":"receive")+" failed at "+ioStage+" (status="+std::to_string(unsigned(result.status))+", errno="+std::to_string(result.error)+"); partial files retained");
 }
}
void sendBytes(const void*data,size_t n){transferBytes(const_cast<void*>(data),n,true);}
void receive(void*data,size_t n){transferBytes(data,n,false);}
void line(const std::string&s){sendBytes(s.data(),s.size());sendBytes("\n",1);}
std::string getLine(){std::string s;char c;while(s.size()<4096){receive(&c,1);if(c=='\n')return s;require(c!='\r'&&c!=0,"Bad protocol line");s+=c;}throw std::runtime_error("Protocol line limit");}
std::vector<std::string> fields(const std::string&s){std::vector<std::string>v;std::istringstream in(s);std::string x;while(std::getline(in,x,'\t'))v.push_back(x);return v;}
uint64_t number(const std::string&s,int base=10){require(!s.empty()&&s.size()<=20&&s.find_first_not_of(base==16?"0123456789abcdef":"0123456789")==std::string::npos,"Invalid number");size_t used=0;auto n=std::stoull(s,&used,base);require(used==s.size(),"Invalid number");return n;}
std::string unhex(const std::string&s){require(s.size()%2==0&&s.size()<=2048&&s.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid path encoding");std::string v;for(size_t i=0;i<s.size();i+=2)v+=char(number(s.substr(i,2),16));return v;}
std::string encoded(const std::string&s){return hex((const u8*)s.data(),s.size());}
bool relative(const std::string&s){if(s.empty()||s.size()>800||s.front()=='/'||s.back()=='/'||s.find_first_of("\\:\r\n\t")!=std::string::npos||s.find('\0')!=std::string::npos)return false;std::istringstream in(s);std::string c;while(std::getline(in,c,'/'))if(!backupChildName(c))return false;return true;}
void directories(const std::string&root,const std::string&rel){require(relative(rel),"Unsafe relative path");size_t at=0;while((at=rel.find('/',at))!=std::string::npos){std::string p=root+"/"+rel.substr(0,at++);struct stat st{};if(stat(p.c_str(),&st))require(!mkdir(p.c_str(),0777),"Create staging directory");else require(S_ISDIR(st.st_mode),"Staging parent not directory");}}
void json(const std::string&s){line("JSON\t"+std::to_string(s.size()));sendBytes(s.data(),s.size());}
void sendFile(const std::string&path,const std::string&relativePath){struct stat st{};require(!lstat(path.c_str(),&st)&&S_ISREG(st.st_mode)&&st.st_size>=0,"Not a regular backup file");std::string old=streamRoot;streamRoot=path.substr(0,path.size()-relativePath.size()-1);companionFileBegin(path,st.st_size);File f(fopen(path.c_str(),"rb"),fclose);require(bool(f),"Open transfer file");Sha256Context h;sha256ContextCreate(&h);std::vector<u8>b(262144);size_t n;while((n=fread(b.data(),1,b.size(),f.get()))){sha256ContextUpdate(&h,b.data(),n);companionFileWrite(b.data(),n);}require(!ferror(f.get()),"File read error");u8 hash[32];sha256ContextGetHash(&h,hash);companionFileEnd(hex(hash,32));streamRoot=old;}
void tree(const std::string&root){std::vector<std::string>dirs={""};unsigned count=0;for(size_t i=0;i<dirs.size();i++){DIR*d=opendir((root+dirs[i]).c_str());require(d,"Open snapshot directory");try{while(auto*e=readdir(d)){std::string name=e->d_name;if(name=="."||name=="..")continue;require(backupChildName(name)&&++count<200000,"Snapshot bounds");std::string rel=dirs[i]+"/"+name,path=root+rel;struct stat st{};require(!lstat(path.c_str(),&st),"Snapshot stat");if(S_ISDIR(st.st_mode)){dirs.push_back(rel);line("DIR\t"+encoded(rel.substr(1)));}else{require(S_ISREG(st.st_mode),"Special file rejected");sendFile(path,rel.substr(1));}}closedir(d);}catch(...){closedir(d);throw;}}}
CompanionItem lookup(const CompanionCallbacks&cb,const std::string&id){CompanionItem found{};unsigned matches=0;for(auto&s:cb.snapshots())if(s.id==id){found=s;matches++;}require(matches==1,"Backup unavailable or duplicate snapshot ID");return found;}
void importSnapshot(const std::string&id){require(backupSnapshotName(id),"Invalid snapshot ID");std::string stage="sdmc:/gamevault/incoming/"+id,dest="sdmc:/gamevault/backups/"+id;struct stat st{};require(stat(dest.c_str(),&st)!=0,"Backup already exists on SD; use existing snapshot");mkdir("sdmc:/gamevault/incoming",0777);if(mkdir(stage.c_str(),0777))require(!lstat(stage.c_str(),&st)&&S_ISDIR(st.st_mode),"Invalid staging directory");line("READY");unsigned files=0;uint64_t budget=0;
 for(;;){auto f=fields(getLine());require(!f.empty(),"Missing upload frame");if(f[0]=="END")break;if(f[0]=="DIR"){require(f.size()==2,"Bad directory frame");auto rel=unhex(f[1]);directories(stage,rel+"/x");continue;}require(f.size()==4&&f[0]=="FILE"&&++files<=200000,"Bad file frame");std::string rel=unhex(f[1]),digest=f[3];uint64_t size=number(f[2]);require(relative(rel)&&vaultImportFile(rel)&&size<=(1ULL<<40)&&digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==std::string::npos,"Bad upload metadata");require(budget+size>=budget,"Upload size overflow");budget+=size;directories(stage,rel);std::string file=stage+"/"+rel,part=file+".gvpart";uint64_t off=0;
 if(!lstat(file.c_str(),&st)){require(S_ISREG(st.st_mode),"Invalid staging file");if(uint64_t(st.st_size)==size&&hashFile(file,size)==digest){line("OFFSET\t"+std::to_string(size));line("ACK\t"+digest);continue;}throw std::runtime_error("Staging collision; choose a fresh snapshot ID");}
 if(!lstat(part.c_str(),&st)){require(S_ISREG(st.st_mode)&&st.st_size>=0&&uint64_t(st.st_size)<=size,"Invalid partial");off=st.st_size;}
 FsFileSystem sd{};check(fsOpenSdCardFileSystem(&sd),"SD space");s64 free=0;Result rc=fsFsGetFreeSpace(&sd,"/",&free);fsFsClose(&sd);check(rc,"SD free space");require(free>=0&&uint64_t(free)>=size-off+16*1024*1024,"Not enough SD staging space");line("OFFSET\t"+std::to_string(off));File output(fopen(part.c_str(),off?"ab":"wb"),fclose);require(bool(output),"Staging open");std::vector<u8>b(262144);while(off<size){size_t n=std::min<uint64_t>(b.size(),size-off);receive(b.data(),n);require(fwrite(b.data(),1,n,output.get())==n,"Staging write");off+=n;vaultTransfer("Receiving PC backup",rel.c_str(),off,size);}flush(output.get());require(!fclose(output.release()),"Staging close");require(hashFile(part,size)==digest,"Upload checksum mismatch; partial retained");require(!rename(part.c_str(),file.c_str()),"Publish staged file");line("ACK\t"+digest);
 }
 require(files>0,"Empty upload");
 bool hasGame=!stat((stage+"/catalog.bin").c_str(),&st),hasSave=false;DIR* scan=opendir(stage.c_str());require(scan,"Inspect import");while(auto* e=readdir(scan)){std::string n=e->d_name;if(n.rfind("save-",0)==0)hasSave=true;}closedir(scan);
 require(hasGame||hasSave,"No game or save backup in upload");if(hasGame)companionValidateImport(stage);if(hasSave)validateImportedSaves(number(id.substr(0,16),16),stage);require(!rename(stage.c_str(),dest.c_str()),"Publish snapshot");line("DONE\t"+id);
}
}
std::string companionJson(const std::string&s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){char b[7];snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=c;}return out+'"';}
bool companionStreaming(){return !streamRoot.empty();}
bool companionScratch(const std::string&p){return companionStreaming()&&(p==streamRoot||p.rfind(streamRoot+"/",0)==0);}
void companionFileBegin(const std::string&p,uint64_t size){require(companionScratch(p)&&p.size()>streamRoot.size()+1,"Export path bounds");line("FILE\t"+encoded(p.substr(streamRoot.size()+1))+"\t"+std::to_string(size));auto a=fields(getLine());require(a.size()==2&&a[0]=="OFFSET","Missing resume offset");skip=number(a[1]);require(skip<=size,"Resume exceeds source");position=0;expected=size;}
void companionFileWrite(const void*data,size_t n){require(position+n>=position&&position+n<=expected,"Export length");size_t drop=position<skip?std::min<uint64_t>(n,skip-position):0;sendBytes((const u8*)data+drop,n-drop);position+=n;}
void companionFileEnd(const std::string&hash){require(position==expected,"Short export");line("HASH\t"+hash);require(getLine()=="ACK\t"+hash,"PC readback checksum failed");}
void companionMode(const CompanionCallbacks&cb){vaultNetworkAcquire();struct End{~End(){streamRoot.clear();client=-1;vaultNetworkRelease();}}end;Socket listenSocket;listenSocket.fd=socket(AF_INET,SOCK_STREAM,0);require(listenSocket.fd>=0,"Companion socket");int one=1;setsockopt(listenSocket.fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(1340);addr.sin_addr.s_addr=INADDR_ANY;require(!bind(listenSocket.fd,(sockaddr*)&addr,sizeof(addr))&&!listen(listenSocket.fd,4),"Companion listen");require(fcntl(listenSocket.fd,F_SETFL,O_NONBLOCK)>=0,"Companion listener mode");unsigned random=0;randomGet(&random,sizeof(random));char pin[5];snprintf(pin,sizeof(pin),"%04u",random%10000);u8 secret[32];randomGet(secret,sizeof(secret));std::string token=hex(secret,sizeof(secret));u64 expires=armGetSystemTick()+armNsToTicks(120000000000ULL);unsigned attempts=0;bool paired=false;in_addr ip{};ip.s_addr=gethostid();std::string address=std::string(inet_ntoa(ip))+":1340";
 cb.display("PC Companion: "+address,std::string("Pairing code: ")+pin+"  (2 minutes, 5 attempts). B exits.");note("COMPANION_READY address=%s\n",address.c_str());
 for(;;){pump();sockaddr_storage peer{};socklen_t length=sizeof(peer);Socket sock;sock.fd=accept(listenSocket.fd,(sockaddr*)&peer,&length);if(sock.fd<0){svcSleepThread(50000000);continue;}client=sock.fd;ioStage="authentication";
  try{require(fcntl(client,F_SETFL,O_NONBLOCK)>=0,"Configure PC socket mode");note("COMPANION_ACCEPT v032 nonblocking_retry=1\n");auto auth=fields(getLine());bool ok=auth.size()==2&&((auth[0]=="AUTH"&&paired&&auth[1]==token)||(auth[0]=="PAIR"&&!paired&&attempts++<5&&armGetSystemTick()<expires&&auth[1]==pin));require(ok,"Pairing rejected; reopen companion mode for a new code");paired=true;line("SESSION\t"+token);cb.display("PC connected: "+address,"Choose a backup or restore in Game Vault Companion. B exits.");
   ioStage="command";auto f=fields(getLine());require(!f.empty(),"Empty command");ioStage="operation";note("COMPANION_COMMAND %s\n",f[0].c_str());if(f[0]=="LIBRARY"){json(cb.library());}
   else if(f[0]=="PROFILES"&&f.size()==1){json(cb.profiles());}
   else if(f[0]=="DOWNLOAD"&&f.size()==2){auto item=lookup(cb,f[1]);tree(item.path);line("DONE\t"+item.id);}
   else if(f[0]=="BACKUP"&&f.size()==5){uint64_t title=number(f[1],16),save=number(f[3],16);unsigned mask=number(f[2]);require(mask&&!(mask&~7)&&backupSnapshotName(f[4])&&f[4].substr(0,16)==idstr(title),"Backup selection invalid");mkdir("sdmc:/gamevault/exports",0777);streamRoot="sdmc:/gamevault/exports/"+f[4];try{cb.backup(title,mask,save,streamRoot);tree(streamRoot);streamRoot.clear();line("DONE\t"+f[4]);}catch(...){streamRoot.clear();throw;}}
   else if(f[0]=="IMPORT"&&f.size()==2){importSnapshot(f[1]);}
   else if(f[0]=="RESTORE"&&f.size()==5){auto item=lookup(cb,f[1]);unsigned mask=number(f[2]);require(mask&&!(mask&~item.components),"Restore selection unavailable");cb.restore(item.path,mask,unhex(f[3]),number(f[4],16));line("DONE\tRESTORE_VERIFIED");}
   else throw std::runtime_error("Unknown companion command");
  }catch(const std::exception&e){streamRoot.clear();note("COMPANION_STOP %s\n",e.what());try{line("ERROR\t"+encoded(e.what()));}catch(...){}cb.display("PC Companion: "+address,e.what());}
  client=-1;
 }
}

