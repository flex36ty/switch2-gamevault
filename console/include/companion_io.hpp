#pragma once
#include <cstddef>
#include <cstdint>
#include <cerrno>
namespace vaultio {
enum class Status {Complete,Closed,Failed,TimedOut};
struct Attempt {int count;int error;};
struct Result {Status status;int error;size_t done;};
// Socket calls must be nonblocking. Deadline measures inactivity, not total transfer time.
template<class Call,class Pause,class Clock>
Result transfer(size_t size,Call call,Pause pause,Clock now,uint64_t timeout){
 size_t done=0;auto last=now();
 while(done<size){
  auto r=call(done,size-done);
  if(r.count>0){if(size_t(r.count)>size-done)return {Status::Failed,EINVAL,done};done+=r.count;last=now();continue;}
  if(r.count==0)return {Status::Closed,0,done};
  if(r.error!=EAGAIN&&r.error!=EWOULDBLOCK&&r.error!=EINTR)return {Status::Failed,r.error,done};
  if(now()-last>=timeout)return {Status::TimedOut,r.error,done};
  pause();
 }
 return {Status::Complete,0,done};
}
}
