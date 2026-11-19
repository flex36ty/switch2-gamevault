#pragma once
#include <string>
inline bool backupRootValid(const std::string&p){
 if(p.size()<2||p.size()>=128||p[0]!='/'||p.back()=='/'||p.find("//")!=std::string::npos)return false;
 for(char c:p)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='_'))return false;
 return true;
}
