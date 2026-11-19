#include "ui.hpp"
#include "tab_filter.hpp"
#include "font_data.h"
#include "brand_icon.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/stb_truetype.h"
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 1024
#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"
static uint32_t rgb(int r,int g,int b){return 0xff000000u|(b<<16)|(g<<8)|r;}
static const auto bg=rgb(22,24,27),panel=rgb(32,35,39),card=rgb(47,50,55),ink=rgb(244,240,232),muted=rgb(173,176,182),amber=rgb(241,187,104),dim=rgb(65,69,76);
static stbtt_fontinfo font;
Painter::Painter():pixels(1280*720){stbtt_InitFont(&font,vault_font,0);}
Icon decodeIcon(const std::vector<unsigned char>&b){Icon out;int channels=0;if(b.empty()||b.size()>0x200000)return out;if(!stbi_info_from_memory(b.data(),b.size(),&out.w,&out.h,&channels)||out.w<1||out.h<1||out.w>1024||out.h>1024){out.w=out.h=0;return out;}auto*p=stbi_load_from_memory(b.data(),b.size(),&out.w,&out.h,&channels,4);if(p){out.rgba.assign(p,p+out.w*out.h*4);stbi_image_free(p);}return out;}
void Painter::rect(int x,int y,int w,int h,uint32_t c,int radius){for(int yy=std::max(0,y);yy<std::min(720,y+h);yy++)for(int xx=std::max(0,x);xx<std::min(1280,x+w);xx++){int dx=std::max({x+radius-xx,0,xx-(x+w-radius-1)}),dy=std::max({y+radius-yy,0,yy-(y+h-radius-1)});if(radius&&dx*dx+dy*dy>radius*radius)continue;pixels[yy*1280+xx]=c;}}
static uint32_t nextCp(const std::string&s,size_t&at){unsigned char c=s[at++];if(c<128)return c;unsigned n=c>=240?3:c>=224?2:c>=192?1:0;uint32_t cp=c&((1u<<(6-n))-1);if(!n||at+n>s.size())return '?';for(unsigned i=0;i<n;i++){unsigned char d=s[at];if((d&192)!=128)return '?';at++;cp=(cp<<6)|(d&63);}return cp;}
void Painter::text(int x,int y,const std::string&s,int size,uint32_t color,int maxwidth){int cursor=x,total=0,dots=0;float scale=stbtt_ScaleForPixelHeight(&font,size);size_t measure=0;while(measure<s.size()){auto cp=nextCp(s,measure);int advance=0,bearing=0;stbtt_GetCodepointHMetrics(&font,cp,&advance,&bearing);total+=std::max(1,(int)std::round(advance*scale));}int dotAdvance=0,bearing=0;stbtt_GetCodepointHMetrics(&font,'.',&dotAdvance,&bearing);dots=3*std::max(1,(int)std::round(dotAdvance*scale));bool clipped=total>maxwidth;size_t at=0;while(at<s.size()){
 uint32_t cp=nextCp(s,at);if(cp<32)cp=' ';uint64_t key=((uint64_t)size<<32)|cp;auto it=glyphs.find(key);
 if(it==glyphs.end()){Glyph g{};float scale=stbtt_ScaleForPixelHeight(&font,size);int a=0,b=0;stbtt_GetCodepointHMetrics(&font,cp,&a,&b);g.advance=std::max(1,(int)std::round(a*scale));unsigned char*p=stbtt_GetCodepointBitmap(&font,0,scale,cp,&g.w,&g.h,&g.x,&g.y);if(p){g.pixels.assign(p,p+g.w*g.h);stbtt_FreeBitmap(p,nullptr);}if(glyphs.size()>4096)glyphs.clear();it=glyphs.emplace(key,std::move(g)).first;}
 const auto&g=it->second;if(cursor+g.advance>x+maxwidth-(clipped?dots:0)){if(clipped&&maxwidth>=dots)text(cursor,y,"...",size,color,dots);break;}
 for(int yy=0;yy<g.h;yy++)for(int xx=0;xx<g.w;xx++){int px=cursor+g.x+xx,py=y+size+g.y+yy;if(px<0||px>=1280||py<0||py>=720)continue;unsigned a=g.pixels[yy*g.w+xx];if(!a)continue;uint32_t base=pixels[py*1280+px];int r=((color&255)*a+(base&255)*(255-a))/255,gr=(((color>>8)&255)*a+((base>>8)&255)*(255-a))/255,b=(((color>>16)&255)*a+((base>>16)&255)*(255-a))/255;pixels[py*1280+px]=rgb(r,gr,b);}
 cursor+=g.advance;
 }}
