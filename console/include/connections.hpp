#pragma once
#include <functional>
#include <string>
using ConnectionDisplay=std::function<void(const std::string&,const std::string&)>;
void vaultFtp(const ConnectionDisplay& display,const std::function<void()>& poll={});
void vaultMtp(const ConnectionDisplay& display);
