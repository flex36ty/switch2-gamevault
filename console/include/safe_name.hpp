#pragma once
#include <cstring>
#include <string>
inline bool safeSaveName(const char* name){if(!name||!*name||!strcmp(name,".")||!strcmp(name,"..")||strpbrk(name,"/\\:"))return false;size_t n=strlen(name);if(name[n-1]=='.'||name[n-1]==' ')return false;for(size_t i=0;i<n;i++)if((unsigned char)name[i]<32||(unsigned char)name[i]>=127)return false;return true;}
inline std::string foldedSavePath(std::string name){for(char& c:name)if(c>='A'&&c<='Z')c=(char)(c-'A'+'a');return name;}
