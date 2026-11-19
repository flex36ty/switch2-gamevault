#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>

// Read-only backup selection is independent of the stricter HOME restore contract.
// Extra records are retained in the source snapshot for the final stability check.
template<class Row>std::vector<Row> selectBackupHomeRows(const std::vector<Row>&all,unsigned selected){
 if(!selected||(selected&~3u))throw std::runtime_error("Select base/update software for backup");
 std::vector<Row> result;unsigned seen=0;
 for(const auto&r:all){unsigned bit=r.key.type==128?1:r.key.type==129?2:0;
  if(!(selected&bit))continue;
  if(seen&bit)throw std::runtime_error("Multiple HOME records match the selected component; source is ambiguous");
  seen|=bit;result.push_back(r);
 }
 if(seen!=selected)throw std::runtime_error("Selected software component is unavailable");
 std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.key.type<b.key.type;});return result;
}
template<class Row>std::vector<Row> mergeHomeRows(const std::vector<Row>&current,const std::vector<Row>&chosen){
 auto validate=[](const std::vector<Row>&rows){unsigned seen=0;if(rows.size()>2)throw std::runtime_error("Unsupported HOME component count");for(const auto&r:rows){unsigned bit=r.key.type==128?1:r.key.type==129?2:0;if(!bit||(seen&bit))throw std::runtime_error("Unknown or duplicate HOME component");seen|=bit;}};
 validate(current);validate(chosen);auto result=current;
 for(const auto&r:chosen){auto at=std::find_if(result.begin(),result.end(),[&](const auto&x){return x.key.type==r.key.type;});if(at==result.end())result.push_back(r);else{if(std::memcmp(&at->key,&r.key,sizeof(r.key)))throw std::runtime_error("A different version is installed; version replacement is not supported");*at=r;}}
 std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.key.type<b.key.type;});if(result.empty()||result.front().key.type!=128)throw std::runtime_error("Restore the base game before restoring this update");return result;
}
