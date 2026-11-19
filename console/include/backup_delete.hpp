#pragma once
#include <string>
inline constexpr const char* backupRoots[]={"sdmc:/gamevault/backups","sdmc:/game-backups"};
inline bool backupSnapshotName(const std::string& n){return n.size()==33&&n[16]=='-'&&n.substr(0,16).find_first_not_of("0123456789abcdef")==std::string::npos&&n.substr(17).find_first_not_of("0123456789abcdef")==std::string::npos;}
inline bool deletableBackupPath(const std::string& path,const std::string& title){for(const auto* base:backupRoots){const std::string root=std::string(base)+"/";if(title.size()==16&&path.compare(0,root.size(),root)==0&&backupSnapshotName(path.substr(root.size()))&&path.substr(root.size(),16)==title)return true;}return false;}

inline bool backupChildName(const std::string& n){return !n.empty()&&n!="."&&n!=".."&&n.find_first_of("/\\:")==std::string::npos;}
