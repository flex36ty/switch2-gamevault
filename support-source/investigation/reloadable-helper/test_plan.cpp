#include "include/plan.hpp"
#include <cassert>
#include <cstdio>
#include <string>
int main(){
 plan::Program p{};auto valid=[&](const std::string& s){return plan::parse(s.data(),s.size(),p);};
 const std::string a="KAIHEN_PLAN 1\nLOG first\nSD_PROBE\nEND\n";
 const std::string b="KAIHEN_PLAN 1\r\n# comment\r\nDELAY 30000\r\nCLOSE\r\nEND\r\n";
 assert(valid(a)&&p.count==2);assert(valid(b)&&p.instructions[0].value==30000);
 assert(!valid(""));assert(!valid("KAIHEN_PLAN 2\nCLOSE\nEND\n"));
 assert(!valid("KAIHEN_PLAN 1\nCLOSE\n"));assert(!valid("KAIHEN_PLAN 1\nEND\n"));
 assert(!valid("KAIHEN_PLAN 1\nCLOSE\nEND\nCLOSE\n"));
 assert(!valid("KAIHEN_PLAN 1\nINSTALL\nEND\n"));
 for(auto n:{"-1","60001","999999999999999999999999999999","1x",""})assert(!valid(std::string("KAIHEN_PLAN 1\nDELAY ")+n+"\nEND\n"));
 assert(valid("KAIHEN_PLAN 1\nDELAY 60000\nDELAY 60000\nEND\n"));
 assert(!valid("KAIHEN_PLAN 1\nDELAY 60000\nDELAY 60000\nDELAY 1\nEND\n"));
 std::string many="KAIHEN_PLAN 1\n";for(int i=0;i<32;i++)many+="CLOSE\n";assert(valid(many+"END\n"));assert(!valid(many+"CLOSE\nEND\n"));
 std::string nul=a;nul[15]=0;assert(!valid(nul));assert(!valid(a+std::string(4096,'#')));
 for(size_t i=0;i<a.find("END")+3;i++)assert(!valid(a.substr(0,i)));
 plan::Store store;assert(!store.rollback(false));assert(store.reload(a.data(),a.size(),false));
 assert(!store.reload(b.data(),b.size(),true));assert(store.generation==1&&store.active.instructions[0].op==plan::Log);
 assert(!store.reload("broken",6,false));assert(store.generation==1);
 assert(store.reload(b.data(),b.size(),false));assert(store.generation==2);
 assert(!store.rollback(true));assert(store.rollback(false));assert(store.active.instructions[0].op==plan::Log);
 assert(store.rollback(false));assert(store.active.instructions[0].op==plan::Delay);
 puts("PASS: parser bounds, upload truncation, invalid input, busy reload, preserved active plan, rollback");
}
