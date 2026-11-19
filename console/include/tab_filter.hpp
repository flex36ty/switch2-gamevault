#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>

template<class Tiles>
std::vector<size_t> tabItems(const Tiles& games, int tab) {
    std::vector<size_t> out;
    for (size_t i=0;i<games.size();++i)
        if (games[i].id!="0000000000000000" &&
            ((tab==0 || tab==3) ? games[i].installed : (tab==1 ? games[i].backups>0 : true)))
            out.push_back(i);
    if(tab==0)std::stable_partition(out.begin(),out.end(),[&](size_t i){return games[i].cartridge;});
    return out;
}
