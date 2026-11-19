#ifdef GAMEVAULT_WEB_HOST_TEST
#include "../../tests/web_host.hpp"
#else
#include "common.hpp"
#include "network_runtime.hpp"
#endif
#include "web_mode.hpp"
#include "web_page.hpp"
#include "emulator_dump.hpp"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <map>
#include <chrono>
namespace {
struct Socket{int fd=-1;~Socket(){if(fd>=0)close(fd);}};
void sendAll(int fd,const void*data,size_t size){const char*p=(const char*)data;auto last=std::chrono::steady_clock::now();while(size){pump();int n=send(fd,p,size,0);if(n>0){p+=n;size-=n;last=std::chrono::steady_clock::now();}else {require(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR),"Browser disconnected; download incomplete");require(std::chrono::steady_clock::now()-last<std::chrono::seconds(60),"Browser transfer timeout");svcSleepThread(10000000);}}}
void sendText(int fd,const std::string&s){sendAll(fd,s.data(),s.size());}
void json(int fd,const std::string&s,int code=200){sendText(fd,"HTTP/1.1 "+std::to_string(code)+(code==200?" OK":" Error")+"\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(s.size())+"\r\n\r\n"+s);}
std::string decode(const std::string&s){std::string out;for(size_t i=0;i<s.size();i++){if(s[i]=='%'){require(i+2<s.size(),"Invalid URL");auto n=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};int a=n(s[i+1]),b=n(s[i+2]);require(a>=0&&b>=0,"Invalid URL");out+=char(a*16+b);i+=2;}else out+=s[i]=='+'?' ':s[i];}require(out.find_first_of("\r\n") == std::string::npos&&out.find('\0')==std::string::npos,"Invalid URL characters");return out;}
u64 number(const std::string&s){require(!s.empty()&&s.size()<=16&&s.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos,"Invalid numeric ID");return strtoull(s.c_str(),nullptr,16);}
std::string pax(const std::string&key,const std::string&value){std::string body=" "+key+"="+value+"\n";size_t size=body.size()+1;for(;;){size_t n=body.size()+std::to_string(size).size();if(n==size)return std::to_string(size)+body;size=n;}}
void tarHeader(int fd,const std::string&name,u64 size,char type){char h[512]{};require(name.size()<100&&size<077777777777ULL,"TAR header bounds");memcpy(h,name.data(),name.size());snprintf(h+100,8,"%07o",0644);snprintf(h+108,8,"%07o",0);snprintf(h+116,8,"%07o",0);snprintf(h+124,12,"%011llo",(unsigned long long)size);snprintf(h+136,12,"%011o",0);memset(h+148,' ',8);h[156]=type;memcpy(h+257,"ustar",5);memcpy(h+263,"00",2);unsigned sum=0;for(unsigned char c:h)sum+=c;snprintf(h+148,7,"%06o",sum);h[155]=' ';sendAll(fd,h,512);}
void padding(int fd,u64 size){char zero[512]{};if(size%512)sendAll(fd,zero,512-size%512);}
void tarStart(int fd,const std::string&name,u64 size){std::string ext=pax("path",name)+pax("size",std::to_string(size));tarHeader(fd,"PaxHeaders/entry",ext.size(),'x');sendText(fd,ext);padding(fd,ext.size());tarHeader(fd,"content",size<077777777777ULL?size:0,'0');}
void tarText(int fd,const std::string&name,const std::string&data){tarStart(fd,name,data.size());sendText(fd,data);padding(fd,data.size());}
void download(int fd,const EmulatorDump&dump){sendText(fd,"HTTP/1.1 200 OK\r\nContent-Type: application/x-tar\r\nContent-Disposition: attachment; filename=\""+idstr(dump.title)+"-matched-base.tar\"\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");std::vector<u8>buf(262144);std::string sums;
 for(const auto&f:dump.files){File input(fopen(f.path.c_str(),"rb"),fclose);require(bool(input),"Cannot open export file");tarStart(fd,f.name,f.size);Sha256Context hash;sha256ContextCreate(&hash);u64 done=0;while(done<f.size){pump();size_t n=fread(buf.data(),1,std::min<u64>(buf.size(),f.size-done),input.get());require(n>0,"Short export read");sendAll(fd,buf.data(),n);sha256ContextUpdate(&hash,buf.data(),n);done+=n;vaultTransfer("Downloading emulator files",f.name.c_str(),done,f.size);}padding(fd,f.size);u8 digest[32];sha256ContextGetHash(&hash,digest);sums+=hex(digest,32)+"  "+f.name+"\n";}
 tarText(fd,"export.json",dump.metadata);tarText(fd,"SHA256SUMS.txt",sums);char end[1024]{};sendAll(fd,end,sizeof(end));note("WEB_EMULATOR_SENT title=%s files=%zu\n",idstr(dump.title).c_str(),dump.files.size());
}
}
void vaultWeb(const CompanionCallbacks&cb){
 vaultNetworkAcquire();struct End{~End(){closeEmulatorDump();vaultNetworkRelease();}}end;Socket server;server.fd=socket(AF_INET,SOCK_STREAM,0);require(server.fd>=0,"Web socket");int one=1;setsockopt(server.fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=INADDR_ANY;addr.sin_port=htons(8080);require(!bind(server.fd,(sockaddr*)&addr,sizeof(addr))&&!listen(server.fd,8),"Web listen on 8080");require(fcntl(server.fd,F_SETFL,O_NONBLOCK)>=0,"Web listener mode");in_addr ip{};ip.s_addr=gethostid();std::string host=std::string(inet_ntoa(ip))+":8080";
 for(;;){cb.display("Game Vault Web", "http://"+host+"\nOpen this address on your PC. No access code.\nB returns to Game Vault; FTP stays on.");pump();Socket client;client.fd=accept(server.fd,nullptr,nullptr);if(client.fd<0){svcSleepThread(50000000);continue;}bool response=false;
  try{require(fcntl(client.fd,F_SETFL,O_NONBLOCK)>=0,"Web client mode");std::string req;auto started=std::chrono::steady_clock::now();while(req.find("\r\n\r\n")==std::string::npos){pump();char b[1024];int n=recv(client.fd,b,sizeof(b),0);if(n>0){req.append(b,n);require(req.size()<=8192,"Request too large");}else{require(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR),"Browser disconnected");require(std::chrono::steady_clock::now()-started<std::chrono::seconds(10),"Request timeout");svcSleepThread(10000000);}}
   size_t a=req.find(' '),b=req.find(' ',a+1);require(a!=std::string::npos&&b!=std::string::npos,"Invalid HTTP request");std::string method=req.substr(0,a),target=req.substr(a+1,b-a-1);std::map<std::string,std::string> headers;size_t p=req.find("\r\n")+2;while(p<req.size()){size_t e=req.find("\r\n",p);if(e==p||e==std::string::npos)break;std::string line=req.substr(p,e-p);auto colon=line.find(':');require(colon!=std::string::npos,"Invalid HTTP header");std::string key=line.substr(0,colon);for(char&c:key)if(c>='A'&&c<='Z')c+=32;auto value=line.substr(colon+1);while(!value.empty()&&value.front()==' ')value.erase(0,1);headers[key]=value;p=e+2;}
   require(headers["host"]==host,"Use the console IP shown on screen");require(method=="GET"||method=="POST","Unsupported method");if(method=="POST"){require(headers["x-game-vault"]=="1"&&headers["origin"]=="http://"+host,"Open the Game Vault page before changing files");require(!headers.count("transfer-encoding")&&(!headers.count("content-length")||headers["content-length"]=="0"),"Request body not supported");}
   std::map<std::string,std::string> args;size_t q=target.find('?');std::string path=target.substr(0,q);if(q!=std::string::npos){p=q+1;while(p<target.size()){auto e=target.find('&',p);if(e==std::string::npos)e=target.size();auto row=target.substr(p,e-p);auto eq=row.find('=');require(eq!=std::string::npos,"Invalid query");args[decode(row.substr(0,eq))]=decode(row.substr(eq+1));p=e+1;}}
   VaultStorageLease access;
   if(path=="/"&&method=="GET"){response=true;sendText(client.fd,"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(sizeof(vaultWebPage)-1)+"\r\n\r\n");sendAll(client.fd,vaultWebPage,sizeof(vaultWebPage)-1);}
   else if(path=="/api/library"&&method=="GET"){auto out=cb.library();response=true;json(client.fd,out);}
   else if(path=="/api/prepare"&&method=="POST"){const auto&d=prepareEmulatorDump(number(args["title"]));response=true;json(client.fd,"{\"files\":"+std::to_string(d.files.size())+",\"metadata\":"+d.metadata+"}");}
   else if(path=="/api/dump-sd"&&method=="POST"){auto out=dumpEmulatorToSd(number(args["title"]));response=true;json(client.fd,"{\"path\":"+companionJson(out)+"}");}
   else if(path=="/download/emulator"&&method=="GET"){const auto&d=currentEmulatorDump();require(d.title&&d.title==number(args["title"]),"Prepare this game again before downloading");response=true;download(client.fd,d);}
   else if(path=="/api/backup"&&method=="POST"){u64 id=number(args["title"]),mask=number(args["mask"]);require(mask==1||mask==2||mask==4,"Choose one backup component");std::string root="sdmc:/gamevault/backups/"+idstr(id)+"-"+idstr(armGetSystemTick());cb.backup(id,mask,mask==4?number(args["save"]):0,root);response=true;json(client.fd,"{\"path\":"+companionJson(root)+"}");}
   else if(path=="/api/restore"&&method=="POST"){u64 mask=number(args["mask"]);require(mask==1||mask==2,"Choose base or update");auto list=cb.snapshots();auto it=std::find_if(list.begin(),list.end(),[&](const auto&s){return s.id==args["snapshot"];});require(it!=list.end()&&(it->components&mask),"Selected backup component unavailable");cb.restore(it->path,mask,"",0);response=true;json(client.fd,"{\"verified\":true}");}
   else{response=true;json(client.fd,"{\"error\":\"Not found\"}",404);}
  }catch(const std::exception&e){if(std::string(e.what()).rfind("Cancelled",0)==0)throw;note("WEB_REQUEST_STOP %s\n",e.what());if(!response)try{json(client.fd,"{\"error\":"+companionJson(e.what())+"}",400);}catch(...){} }
 }
}
