#pragma once
// Account saves may change user, never game/type/rank/index. Device saves
// retain their exact identity. Numeric values match the SDK save enums.
template<class A> bool compatibleSaveDestination(const A& source,const A& target){
 if(!source.application_id||source.application_id!=target.application_id||
    source.system_save_data_id||target.system_save_data_id||
    source.save_data_rank||target.save_data_rank||
    source.save_data_type!=target.save_data_type||source.save_data_index!=target.save_data_index)return false;
 if(source.save_data_type==1)return (source.uid.uid[0]||source.uid.uid[1])&&(target.uid.uid[0]||target.uid.uid[1]);
 return source.save_data_type==2&&source.uid.uid[0]==target.uid.uid[0]&&source.uid.uid[1]==target.uid.uid[1];
}
