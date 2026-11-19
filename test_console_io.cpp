#include "console/include/companion_io.hpp"
#include <cassert>
#include <string>
#include <vector>
using namespace vaultio;
int main(){
 uint64_t clock=0;int calls=0;std::string source="PAIR\t1234\n",received;
 auto r=transfer(source.size(),[&](size_t off,size_t left){
  calls++;if(calls==1)return Attempt{-1,EAGAIN};if(calls==2)return Attempt{-1,EINTR};
  if(calls==4)return Attempt{-1,EWOULDBLOCK};
  size_t n=left>3?3:left;received+=source.substr(off,n);return Attempt{int(n),0};
 },[&]{clock++;},[&]{return clock;},10);
 assert(r.status==Status::Complete&&received==source&&r.done==source.size());
 clock=0;r=transfer(1,[](size_t,size_t){return Attempt{-1,EAGAIN};},[&]{clock++;},[&]{return clock;},5);
 assert(r.status==Status::TimedOut&&clock==5&&r.done==0);
 r=transfer(1,[](size_t,size_t){return Attempt{0,0};},[]{},[]{return uint64_t(0);},5);assert(r.status==Status::Closed);
 r=transfer(1,[](size_t,size_t){return Attempt{-1,ECONNRESET};},[]{},[]{return uint64_t(0);},5);assert(r.status==Status::Failed&&r.error==ECONNRESET);
 // Continuous progress may exceed one timeout window in total.
 clock=0;r=transfer(20,[&](size_t,size_t){clock+=4;return Attempt{1,0};},[]{},[&]{return clock;},5);
 assert(r.status==Status::Complete&&clock==80);
}
