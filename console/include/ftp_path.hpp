#pragma once
#include <string>
#include <vector>
#include <stdexcept>
inline std::string ftpPath(const std::string& cwd,const std::string& input){
 std::string raw=input.empty()?cwd:input[0]=='/'?input:cwd+"/"+input;
 if(raw.size()>1000)throw std::runtime_error("Path too long");
 for(unsigned char c:raw)if(c<32||c==127||c=='\\'||c==':')throw std::runtime_error("Invalid path");
 std::vector<std::string> parts;size_t at=0;
 while(at<=raw.size()){auto end=raw.find('/',at);if(end==std::string::npos)end=raw.size();auto p=raw.substr(at,end-at);if(p==".."){if(parts.empty())throw std::runtime_error("Outside SD root");parts.pop_back();}else if(!p.empty()&&p!=".")parts.push_back(p);at=end+1;}
 std::string out;for(const auto&p:parts)out+="/"+p;return out.empty()?"/":out;
}
