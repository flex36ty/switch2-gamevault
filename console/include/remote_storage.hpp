#pragma once
#include <string>
#include <vector>
struct RemoteTarget {std::string host,path,user,password,share,domain;unsigned port=21;bool smb=false;bool configured()const{return !host.empty();}};
struct RemoteSettings {RemoteTarget ftp,smb;int selected=0;RemoteSettings(){smb.smb=true;smb.port=445;}};
RemoteSettings loadRemoteSettings();
void saveRemoteSettings(const RemoteSettings&);
void validateRemoteTarget(const RemoteTarget&);
void testRemoteTarget(const RemoteTarget&);
std::vector<std::string> listRemoteBackups(const RemoteTarget&);
void uploadRemoteBackup(const RemoteTarget&,const std::string& local,bool decrypted=false);
std::string downloadRemoteBackup(const RemoteTarget&,const std::string& snapshot);
