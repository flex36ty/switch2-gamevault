#ifdef GAMEVAULT_FTP_HOST_TEST
#include "../../tests/ftp_host.hpp"
#else
#include "common.hpp"
#endif
#include "connections.hpp"
#if !defined(GAMEVAULT_FTP_HOST_TEST) || defined(GAMEVAULT_NETWORK_HOST_TEST)
#include "network_runtime.hpp"
#endif
#include "ftp_path.hpp"
#include <sys/socket.h>
#include <cstdarg>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <memory>
namespace {
void ftpTrace(const char*fmt,...){FILE*f=fopen("sdmc:/game-vault-ftp.log","a");if(!f)return;va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fclose(f);}

struct FtpStop:std::runtime_error{FtpStop():std::runtime_error("FTP stopped"){}};
thread_local std::function<void()> ftpPoll;
void ftpPump(){try{if(ftpPoll)ftpPoll();else pump();}catch(...){throw FtpStop();}}
struct Socket {
 int fd=-1;Socket()=default;explicit Socket(int value):fd(value){}
 Socket(const Socket&)=delete;Socket&operator=(const Socket&)=delete;
 ~Socket(){reset();}void reset(int value=-1){if(fd>=0)close(fd);fd=value;}
};
bool again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
void nonblock(int fd){require(fcntl(fd,F_SETFL,O_NONBLOCK)>=0,"Socket nonblocking mode");}
void ftpPause(){ftpPump();svcSleepThread(1000000);}
void sendAll(int fd,const char* p,size_t size){
 auto deadline=armGetSystemTick()+armNsToTicks(60000000000ULL);
 while(size){ftpPump();int n=send(fd,p,std::min<size_t>(size,65536),0);if(n>0){size-=n;p+=n;deadline=armGetSystemTick()+armNsToTicks(60000000000ULL);}else {require(n<0&&again(),"FTP connection closed");require(armGetSystemTick()<deadline,"FTP send timeout");ftpPause();}}
}
void reply(int fd,const std::string& s){std::string out=s+"\r\n";sendAll(fd,out.data(),out.size());}
std::string command(int fd){
 std::string out;auto end=armGetSystemTick()+armNsToTicks(300000000000ULL);
 while(out.size()<2048){ftpPump();char c;int n=recv(fd,&c,1,0);if(n==1){if(c=='\n')return out;if(c!='\r'){require(c!=0,"Invalid command");out+=c;}}else{require(n<0&&again(),"FTP client disconnected");require(armGetSystemTick()<end,"FTP command timeout");ftpPause();}}
 throw std::runtime_error("FTP command too long");
}
std::string disk(const std::string& path){
 // Reject links in every existing component, not only the leaf.
 for(size_t at=1;at<=path.size();++at)if(at==path.size()||path[at]=='/'){
  struct stat st{};std::string prefix="sdmc:"+path.substr(0,at);
  if(!lstat(prefix.c_str(),&st))require(!S_ISLNK(st.st_mode),"Links are not exposed");
 }
 return "sdmc:"+path;
}
void writable(const std::string& path){
 std::string lower=path;for(char&c:lower)c=std::tolower((unsigned char)c);
 require(lower!="/"&&lower!="/switch"&&lower!="/switch/game-vault.nro"&&lower!="/hbmenu.nro"&&lower!="/kaihen"&&lower.rfind("/kaihen/",0)!=0,"Active app or HEN path protected");
}
int listener(unsigned port){
 Socket s(socket(AF_INET,SOCK_STREAM,0));require(s.fd>=0,"FTP socket");int one=1;setsockopt(s.fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=INADDR_ANY;addr.sin_port=htons(port);
 require(!bind(s.fd,(sockaddr*)&addr,sizeof(addr))&&!listen(s.fd,1),"FTP port unavailable");nonblock(s.fd);int fd=s.fd;s.fd=-1;return fd;
}
int acceptData(int passive,in_addr peer){
 require(passive>=0,"Use PASV or EPSV first");auto end=armGetSystemTick()+armNsToTicks(30000000000ULL);
 while(armGetSystemTick()<end){ftpPump();sockaddr_in addr{};socklen_t len=sizeof(addr);Socket data(accept(passive,(sockaddr*)&addr,&len));if(data.fd>=0){if(addr.sin_addr.s_addr!=peer.s_addr)continue;nonblock(data.fd);int fd=data.fd;data.fd=-1;return fd;}require(again(),"FTP data accept failed");ftpPause();}
 throw std::runtime_error("FTP data connection timed out");
}
std::string facts(const std::string& name,const struct stat& st){
 char date[32]="19700101000000";auto*t=gmtime(&st.st_mtime);if(t)strftime(date,sizeof(date),"%Y%m%d%H%M%S",t);
 return std::string("type=")+(S_ISDIR(st.st_mode)?"dir":"file")+";size="+std::to_string(st.st_size)+";modify="+date+"; "+name;
}
}
void vaultFtp(const ConnectionDisplay& display,const std::function<void()>& poll){
 ftpPoll=poll;
#if defined(GAMEVAULT_FTP_HOST_TEST) && !defined(GAMEVAULT_NETWORK_HOST_TEST)
 check(socketInitializeDefault(),"FTP sockets");struct End{~End(){socketExit();}}end;
#else
 vaultNetworkAcquire();struct End{~End(){vaultNetworkRelease();}}end;
#endif
 Socket server(listener(1337));unsigned random=0;randomGet(&random,sizeof(random));char pin[5];snprintf(pin,sizeof(pin),"%04u",random%10000);
 in_addr ip{};ip.s_addr=gethostid();std::string address=inet_ntoa(ip);
 auto show=[&](const std::string& status){display("FTP  "+address+":1337",std::string("User: vault   Password: ")+pin+"\n"+status+"\n"+(poll?"B returns to menu. FTP stays on.":"B stops FTP.")+" Passive mode. SD card only.");};
 show("Waiting for a client");ftpTrace("VAULT_FTP_STARTED port=1337 sd_only=1\n");unsigned attempts=0;
 for(;;){
  ftpPump();sockaddr_in peer{};socklen_t len=sizeof(peer);Socket client(accept(server.fd,(sockaddr*)&peer,&len));if(client.fd<0){require(again(),"FTP accept");ftpPause();continue;}nonblock(client.fd);
  try{
   reply(client.fd,"220 Game Vault SD FTP");bool user=false,auth=false;std::string cwd="/",renameFrom;uint64_t offset=0;Socket passive;
   for(;;){
    std::string line=command(client.fd);auto split=line.find(' ');auto cmd=line.substr(0,split);std::string arg=split==std::string::npos?"":line.substr(split+1);for(char&c:cmd)c=std::toupper((unsigned char)c);
    if(cmd=="QUIT"){reply(client.fd,"221 Goodbye");break;}
    if(cmd=="USER"){auth=false;user=arg=="vault";reply(client.fd,"331 Password required");continue;}
    if(cmd=="PASS"){auth=user&&arg==pin&&attempts<5;if(!auth)++attempts;reply(client.fd,auth?"230 Logged in":"530 Login failed");if(attempts>=5)break;continue;}
    if(cmd=="FEAT"){reply(client.fd,"211-Features\r\n UTF8\r\n EPSV\r\n SIZE\r\n REST STREAM\r\n MLST type*;size*;modify*;\r\n211 End");continue;}
    if(!auth){reply(client.fd,"530 Log in first");continue;}
    try{
#if !defined(GAMEVAULT_FTP_HOST_TEST) || defined(GAMEVAULT_NETWORK_HOST_TEST)
     VaultStorageLease access;
#endif
     if(cmd=="SYST")reply(client.fd,"215 UNIX Type: L8");
     else if(cmd=="TYPE"||cmd=="OPTS"||cmd=="NOOP")reply(client.fd,"200 OK");
     else if(cmd=="PWD"||cmd=="XPWD"){std::string escaped;for(char c:cwd){escaped+=c;if(c=='\"')escaped+=c;}reply(client.fd,"257 \""+escaped+"\"");}
     else if(cmd=="CWD"||cmd=="CDUP"){auto path=ftpPath(cwd,cmd=="CDUP"?"..":arg);struct stat st{};require(!stat(disk(path).c_str(),&st)&&S_ISDIR(st.st_mode),"Directory unavailable");cwd=path;reply(client.fd,"250 Directory changed");}
     else if(cmd=="PASV"||cmd=="EPSV"){
      passive.reset(listener(0));sockaddr_in bound{};socklen_t n=sizeof(bound);require(!getsockname(passive.fd,(sockaddr*)&bound,&n),"Passive address");unsigned port=ntohs(bound.sin_port);
      if(cmd=="EPSV")reply(client.fd,"229 Entering Extended Passive Mode (|||"+std::to_string(port)+"|)");
      else{std::string host=address;for(char&c:host)if(c=='.')c=',';reply(client.fd,"227 Entering Passive Mode ("+host+","+std::to_string(port/256)+","+std::to_string(port%256)+")");}
     }
     else if(cmd=="REST"){require(!arg.empty()&&arg.size()<=19&&arg.find_first_not_of("0123456789")==std::string::npos,"Invalid offset");offset=std::stoull(arg);require(offset<=INT64_MAX,"Offset too large");reply(client.fd,"350 Resume offset set");}
     else if(cmd=="SIZE"||cmd=="MDTM"||cmd=="MLST"){
      auto path=ftpPath(cwd,arg);struct stat st{};require(!lstat(disk(path).c_str(),&st),"File unavailable");
      if(cmd=="MLST")reply(client.fd,"250-Entry\r\n "+facts(path,st)+"\r\n250 End");
      else if(cmd=="SIZE"){require(S_ISREG(st.st_mode),"Not a file");reply(client.fd,"213 "+std::to_string(st.st_size));}
      else{char date[32];auto*t=gmtime(&st.st_mtime);require(t,"Invalid time");strftime(date,sizeof(date),"%Y%m%d%H%M%S",t);reply(client.fd,std::string("213 ")+date);}
     }
     else if(cmd=="LIST"||cmd=="NLST"||cmd=="MLSD"){
      if(arg=="-a"||arg=="-l"||arg=="-la")arg.clear();auto path=ftpPath(cwd,arg);std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(disk(path).c_str()),closedir);require(bool(dir),"Directory unavailable");
      require(passive.fd>=0,"Use passive mode");reply(client.fd,"150 Opening data connection");Socket data(acceptData(passive.fd,peer.sin_addr));passive.reset();
      while(auto*e=readdir(dir.get())){ftpPump();std::string name=e->d_name;if(name=="."||name==".."||name.find_first_of("\r\n")!=std::string::npos)continue;struct stat st{};auto child=ftpPath(path,name);if(lstat(disk(child).c_str(),&st)||(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode)))continue;std::string row;
       if(cmd=="MLSD")row=facts(name,st);else if(cmd=="NLST")row=name;else row=std::string(S_ISDIR(st.st_mode)?"drwxr-xr-x":"-rw-r--r--")+" 1 owner group "+std::to_string(st.st_size)+" Jan 01  2026 "+name;
       reply(data.fd,row);
      }data.reset();reply(client.fd,"226 Listing complete");
     }
     else if(cmd=="RETR"||cmd=="STOR"){
      bool upload=cmd=="STOR";auto path=ftpPath(cwd,arg);if(upload)writable(path);std::string file=disk(path);struct stat st{};bool exists=!stat(file.c_str(),&st);
      require(!exists||S_ISREG(st.st_mode),"Not a regular file");require(!offset||(exists&&uint64_t(st.st_size)>=offset),"Invalid resume position");
      require(passive.fd>=0,"Use passive mode");
      // Accept data before opening a writable file, so connection failures cannot truncate it.
      reply(client.fd,"150 Opening data connection");Socket data(acceptData(passive.fd,peer.sin_addr));passive.reset();
      File output(fopen(file.c_str(),upload?(offset?"r+b":"wb"):"rb"),fclose);require(bool(output),"Cannot open file");require(!fseeko(output.get(),offset,SEEK_SET),"Seek failed");uint64_t done=offset;offset=0;
      std::vector<char> buffer(65536);auto deadline=armGetSystemTick()+armNsToTicks(60000000000ULL);show(std::string(upload?"Receiving ":"Sending ")+path);
      for(;;){ftpPump();if(upload){int n=recv(data.fd,buffer.data(),buffer.size(),0);if(n==0)break;if(n<0){require(again(),"Data receive failed");require(armGetSystemTick()<deadline,"Data receive timeout");ftpPause();continue;}require(fwrite(buffer.data(),1,n,output.get())==size_t(n),"SD write failed; partial retained");done+=n;deadline=armGetSystemTick()+armNsToTicks(60000000000ULL);}else{size_t n=fread(buffer.data(),1,buffer.size(),output.get());if(!n){require(!ferror(output.get()),"SD read failed");break;}sendAll(data.fd,buffer.data(),n);done+=n;}}
      if(upload)flush(output.get());require(!fclose(output.release()),"File close failed");data.reset();reply(client.fd,"226 Transfer complete");show("Transfer complete: "+std::to_string(done)+" bytes");
     }
     else if(cmd=="MKD"||cmd=="RMD"||cmd=="DELE"||cmd=="RNFR"||cmd=="RNTO"){
      auto path=ftpPath(cwd,arg);writable(path);auto file=disk(path);
      if(cmd=="MKD"){require(!mkdir(file.c_str(),0777),"Create directory failed");reply(client.fd,"257 Directory created");}
      else if(cmd=="RMD"){require(!rmdir(file.c_str()),"Remove empty directory failed");reply(client.fd,"250 Removed");}
      else if(cmd=="DELE"){require(!unlink(file.c_str()),"Delete failed");reply(client.fd,"250 Deleted");}
      else if(cmd=="RNFR"){struct stat st{};require(!lstat(file.c_str(),&st),"Source unavailable");renameFrom=file;reply(client.fd,"350 Send RNTO");}
      else{require(!renameFrom.empty(),"Send RNFR first");struct stat st{};require(lstat(file.c_str(),&st)!=0&&errno==ENOENT,"Destination already exists");require(!rename(renameFrom.c_str(),file.c_str()),"Rename failed");renameFrom.clear();reply(client.fd,"250 Renamed");}
     }
     else reply(client.fd,"502 Command not supported; use passive binary transfers");
    }catch(const FtpStop&){throw;}catch(const std::exception&e){ftpTrace("VAULT_FTP_COMMAND_FAILED cmd=%s error=%s\n",cmd.c_str(),e.what());passive.reset();offset=0;reply(client.fd,"550 Operation failed; partial files retained if applicable");}
   }
  }catch(const FtpStop&){throw;}catch(const std::exception&e){ftpTrace("VAULT_FTP_CLIENT_END %s\n",e.what());}
  if(attempts>=5){randomGet(&random,sizeof(random));snprintf(pin,sizeof(pin),"%04u",random%10000);attempts=0;}
  show("Waiting for a client");
 }
}