void Painter::icon(int x,int y,int size,const Icon&i,const std::string&label){if(i.rgba.empty()){rect(x,y,size,size,rgb(61,51,37),14);text(x+size/4,y+size/5,label.empty()?"?":label.substr(0,1),size/2,amber,size);return;}for(int yy=0;yy<size;yy++)for(int xx=0;xx<size;xx++){if(x+xx<0||x+xx>=1280||y+yy<0||y+yy>=720)continue;unsigned pos=((yy*i.h/size)*i.w+xx*i.w/size)*4;pixels[(y+yy)*1280+x+xx]=rgb(i.rgba[pos],i.rgba[pos+1],i.rgba[pos+2]);}}
void Painter::draw(const Screen&original){
 Screen filtered=original;
 if(!original.busy&&!original.confirm&&!original.finished){filtered.games.clear();filtered.game=0;for(auto i:tabItems(original.games,original.tab)){if(i==original.game)filtered.game=filtered.games.size();filtered.games.push_back(original.games[i]);}}
 const Screen&s=filtered;std::fill(pixels.begin(),pixels.end(),bg);static Icon brand=decodeIcon(std::vector<unsigned char>(vault_brand_icon,vault_brand_icon+sizeof(vault_brand_icon)));icon(34,20,72,brand,"V");text(121,20,"Game Vault",36,ink);text(123,64,"BACKUP / RESTORE / CONNECT",17,muted);const char*tabs[]={"Backup","Restore","Library","Decrypted Backup","Settings"};int tx=410;for(int i=0;i<5;i++){int w=i==3?210:140;rect(tx,26,w-8,55,i==s.tab?amber:panel,13);text(tx+14,39,tabs[i],i==3?23:25,i==s.tab?bg:ink,w-28);tx+=w;}rect(38,104,1204,1,dim);
 const auto net=s.networkConnected?rgb(112,208,160):muted;
 if(s.networkWired){rect(1210,40,31,22,net,3);rect(1214,44,23,14,bg,1);rect(1223,62,5,10,net);rect(1214,71,23,3,net);}else{for(int i=0;i<3;i++)rect(1210+i*11,67-i*9,7,8+i*9,s.networkConnected&&i<int(std::max(1u,s.networkStrength))?net:dim,2);if(!s.networkConnected){text(1210,36,"x",22,muted,35);}}

 if(s.busy&&(s.phase.rfind("PC Companion: ",0)==0||s.phase.rfind("PC connected: ",0)==0)){
  bool waiting=s.file.rfind("Pairing code: ",0)==0;std::string address=s.phase.substr(14);
  rect(110,145,1060,490,panel,20);text(151,177,"DESKTOP COMPANION",18,amber);text(151,214,waiting?"Pair with your PC":"PC Link",43,ink);
  text(151,281,"Open Game Vault Companion on Windows",25,muted,950);
  rect(151,338,540,97,bg,13);text(174,352,"CONSOLE ADDRESS",17,muted);text(174,381,address,31,ink,490);
  rect(721,338,407,97,rgb(61,51,37),13);text(746,352,waiting?"PAIRING CODE":"CONNECTION",17,amber);
  if(waiting){auto pin=s.file.substr(14,4);std::string spaced;for(char c:pin){if(!spaced.empty())spaced+="   ";spaced+=c;}text(746,376,spaced,40,amber,355);}
  else text(746,382,s.phase.rfind("PC connected: ",0)==0?"Connected":"Check desktop",29,ink,355);
  text(151,473,waiting?"Enter the address and four-digit code in the desktop app.":s.message,23,muted,950);
  text(151,520,waiting?"Code expires in 2 minutes. Reopen PC Link for a fresh code.":"Keep Game Vault open while your PC transfers files.",22,muted,950);
  text(40,666,"B  Leave PC Link",23,ink);return;
 }
 if(s.transferMode){
  rect(110,145,1060,490,panel,20);text(150,175,"CONNECTIONS",18,amber);
  text(150,220,s.phase,35,ink,960);
  size_t at=0;int y=300;
  while(at<s.file.size()&&y<560){auto end=s.file.find('\n',at);if(end==std::string::npos)end=s.file.size();text(150,y,s.file.substr(at,end-at),24,muted,960);at=end+1;y+=48;}
  text(150,575,"Backup and restore are paused while this mode is open.",22,muted,960);
  text(40,666,"B  Stop connection and return to Game Vault",23,ink);return;
 }
 if(s.choosingSnapshot){
  rect(110,132,1060,510,panel,20);text(150,155,s.popupTitle,36,ink);
  text(150,202,s.popupSubtitle,22,muted,970);
  size_t first=s.snapshotChoice>2?s.snapshotChoice-2:0;
  if(first+5>s.snapshotChoices.size())first=s.snapshotChoices.size()>5?s.snapshotChoices.size()-5:0;
  for(size_t n=first;n<std::min(first+5,s.snapshotChoices.size());++n){
   int y=245+int(n-first)*62;bool active=n==s.snapshotChoice;
   rect(145,y,990,54,active?amber:card,10);
   text(165,y+10,std::to_string(n+1)+". "+s.snapshotChoices[n],24,active?bg:ink,945);
  }
  text(150,579,std::to_string(s.snapshotChoice+1)+" / "+std::to_string(s.snapshotChoices.size())+" available",21,muted);
  text(40,666,"D-pad  Browse     A  Select     B  Cancel",23,ink);return;
 }
 if(s.tab==4&&!s.busy&&!s.confirm&&!s.finished){
  text(60,135,"Connection settings",35,ink);text(60,186,"FTP runs in the background while Game Vault is open.",23,muted);
  const char*names[]={"PC Companion","Persistent FTP","USB MTP","Web interface","Open automatically","Open a connection now","Storage destinations"};
  for(int i=0;i<7;i++){int y=210+i*52;bool active=s.setting==i;rect(60,y,1160,46,active?card:panel,10);if(active)rect(60,y,4,46,amber,2);text(82,y+10,names[i],27,ink,580);
   std::string value=i<4?(s.connections.enabled[i]?"Enabled":"Disabled"):i==4?(s.connections.automatic<0?"Off":names[s.connections.automatic]):i==6?s.storageLabel:"A to choose";
   text(770,y+14,value,23,active?amber:muted,420);
  }
  text(60,591,"Automatic startup applies when Game Vault opens, not when the console boots.",21,muted,1160);
  text(60,627,s.message,20,muted,1160);text(40,675,"D-pad  Navigate     A  Change     L / R  Tabs     ZL  Connections     B  Exit",22,ink);return;
 }
 if(s.games.empty()){text(70,240,s.tab==0?"No installed games available":s.tab==1?"No complete backups on SD":"No games or backups found",36,ink);text(70,300,s.tab==1?"Create a backup in the Backup tab to see it here.":"Use L / R to switch tabs, or ZL for Connections.",24,muted);text(40,666,"L / R  Tabs     ZL  Connections     B  Exit",23,muted);return;}const auto&game=s.games[s.game];
 if(s.busy||s.confirm||s.finished){
  rect(110,145,1060,490,panel,20);icon(145,180,100,game.icon,game.title);text(275,182,game.title,34,ink,820);
  text(275,229,s.busy?s.phase:s.confirm?"Review your selection":s.passed?"Operation complete":"Operation stopped",27,amber,820);
  if(s.busy){
   std::string file=s.file;auto slash=file.find_last_of("/\\");if(slash!=std::string::npos)file=file.substr(slash+1);
   text(150,320,file.empty()?"Checking metadata and preparing the next step":file,25,ink,960);
   text(150,365,s.phase=="Deleting selected backup"?"Selected backup: bytes removed":"Current file: each copy and verification is a separate phase",21,muted,960);
   rect(150,421,960,22,dim,8);
   if(s.total){double ratio=double(std::min(s.done,s.total))/double(s.total);rect(150,421,int(960*ratio),22,amber,8);char label[140];snprintf(label,sizeof(label),"%.1f%%    %.2f / %.2f MiB",ratio*100,double(s.done)/1048576,double(s.total)/1048576);text(150,463,label,27,ink,960);}
   else text(150,463,"Preparing / no byte total available",25,muted,960);
  }else{int y=310;for(const auto&line:s.details){text(150,y,line,24,ink,960);y+=43;if(y>520)break;}}
  text(150,547,s.message,22,muted,960);text(40,666,s.busy?"Please keep the app open.  B  Cancel safely":s.confirm?"+  Confirm       B  Go back":"A  Return to menu       B  Exit",23,ink);return;
 }
 if(s.tab==2){size_t start=(s.game/6)*6;for(size_t n=start;n<std::min(start+6,s.games.size());n++){int col=(n-start)%3,row=(n-start)/3,x=40+col*404,y=135+row*246;bool active=n==s.game;rect(x,y,384,226,active?amber:panel,14);rect(x+3,y+3,378,220,panel,12);const auto&g=s.games[n];icon(x+17,y+17,125,g.icon,g.title);text(x+159,y+22,g.installed?"Installed":"Backup only",20,active?amber:muted,205);text(x+159,y+58,std::to_string(g.backups)+" complete backups",19,muted,205);text(x+17,y+154,g.title,27,ink,348);text(x+17,y+190,g.id,18,muted,348);}text(40,666,"D-pad  Browse    ZL  Connections    A  Open restore       L / R  Tabs       B  Exit",23,muted);return;}
 rect(38,129,365,496,panel,16);size_t first=s.game>2?s.game-2:0;if(first+5>s.games.size())first=s.games.size()>5?s.games.size()-5:0;
 for(size_t n=first;n<std::min(first+5,s.games.size());n++){int y=143+(n-first)*93;bool selected=n==s.game;rect(49,y,343,83,selected?(s.focus<0?amber:rgb(61,51,37)):panel,12);const auto&g=s.games[n];icon(60,y+10,62,g.icon,g.title);text(136,y+12,g.title,25,selected&&s.focus<0?bg:ink,242);text(136,y+46,s.tab==1?std::to_string(g.backups)+" backups":g.cartridge?"INSERTED CARTRIDGE":"Installed",18,selected&&s.focus<0?bg:muted,235);}
 icon(437,137,86,game.icon,game.title);text(545,137,game.title,34,ink,680);text(546,179,game.cartridge?"Inserted cartridge - keep inserted while copying":game.installed?"Installed on this console":"Software not installed",22,muted,660);
 if(s.tab==3){
  text(450,251,"Matched base ExeFS + RomFS",31,amber,760);
  text(450,305,"Includes main, main.npdm, executable modules and game data.",23,ink,750);
  text(450,350,"Both filesystems come from the same base program archive.",23,muted,750);
  text(450,392,"Installed updates are excluded. Checksums are verified on SD.",23,muted,750);
  rect(437,451,802,62,amber,12);text(460,466,"A   Back up to "+s.storageLabel,29,bg,750);
  rect(437,530,802,62,card,12);text(460,545,"X   Open web interface for PC downloads",27,ink,750);
  text(40,640,s.message,19,muted,1200);text(40,677,"D-pad Choose game    - Storage    L/R Tabs    ZL Connections    B Exit",23,muted);return;
 }
 rect(437,238,802,47,s.focus==0?rgb(61,51,37):panel,10);text(452,246,s.tab==0?"Back up to "+s.storageLabel:s.snapshot,22,ink,765);
 const char*labels[]={"Base game","Update","Save data"};for(int i=0;i<3;i++){int y=294+i*61;unsigned bit=1u<<i;bool enabled=s.available&bit,chosen=s.selected&bit;rect(437,y,802,57,s.focus==i+1?rgb(61,51,37):panel,11);if(s.focus==i+1)rect(437,y,4,57,amber,2);rect(455,y+15,26,26,chosen?amber:dim,5);if(chosen)text(460,y+9,"x",26,bg,25);text(499,y+9,labels[i],29,enabled?ink:dim,300);text(888,y+15,enabled?(chosen?"Selected":"Not selected"):"Unavailable",21,enabled?muted:dim,325);}
 rect(437,481,802,40,s.focus==4?rgb(61,51,37):panel,9);text(454,486,(s.tab==1?"Source: ":"User: ")+s.user,22,ink,740);
 if(s.tab==1){rect(437,527,802,40,s.focus==5?rgb(61,51,37):panel,9);text(454,532,"Destination: "+s.destination,22,ink,740);}
 int action=s.tab==1?6:5;rect(437,576,802,49,s.focus==action?amber:rgb(61,51,37),12);text(466,583,s.tab==0?"Back up selected":"Review restore",27,s.focus==action?bg:ink,750);
 text(40,640,s.message,18,muted,1200);text(40,677,"A Select   L/R Tabs   + Refresh   Y Backup   X User   - Storage   ZL Connect   ZR Delete   B Exit",21,muted,1200);
}
