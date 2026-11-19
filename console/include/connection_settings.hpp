#pragma once
#include <string>
#include <sstream>
struct ConnectionSettings {
 bool enabled[4]={true,true,true,true};
 int automatic=-1;
 std::string encode()const {
  return "GVCONN2\n"+std::to_string(enabled[0])+" "+std::to_string(enabled[1])+" "+std::to_string(enabled[2])+" "+std::to_string(enabled[3])+" "+std::to_string(automatic)+"\n";
 }
 static ConnectionSettings decode(const std::string& text){
  ConnectionSettings out;std::istringstream in(text);std::string magic,extra;int a,b,c,d,e=1;
  if(!(in>>magic>>a>>b>>c)||(magic!="GVCONN1"&&magic!="GVCONN2"))return out;
  if(magic=="GVCONN2"&&!(in>>e))return out;
  if(!(in>>d)||a<0||a>1||b<0||b>1||c<0||c>1||e<0||e>1||d< -1||d>(magic=="GVCONN2"?3:2)||(in>>extra))return out;
  out.enabled[0]=a;out.enabled[1]=b;out.enabled[2]=c;out.enabled[3]=e;
  out.automatic=d>=0&&out.enabled[d]?d:-1;return out;
 }
};
