#pragma once
#include <string>
inline bool vaultHex(const std::string&s,size_t n){return s.size()==n&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline bool vaultImportFile(const std::string&p){
 if(p=="catalog.bin"||p=="created.txt"||p=="game-name.txt"||p=="game-icon.jpg")return true;
 if(p.rfind("p0/",0)==0||p.rfind("p1/",0)==0){auto f=p.substr(3);return f=="package.bin"||(f.size()==36&&vaultHex(f.substr(0,32),32)&&f.substr(32)==".nca")||(f.size()==41&&vaultHex(f.substr(0,32),32)&&f.substr(32)==".cnmt.nca");}
 if(p.size()>22&&p.rfind("save-",0)==0&&vaultHex(p.substr(5,16),16)&&p[21]=='/'){auto f=p.substr(22);return f=="save-info.bin"||f=="save-attribute.bin"||f=="manifest.txt"||f=="seal.bin"||f=="COMPLETE.txt"||f.rfind("files/",0)==0;}
 return false;
}
