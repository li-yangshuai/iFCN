#include"mapping.h"

namespace fcngraph{
    //门级->元胞级坐标映射
std::map<std::pair<position, position>, std::vector<std::vector<position>>> Mapping::mapping_line(std::vector<std::vector<position>>& _example){
    
    for (auto &oneroute : _example)//此处是头文件中的存放的坐标形式的多条线路，含有起始点std::vector<std::vector<position>> routepos_list;
    {
        routepos_Deviate(oneroute);
    }
    deviate_mapping(deviate_list);
    
    crossline_mapping(_example);

    return deviatemapping_list;
}

//给予门级线路偏移量
void Mapping::routepos_Deviate(std::vector<position>& _oneroutepos_list){
    std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>> RouteDeviate_list;
    std::vector<std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>>> temp_list_vector;
    std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>> temp_list;

    std::string XMIDDLE("XMIDDLE"), XSIDE("XSIDE"), YMIDDLE("YMIDDLE"), YSIDE("YSIDE"), XMYM("XMYM"), XMYS("XMYS"), XSYM("XSYM"), XSYS("XSYS"), EMPTY("EMPTY");  
    bool isFirst = true; 
    bool isFanout = false;
    int tempdirection = 0; 
    std::pair<unsigned int, unsigned int> startpos = _oneroutepos_list.front();
    std::pair<unsigned int, unsigned int> endpos = _oneroutepos_list.back();
    
    
    if(_oneroutepos_list.size() == 2)
    {
        deviate_list.insert({{startpos, endpos}, {}});
    }
    else
    {
        for (auto it = _oneroutepos_list.begin(); it != _oneroutepos_list.end(); ++it)
        {
            if (isFirst)
            {
                //判断是否多扇出
                temp_list_vector.clear();
                if (!deviate_list.empty())
                {
                    for ( auto& pair : deviate_list) 
                    {  
                        if (pair.first.first == startpos) 
                        {  
                            position tempsecondpos = _oneroutepos_list[1];
                            if(!pair.second.empty())
                            {
                                if (tempsecondpos == (pair.second.front()).first)
                                {
                                    auto temp = pair.second;
                                    temp_list_vector.emplace_back(temp);
                                    isFanout = true;
                                }
                            }
                        }  
                    } 
                    //多条线路复用，后面线路与前面多条线路比较，追踪复用坐标最多线路
                    int maxrepeat = 0;
                    if (!temp_list_vector.empty())
                    {
                        for (auto &v : temp_list_vector)
                        {
                            int temprepeat = 0;
                            for (size_t i = 0; i < v.size(); ++i)
                            {
                                if (v[i].first == _oneroutepos_list[i+1])
                                {
                                    temprepeat++;
                                }
                                else
                                {
                                    break;
                                }
                            }
                            if (temprepeat >= maxrepeat)
                            {
                                maxrepeat = temprepeat;
                                temp_list = v;
                            }
                        }
                    }
                }
                if (isFanout)
                {
                    for (size_t i = 0; i < temp_list.size(); ++i) 
                    {  
                        if (_oneroutepos_list[i+1] == temp_list[i].first) 
                        {  
                            if (i != (temp_list.size()-1))
                            {
                                RouteDeviate_list.push_back(temp_list[i]);
                            }
                            else
                            {
                                RouteDeviate_list.push_back(temp_list[i]);
                                std::pair<unsigned int, unsigned int> fanoutpos = temp_list[i].first;
                                if (!RouteDeviate_list.empty()){
                                    if ((RouteDeviate_list.back().second == XMIDDLE) || (RouteDeviate_list.back().second == XSIDE))
                                    {
                                        tempdirection = 1;
                                    }
                                    else if ((RouteDeviate_list.back().second == YMIDDLE) || (RouteDeviate_list.back().second == YSIDE))
                                    {
                                        tempdirection = 0;
                                    }
                                    else 
                                    {
                                        if (_oneroutepos_list[i+1].second == _oneroutepos_list[i].second)//沿X轴
                                        {
                                            RouteDeviate_list.pop_back();
                                            if ((temp_list[i].second == XMYM) || (temp_list[i].second == XMYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], XMIDDLE);
                                                tempdirection = 1;
                                            }
                                            else if ((temp_list[i].second == XSYM) || (temp_list[i].second == XSYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], XSIDE);
                                                tempdirection = 1;
                                            }
                                        }
                                        else
                                        {
                                            RouteDeviate_list.pop_back();
                                            if ((temp_list[i].second == XMYM) || (temp_list[i].second == XSYM))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], YMIDDLE);
                                                tempdirection = 0;
                                            }
                                            else if ((temp_list[i].second == XMYS) && (temp_list[i].second == XSYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], YSIDE);
                                                tempdirection = 0;
                                            }
                                        }
                                        
                                    }
                                }
                                it = std::find(_oneroutepos_list.begin(), _oneroutepos_list.end(), fanoutpos); 
                                break;
                            }
                        
                            //RouteDeviate_list.push_back(temp_list[i]);
                        } 
                        else
                        {
                            std::pair<unsigned int, unsigned int> fanoutpos = temp_list[i-1].first;
                            if (!RouteDeviate_list.empty()){
                                if ((RouteDeviate_list.back().second == XMIDDLE) || (RouteDeviate_list.back().second == XSIDE))
                                {
                                    tempdirection = 1;
                                }
                                else if ((RouteDeviate_list.back().second == YMIDDLE) || (RouteDeviate_list.back().second == YSIDE))
                                {
                                    tempdirection = 0;
                                }
                                else 
                                {
                                    if ((!_oneroutepos_list.empty()) && (i > 0) && (_oneroutepos_list[i].second == _oneroutepos_list[i-1].second))//沿X轴
                                    {
                                        RouteDeviate_list.pop_back();
                                        if ((temp_list[i-1].second == XMYM) || (temp_list[i-1].second == XMYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], XMIDDLE);
                                            tempdirection = 1;
                                        }
                                        else if ((temp_list[i-1].second == XSYM) || (temp_list[i-1].second == XSYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], XSIDE);
                                            tempdirection = 1;
                                        }
                                    }
                                    else
                                    {
                                        RouteDeviate_list.pop_back();
                                        if ((temp_list[i-1].second == XMYM) || (temp_list[i-1].second == XSYM))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], YMIDDLE);
                                            tempdirection = 0;
                                        }
                                        else if ((temp_list[i-1].second == XMYS) && (temp_list[i-1].second == XSYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], YSIDE);
                                            tempdirection = 0;
                                        }
                                    }
                                    
                                }
                            }
                            it = std::find(_oneroutepos_list.begin(), _oneroutepos_list.end(), fanoutpos); 
                            break;
                        } 
                    }

                }
                else
                {
                    auto next_it = std::next(it);  
                    const auto& next_cell = *next_it; 
                    if (next_cell.second == (*it).second)
                    {
                        tempdirection = 1; //初始沿X轴出边
                    }
                    else
                    {
                        tempdirection = 0; //初始沿Y轴出边
                    }
                }
                
                isFirst = false;

            }
            else if (it != _oneroutepos_list.begin())
            {
                auto prev_it = std::prev(it);  
                auto& prev_cell = *prev_it; 
                int judging_direction;

                if (((*it).first != prev_cell.first) && ((*it).second == prev_cell.second))
                {
                    judging_direction = 1;
                }
                else
                {
                    judging_direction = 0;
                }
                
                switch (judging_direction)
                {
                //沿X轴方向
                case 1:
                    if (tempdirection == 1)
                    {
                        if (prev_it == _oneroutepos_list.begin())//起点第一条边的放置
                        {
                            if ((findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                            {
                                RouteDeviate_list.emplace_back(*it, XSIDE);
                            }
                            else
                            {
                                RouteDeviate_list.emplace_back(*it, XMIDDLE);
                            }
                        }
                        else//后一条边尽量同步前一条的位置
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, *it) == XSIDE)||(findInVectorPairFirst(deviate_list, *it) == XSYM)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                                    {
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                }
                                else
                                {
                                    if ((findInVectorPairFirst(deviate_list, *it) == XMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                                    {
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                    
                                }
                            } 
                        }

                        tempdirection = 1;
                    }
                    //Y转X的拐点
                    else if (tempdirection == 0)
                    {
                        if ((findInVectorPairFirst(deviate_list, *it) == XMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XSIDE);
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XSIDE);
                                }
                            }
                        }
                        else if ((findInVectorPairFirst(deviate_list, *it) == XSIDE)||(findInVectorPairFirst(deviate_list, *it) == XSYM)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                }
                            }
                        }
                        else 
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                
                                }
                            }
                        }

                        tempdirection = 1;
                    }
                    
                    break;
                
                //沿Y轴方向
                case 0:
                    if (tempdirection == 0)
                    {
                        if (prev_it == _oneroutepos_list.begin())//起点第一条边的放置
                        {
                            if ((findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                            {
                                RouteDeviate_list.emplace_back(*it, YSIDE);
                            }
                            else
                            {
                                RouteDeviate_list.emplace_back(*it, YMIDDLE);
                            }
                        }
                        else//后一条边尽量同步前一条的位置
                        {
                            if (RouteDeviate_list.back().second == YSIDE)
                            {
                                if ((findInVectorPairFirst(deviate_list, *it) == YSIDE)||(findInVectorPairFirst(deviate_list, *it) == XMYS)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                                {
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                else
                                {
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                            }
                            else
                            {
                                if ((findInVectorPairFirst(deviate_list, *it) == YMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                                {
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                                else
                                {
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                
                            }
                            
                        }

                        tempdirection = 0;
                    }
                    //X转Y的拐点
                    else if (tempdirection == 1)
                    {
                        if ((findInVectorPairFirst(deviate_list, *it) == YMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                            }
                        }
                        else if ((findInVectorPairFirst(deviate_list, *it) == YSIDE)||(findInVectorPairFirst(deviate_list, *it) == XMYS)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                            }
                        }
                        else 
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                        RouteDeviate_list.emplace_back(*it, YSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                        RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                    }
                                
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                        RouteDeviate_list.emplace_back(*it, YSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                        RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                    }
                                
                                }
                            }
                        }

                        tempdirection = 0;
                    }
                    break;

                default:
                    break;
                }
            }
            
            
        }
        RouteDeviate_list.pop_back();
        deviate_list.insert({{startpos, endpos}, RouteDeviate_list});
    }
}

//通过门级线路偏移量进行具体元胞映射
void Mapping::deviate_mapping(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list){
    std::string XMIDDLE("XMIDDLE"), XSIDE("XSIDE"), YMIDDLE("YMIDDLE"), YSIDE("YSIDE"), XMYM("XMYM"), XMYS("XMYS"), XSYM("XSYM"), XSYS("XSYS"), EMPTY("EMPYT");
    //std::vector<std::vector<position>> route_mapping;

    if(!_deviate_list.empty())
    {
        for (const auto& route : _deviate_list)
        {
            auto startpos = route.first.first;
            auto endpos = route.first.second;
            std::vector<std::pair<position, std::string>> single_route;
            single_route = route.second;
            std::vector<std::vector<position>> route_mapping;

            for (auto it = single_route.begin(); it != single_route.end(); ++it)
            {
                std::vector<position> unit_mapping;
                auto itpos = (*it).first;
                std::pair<unsigned int, unsigned int> nwpos = itpos;
                nwpos.first *= 5;
                nwpos.second *= 5;
                auto nextpos = (*(std::next(it))).first;
                auto prevpos = (*(std::prev(it))).first;

                if (it == single_route.begin())
                {
                    if (single_route.size() == 1)
                    {
                        switch (((*it).first.first != startpos.first) && ((*it).first.second == startpos.second))
                        {
                        case true://左右
                            if (itpos.first > startpos.first)//右
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            else//左
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            
                            break;
                        
                        case false://上下
                            if (itpos.second > startpos.second)//下
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            else//上
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            
                            break;
                        
                        default:
                            break;
                        }
                    }
                    else
                    {
                        switch (((*it).first.first != startpos.first) && ((*it).first.second == startpos.second))
                        {
                        case true://左右
                            if (itpos.first > startpos.first)//右
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second ==XSYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XMYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                        {
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                
                            }
                            else//左
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second ==XSYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        }
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                    
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XMYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                            }
                            
                            break;
                        
                        case false://上下
                            if (itpos.second > startpos.second)//下
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second ==XSYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XSYM))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            else//上
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second ==XSYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        }
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                    
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XSYM))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYS)//情况特殊几乎不存在
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            
                            break;
                        
                        default:
                            break;
                        }
                    }
                }
                else if (std::next(it) == single_route.end())
                {
                    switch (((*it).first.first != endpos.first) && ((*it).first.second == endpos.second))
                    {
                    case true://左右
                        if (itpos.first > endpos.first)//右
                        {
                            if ((*it).second == XMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                            }
                            else if((*it).second == XSIDE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                            }
                            else if((*it).second == XMYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }   
                            }
                            else if((*it).second == XMYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                        }
                        else//左
                        {
                            if ((*it).second == XMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                            }
                            
                            else if((*it).second == XSIDE)
                            {
                                if (isfindpostype(deviate_list, (*it).first, YSIDE)||isfindpostype(deviate_list, (*it).first, XMYS)||isfindpostype(deviate_list, (*it).first, XSYS))
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            
                            }
                            else if((*it).second == XMYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                
                            }
                            else if((*it).second == XMYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYM)//此情况不太可能
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                        }
                        break;
                    
                    case false://上下
                        if (itpos.second > endpos.second)//下
                        {
                            if ((*it).second == YMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            else if ((*it).second == YSIDE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            else if ((*it).second == XMYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XSYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XMYS)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XSYS)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                        }
                        else//上
                        {
                            if ((*it).second == YMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            else if ((*it).second == YSIDE)
                            {
                                if (isfindpostype(deviate_list, (*it).first, XSIDE)||isfindpostype(deviate_list, (*it).first, XSYM)||isfindpostype(deviate_list, (*it).first, XSYS))
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XMYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if((*it).second == XSYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XMYS)//特殊情况
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XSYS)//特殊情况
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                        }
                        break;
                    
                    default:
                        break;
                    }
                }
                else
                {
                    if (((*it).second == XMYM)||((*it).second == XMYS)||((*it).second == XSYM)||((*it).second == XSYS))
                    {
                        if (((prevpos.first<itpos.first)&&(nextpos.second<itpos.second))||((prevpos.second<itpos.second)&&(nextpos.first<itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                        }
                        else if (((prevpos.first>itpos.first)&&(nextpos.second<itpos.second))||((prevpos.second<itpos.second)&&(nextpos.first>itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                        }
                        else if (((prevpos.first>itpos.first)&&(nextpos.second>itpos.second))||((prevpos.second>itpos.second)&&(nextpos.first>itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        else
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        
                    }
                    else if(((*it).second == XMIDDLE)||((*it).second == XSIDE)||((*it).second == YMIDDLE)||((*it).second == YSIDE))
                    {
                        if ((*it).second == XMIDDLE)
                        {
                            if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second == XSYS))
                            {
                                if (nextpos.first > itpos.first)
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                            }
                            
                        }
                        
                        else if ((*it).second == XSIDE)
                        {
                            if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second == XMYS))
                            {
                                if (nextpos.first > itpos.first)
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        
                        else if ((*it).second == YMIDDLE)
                        {
                            if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second == XSYS))
                            {
                                if (nextpos.second > itpos.second)
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                        }
                        
                        else if ((*it).second == YSIDE)
                        {
                            if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second == XSYM))
                            {
                                if (nextpos.second > itpos.second)
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                    }
                }
                route_mapping.push_back(unit_mapping);
            }
            if(!route_mapping.empty()){
                deviatemapping_list.insert({{startpos, endpos}, route_mapping});
            }
            else
            {
                deviatemapping_list.insert({{startpos, endpos}, {}});
            }
        }
    }
    
}

//在布线时查找deviate_list里是否在该位置已经布线
std::string Mapping::findInVectorPairFirst(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair){  
    if (!_deviate_list.empty())
    {
        for (const auto& list : _deviate_list) 
        {  
            for (const auto& vec_it : list.second) 
            {  
                if (vec_it.first == target_pair) 
                {  
                    return vec_it.second; // 找到了  
                }  
            }  
        } 
    } 
    std::string EMPTY("EMPYT");
    return EMPTY; // 没有找到  
}  

//查找deviate_list里指定坐标位置是否具有指定偏移态
bool Mapping::isfindpostype(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair, std::string& _type){
    if(!_deviate_list.empty())
    {
        for (const auto& list : _deviate_list)
        {
            for (const auto& vec_it : list.second)
            {
                if (vec_it.first == target_pair) 
                {  
                    if (vec_it.second == _type)
                    {
                        return true;
                    }
                    
                } 
            }
            
        }
    }
    return false;
}

//找出交叉线映射元胞坐标
void Mapping::crossline_mapping(std::vector<std::vector<position>> &_routepos_list){
    std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list;
    std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list;
    //std::vector<position> existpos_list;

    // for (auto it = _routepos_list.begin(); it != _routepos_list.end(); it++)
    // {
    //     //检查单条线自身是否有交叉情况
    //     std::vector<position> oneroute = (*it);
    //     for (size_t i = 0; i < oneroute.size(); ++i) 
    //     {  
    //         for (size_t j = i + 1; j < oneroute.size(); ++j) 
    //         {  
    //             if (oneroute[i] == oneroute[j]) 
    //             {  
    //                 std::vector<position> existpos_list;
    //                 if(!temppos_list.empty())
    //                 {
    //                     for (auto &v: temppos_list)
    //                     {
    //                         existpos_list.push_back(v.second);
    //                     }
    //                 }
    //                 auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), oneroute[i]); 
    //                 if (exitpos == existpos_list.end())
    //                 {
    //                     oneroutepos_list.emplace_back(std::make_pair(std::make_pair((*it).front(), (*it).back()), oneroute[i]));
    //                 }
    //             }  
    //         }  
    //     }   

    //     //不同线之间交叉情况
    //     if (std::next(it) != _routepos_list.end())
    //     {
    //         for (auto temp = std::next(it); temp != _routepos_list.end(); temp++)
    //         {
    //             //同起点扇出前部分不交叉
    //             if ((*it).front() == (*temp).front())
    //             {
    //                 int i = 0;
    //                 while (i < (*it).size() && i < (*temp).size() && (*it)[i] == (*temp)[i])
    //                 {
    //                     ++i;
    //                 }
    //                 if (i != (*it).size() && i != (*temp).size())
    //                 {
    //                     std::vector<position> itpart((*it).begin()+i, (*it).end());
    //                     std::vector<position> temppart((*temp).begin()+i, (*temp).end());
    //                     if(!temppart.empty())
    //                     {
    //                         for (auto &pos : temppart)
    //                         {
    //                             auto temppos = std::find(itpart.begin(), itpart.end(), pos);
                                
    //                             std::vector<position> existpos_list;
    //                             if(!temppos_list.empty())
    //                             {
    //                                 for (auto &v: temppos_list)
    //                                 {
    //                                     existpos_list.push_back(v.second);
    //                                 }
    //                             }
    //                             auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos); 

    //                             if (temppos != (*it).end() && (*temppos) != (*it).back() && (*temppos) != (*temp).back() && exitpos == existpos_list.end())
    //                             {
    //                                 temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), pos));
    //                             }
    //                         }
                            
    //                     }
    //                     // for (i; i < (*it).size() && i < (*temp).size(); i++)
    //                     // {
    //                     //     if ((*it)[i] == (*temp)[i])
    //                     //     {
    //                     //         temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), (*temp)[i]));
    //                     //     }
                            
    //                     // }
                        
    //                 }
                    
    //             }
    //             else
    //             {
    //                 for (auto &pos : (*temp))
    //                 {
    //                     auto temppos = std::find((*it).begin(), (*it).end(), pos);

    //                     //避免重复检查其他线与扇出前的重合线导致重复输出交叉点
    //                     std::vector<position> existpos_list;
    //                     if(!temppos_list.empty())
    //                     {
    //                         for (auto &v: temppos_list)
    //                         {
    //                             existpos_list.push_back(v.second);
    //                         }
    //                     }
    //                     auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos); 

    //                     if (temppos != (*it).end() && (*temppos) != (*it).back() && (*temppos) != (*temp).back() && exitpos == existpos_list.end())
    //                     {
    //                         temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), pos));
    //                     }
                        
    //                 }
                    
    //             }
    //         }
    //     }
    //     else
    //     {
    //         break;
    //     }
    // }

    for (size_t i = 0; i < _routepos_list.size(); i++)
    {
        std::vector<position> oneroute = _routepos_list[i];
        for (size_t i1 = 0; i1 < oneroute.size(); i1++) 
        {  
            for (size_t j1 = i1 + 1; j1 < oneroute.size(); j1++) 
            {  
                if (oneroute[i1] == oneroute[j1]) 
                {  
                    std::vector<position> existpos_list;
                    if(!temppos_list.empty())
                    {
                        for (auto &v: temppos_list)
                        {
                            existpos_list.push_back(v.second);
                        }
                    }
                    auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), oneroute[i1]); 
                    if (exitpos == existpos_list.end())
                    {
                        oneroutepos_list.emplace_back(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), oneroute[i1]));
                    }
                }  
            }  
        }
        for (size_t j = i+1; j < _routepos_list.size(); j++)
        {
            if (_routepos_list[i].front() == _routepos_list[j].front())
            {
                int i1 = 0;
                while (i1 < _routepos_list[i].size() && i1 < _routepos_list[j].size() && _routepos_list[i][i1] == _routepos_list[j][i1])
                {
                    ++i1;
                }
                if ((i1 <= _routepos_list[i].size()) && (i1 <= _routepos_list[j].size()))
                {
                    std::vector<position> itpart(_routepos_list[i].begin()+i1, _routepos_list[i].end());
                    std::vector<position> temppart(_routepos_list[j].begin()+i1, _routepos_list[j].end());
                    if(!temppart.empty())
                    {
                        for (auto &pos1 : temppart)
                        {
                            auto temppos = std::find(itpart.begin(), itpart.end(), pos1);
                            
                            std::vector<position> existpos_list;
                            if(!temppos_list.empty())
                            {
                                for (auto &v: temppos_list)
                                {
                                    existpos_list.push_back(v.second);
                                }
                            }
                            auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos1); 

                            if (temppos != itpart.end() && (*temppos) != itpart.back() && (*temppos) != temppart.back() && exitpos == existpos_list.end())
                            {
                                temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), std::make_pair(_routepos_list[j].front(), _routepos_list[j].back()))), pos1));
                            }
                        }
                        
                    }
                    
                }
                
            }
            else
            {
                for (auto &pos2 : _routepos_list[j])
                {
                    auto temppos = std::find(_routepos_list[i].begin(), _routepos_list[i].end(), pos2);

                    //避免重复检查其他线与扇出前的重合线导致重复输出交叉点
                    std::vector<position> existpos_list;
                    if(!temppos_list.empty())
                    {
                        for (auto &v: temppos_list)
                        {
                            existpos_list.push_back(v.second);
                        }
                    }
                    auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos2); 

                    if (temppos != _routepos_list[i].end() && (*temppos) != _routepos_list[i].back() && (*temppos) != _routepos_list[j].back() && exitpos == existpos_list.end())
                    {
                        temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), std::make_pair(_routepos_list[j].front(), _routepos_list[j].back()))), pos2));
                    }
                    
                }
                
            }
        }
        
    }
    //temppos_list_examp = temppos_list;
    
    //输出元胞级映射的交叉线具体坐标（5×5结构）
    std::vector<position> allcrosspos;
    if (!oneroutepos_list.empty())
    {
        for (auto &tempcross : oneroutepos_list)
        {
            allcrosspos.push_back(tempcross.second);
        }
    }
    if (!temppos_list.empty())
    {
        for (auto &tempcross : temppos_list)
        {
            allcrosspos.push_back(tempcross.second);
        }
    }
    

    for (auto it = oneroutepos_list.begin(); it != oneroutepos_list.end(); it++)
    {
        auto pospair = (*it).first;
        auto crosspos = (*it).second;
        auto vec = deviate_list[pospair];
        auto mapping = deviatemapping_list[pospair];
        std::vector<std::vector<position>> unitpos;
        std::vector<std::string> deviate;
        

        for (auto pos = vec.begin(); pos != vec.end(); pos++)
        {
            if ((*pos).first == crosspos)
            {
                size_t index = std::distance(vec.begin(), pos); 
                unitpos.push_back(mapping[index]);
                deviate.push_back((*pos).second);
            }
        }
        if((unitpos.size() == 2) && (deviate.size() == 2))
        {
            auto unitpos1 = unitpos[0];
            auto unitpos2 = unitpos[1];
            for (auto &temp_unitpos : unitpos1)
            {
                auto temp_it = std::find(unitpos2.begin(), unitpos2.end(), temp_unitpos);
                if (temp_it != unitpos2.end())
                {
                    position diru = {crosspos.first, crosspos.second - 1}; 
                    position dird = {crosspos.first, crosspos.second + 1}; 
                    position dirl = {crosspos.first - 1, crosspos.second}; 
                    position dirr = {crosspos.first + 1, crosspos.second}; 
                    if ((deviate[0] == "XSIDE") || (deviate[0] == "YSIDE") || (deviate[0] == "XSYS"))
                    {
                        if (((deviate[0] == "XSIDE")&&((deviate[1] == "YSIDE")||(deviate[1] == "XMYS")))
                        &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos2);  
                            }
                            break;
                        }
                        else if (((deviate[0] == "YSIDE")&&((deviate[1] == "XSIDE")||(deviate[1] == "XSYM")))
                        &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos2);  
                            }
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos1);  
                            }
                            break;
                        }    
                    }
                    else
                    {
                        if (crossline_list.find(pospair) == crossline_list.end()) 
                        {  
                            crossline_list[pospair] = {}; 
                            crossline_list[pospair].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair].push_back(unitpos2);  
                        }
                        break;
                    }
                }
            }
        }
    }
    
    for (auto it = temppos_list.begin(); it != temppos_list.end(); it++)
    {
        auto pospair1 = (*it).first.first;
        auto pospair2 = (*it).first.second;
        auto crosspos = (*it).second;

        auto vec1 = deviate_list[pospair1];
        auto vec2 = deviate_list[pospair2];
        auto mapping1 = deviatemapping_list[pospair1];
        auto mapping2 = deviatemapping_list[pospair2];
        std::vector<position> unitpos1;
        std::vector<position> unitpos2;
        std::string deviate1;
        std::string deviate2;

        std::vector<position> tempcrosscell;
        if (!crossline_list.empty())
        {
            for (auto &v : crossline_list)
            {
                auto tempv = v.second;
                for (auto &unit : tempv)
                {
                    tempcrosscell.insert(tempcrosscell.end(), unit.begin(), unit.end());
                }
            }
        }

        for (auto it1 = vec1.begin(); it1 != vec1.end(); it1++)
        {
            if ((*it1).first == crosspos)
            {
                size_t index1 = std::distance(vec1.begin(), it1); 
                unitpos1 = mapping1[index1];
                deviate1 = (*it1).second;
            }
            
        }

        for (auto it2 = vec2.begin(); it2 != vec2.end(); it2++)
        {
            if ((*it2).first == crosspos)
            {
                size_t index2 = std::distance(vec2.begin(), it2); 
                unitpos2 = mapping2[index2];
                deviate2 = (*it2).second;
            }
            
        }
        
        for (auto &unitpos : unitpos1)
        {
            auto it3 = std::find(unitpos2.begin(), unitpos2.end(), unitpos);
            if (it3 != unitpos2.end())
            {
                /*
                if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE") || (deviate1 == "XSYS"))
                {
                    if (crossline_list.find(pospair1) == crossline_list.end()) 
                    {  
                        crossline_list[pospair1] = {}; 
                        crossline_list[pospair1].push_back(unitpos1);  
                    } else 
                    {  
                        crossline_list[pospair1].push_back(unitpos1);  
                    }

                    break;
                }
                else
                {
                    if (crossline_list.find(pospair2) == crossline_list.end()) 
                    {  
                        crossline_list[pospair2] = {}; 
                        crossline_list[pospair2].push_back(unitpos2);  
                    } else 
                    {  
                        crossline_list[pospair2].push_back(unitpos2);  
                    }

                    break;
                }
                */
                
                if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE") || (deviate1 == "XSYS"))
                {
                    if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE")/*unitpos1.size() == 5*/)
                    {
                        position midpos;
                        if (deviate1 == "XSIDE")
                        {
                            midpos.first = (crosspos.first)*5+2;
                            midpos.second = (crosspos.second)*5+4;
                        }
                        else if(deviate1 == "YSIDE")
                        {
                            midpos.first = (crosspos.first)*5+4;
                            midpos.second = (crosspos.second)*5+2;
                        }
                        
                        //auto midpos = unitpos1[2]; //还有种情况：XSYS还需考虑unitpos1[6]
                        position dir1 = {midpos.first, midpos.second + 1}; 
                        position dir2 = {midpos.first, midpos.second - 1}; 
                        position dir3 = {midpos.first - 1, midpos.second}; 
                        position dir4 = {midpos.first + 1, midpos.second}; 
                        
                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir3) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir4) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                        
                            break;
                        }
                        else
                        {
                            position diru = {crosspos.first, crosspos.second - 1}; 
                            position dird = {crosspos.first, crosspos.second + 1}; 
                            position dirl = {crosspos.first - 1, crosspos.second}; 
                            position dirr = {crosspos.first + 1, crosspos.second}; 
                            if (((deviate1 == "XSIDE")&&((deviate2 == "YSIDE")||(deviate2 == "XMYS")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else if (((deviate1 == "YSIDE")&&((deviate2 == "XSIDE")||(deviate2 == "XSYM")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                        }
                            
                    }
                    else
                    {
                        position midpos1;
                        midpos1.first = (crosspos.first)*5+2;
                        midpos1.second = (crosspos.second)*5+4;
                        position midpos2;
                        midpos2.first = (crosspos.first)*5+4;
                        midpos2.second = (crosspos.second)*5+2;
                        position dir1 = {midpos1.first, midpos1.second + 1}; 
                        position dir2 = {midpos2.first + 1, midpos2.second}; 

                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                        
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                            break;
                        }
                    }
                }
                else if ((deviate2 == "XSIDE") || (deviate2 == "YSIDE") || (deviate2 == "XSYS"))
                {
                    
                    if ((deviate2 == "XSIDE") || (deviate2 == "YSIDE"))
                    {
                        position midpos;
                        if (deviate2 == "XSIDE")
                        {
                            midpos.first = (crosspos.first)*5+2;
                            midpos.second = (crosspos.second)*5+4;
                        }
                        else if(deviate2 == "YSIDE")
                        {
                            midpos.first = (crosspos.first)*5+4;
                            midpos.second = (crosspos.second)*5+2;
                        }
                        //auto midpos = unitpos2[2];
                        position dir1 = {midpos.first, midpos.second + 1}; 
                        position dir2 = {midpos.first, midpos.second - 1}; 
                        position dir3 = {midpos.first - 1, midpos.second}; 
                        position dir4 = {midpos.first + 1, midpos.second}; 
                        
                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir3) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir4) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                        
                            break;
                        }
                        else
                        {
                            position diru = {crosspos.first, crosspos.second - 1}; 
                            position dird = {crosspos.first, crosspos.second + 1}; 
                            position dirl = {crosspos.first - 1, crosspos.second}; 
                            position dirr = {crosspos.first + 1, crosspos.second}; 
                            if (((deviate2 == "XSIDE")&&((deviate1 == "YSIDE")||(deviate1 == "XMYS")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else if (((deviate2 == "YSIDE")&&((deviate1 == "XSIDE")||(deviate1 == "XSYM")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                        }
                    }
                    else 
                    {
                        position midpos1;
                        midpos1.first = (crosspos.first)*5+2;
                        midpos1.second = (crosspos.second)*5+4;
                        position midpos2;
                        midpos2.first = (crosspos.first)*5+4;
                        midpos2.second = (crosspos.second)*5+2;
                        position dir1 = {midpos1.first, midpos1.second + 1}; 
                        position dir2 = {midpos2.first + 1, midpos2.second}; 

                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                        
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                            break; 
                        }
                
                    }
                }
                else
                {
                    position Ymidpos;
                    Ymidpos.first = crosspos.first*5+4;
                    Ymidpos.second = crosspos.second*5+2;
                    position Xmidpos;
                    Xmidpos.first = crosspos.first*5+2;
                    Xmidpos.second = crosspos.second*5+4;
                    
                    //在MIDDLE向SIDE转变的过程中，即使显示是MIDDLE但是在映射中还是会变成SIDE位置
                    if (((deviate1 == "YMIDDLE")&&(std::find(unitpos1.begin(), unitpos1.end(), Ymidpos) != unitpos1.end()))
                    ||((deviate1 == "XMIDDLE")&&(std::find(unitpos1.begin(), unitpos1.end(), Xmidpos) != unitpos1.end())))
                    {
                        if (deviate1 == "YMIDDLE")
                        {
                            position dir = {Ymidpos.first + 1, Ymidpos.second};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }                             
                        }
                        else
                        {
                            position dir = {Xmidpos.first, Xmidpos.second + 1};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }  
                        }
                    }
                    else if(((deviate2 == "YMIDDLE")&&(std::find(unitpos2.begin(), unitpos2.end(), Ymidpos) != unitpos2.end()))
                    ||((deviate2 == "XMIDDLE")&&(std::find(unitpos2.begin(), unitpos2.end(), Xmidpos) != unitpos2.end())))
                    {
                        if (deviate2 == "YMIDDLE")
                        {
                            position dir = {Ymidpos.first + 1, Ymidpos.second};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }                             
                        }
                        else
                        {
                            position dir = {Xmidpos.first, Xmidpos.second + 1};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }  
                        }
                    }

                    auto headpos1 = unitpos1.front();
                    auto tailpos1 = unitpos1.back();
                    auto headpos2 = unitpos2.front();
                    auto tailpos2 = unitpos2.back();
                    position hdir11 = {headpos1.first, headpos1.second + 1}; 
                    position hdir12 = {headpos1.first, headpos1.second + 2}; 
                    position hdir21 = {headpos1.first, headpos1.second - 1}; 
                    position hdir22 = {headpos1.first, headpos1.second - 2}; 
                    position hdir31 = {headpos1.first - 1, headpos1.second}; 
                    position hdir32 = {headpos1.first - 2, headpos1.second}; 
                    position hdir41 = {headpos1.first + 1, headpos1.second}; 
                    position hdir42 = {headpos1.first + 2, headpos1.second}; 

                    position tdir11 = {tailpos1.first, tailpos1.second + 1}; 
                    position tdir12 = {tailpos1.first, tailpos1.second + 2}; 
                    position tdir21 = {tailpos1.first, tailpos1.second - 1}; 
                    position tdir22 = {tailpos1.first, tailpos1.second - 2}; 
                    position tdir31 = {tailpos1.first - 1, tailpos1.second}; 
                    position tdir32 = {tailpos1.first - 2, tailpos1.second}; 
                    position tdir41 = {tailpos1.first + 1, tailpos1.second}; 
                    position tdir42 = {tailpos1.first + 2, tailpos1.second}; 

                    bool isconflict = false;
                    
                    
                    if (((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir11) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir12) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir21) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir22) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir31) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir32) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir41) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir42) == tempcrosscell.end())))
                    {
                        if (crossline_list.find(pospair2) == crossline_list.end()) 
                        {  
                            crossline_list[pospair2] = {}; 
                            crossline_list[pospair2].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair2].push_back(unitpos2);  
                        }
                        
                        break;
                    }
                    else if(((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir11) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir12) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir21) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir22) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir31) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir32) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir41) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir42) == tempcrosscell.end())))
                    {
                        if (crossline_list.find(pospair2) == crossline_list.end()) 
                        {  
                            crossline_list[pospair2] = {}; 
                            crossline_list[pospair2].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair2].push_back(unitpos2);  
                        }
                        
                        break;
                    }
                      
                    if (!isconflict)
                    {
                        if (crossline_list.find(pospair1) == crossline_list.end()) 
                        {  
                            crossline_list[pospair1] = {}; 
                            crossline_list[pospair1].push_back(unitpos1);  
                        } else 
                        {  
                            crossline_list[pospair1].push_back(unitpos1);  
                        }
                        break;
                    }
                    
                }

                break;
                
            }
            
        }
    }
}

//通过节点的扇入扇出关系对节点的组成元胞分类映射(input,output,normal,fix0,fix1)
void Mapping::node_mapping(std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>>& _Nodelink)
{
    nodecell_list["input"];
    nodecell_list["output"];
    nodecell_list["normal"];
    nodecell_list["fix0"];
    nodecell_list["fix1"];
    



    if(!_Nodelink.empty())
    {
    for (auto &node : _Nodelink)
    {

        //TODO

        position temppos = node.first.first;
        unsigned int temp_first = (temppos.first)*5;
        unsigned int temp_second = (temppos.second)*5;
        position temppos1 = std::pair<unsigned int, unsigned int>(temp_first, temp_second);
        if (node.first.second == "input")
        {
            nodecell_list["input"].emplace_back(temppos1.first+2, temppos1.second+2);
            int size = node.second.second.size();
            if (size == 1)
            {
                if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))//左
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                }
                else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))//右
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))//上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                }
                else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))//下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
            }
            else if(size == 2)
            {
                if (((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                 || ((node.second.second.back().first < temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second)))//左上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                }
                else if (((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                 || ((node.second.second.back().first > temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second)))//右上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                }
                else if (((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                 || ((node.second.second.back().first < temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second)))//左下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
                else if (((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                 || ((node.second.second.back().first > temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second)))//右下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
            }
        }
        else if (node.first.second == "output")
        {
            nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+2);
            if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//左
            {
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
            }
            else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//右
            {
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
            else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
            }
            else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))//下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
            }
        }
        else if (node.first.second == "maj")
        {
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
        }
        else if (node.first.second == "and")
        {
            if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//左上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    
                }
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//右上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//左下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//右下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first == temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//上下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second == temppos.second)))//左右
            {
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                if(node.second.second.empty())
                {
                    nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
                else
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
            }
        }
        else if (node.first.second == "or")
        {
            if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//左上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    
                }
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//右上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//左下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//右下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                || ((node.second.first.back().first == temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//上下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                }
                else
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second))
                || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second == temppos.second)))//左右
            {
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                if(node.second.second.empty())
                {
                    nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
                else
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
            }
        }
        else if (node.first.second == "not")
        {
            int size = node.second.second.size();
            if (size == 1)
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//左
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//上
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//右
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))//下
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
            }
            else if (size == 2)
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if (((node.second.second.front().second < temppos.second)&&(node.second.second.back().first > temppos.first))
                    ||((node.second.second.back().second < temppos.second)&&(node.second.second.front().first > temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if (((node.second.second.front().second > temppos.second)&&(node.second.second.back().first > temppos.first))
                    ||((node.second.second.back().second > temppos.second)&&(node.second.second.front().first > temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    if (((node.second.second.front().first < temppos.first)&&(node.second.second.back().second > temppos.second))
                    ||((node.second.second.back().first < temppos.first)&&(node.second.second.front().second > temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if (((node.second.second.front().first > temppos.first)&&(node.second.second.back().second > temppos.second))
                    ||((node.second.second.back().first > temppos.first)&&(node.second.second.front().second > temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if (((node.second.second.front().second < temppos.second)&&(node.second.second.back().first < temppos.first))
                    ||((node.second.second.back().second < temppos.second)&&(node.second.second.front().first < temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if (((node.second.second.front().second > temppos.second)&&(node.second.second.back().first < temppos.first))
                    ||((node.second.second.back().second > temppos.second)&&(node.second.second.front().first < temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                {
                    if (((node.second.second.front().first < temppos.first)&&(node.second.second.back().second < temppos.second))
                    ||((node.second.second.back().first < temppos.first)&&(node.second.second.front().second < temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if (((node.second.second.front().first > temppos.first)&&(node.second.second.back().second < temppos.second))
                    ||((node.second.second.back().first > temppos.first)&&(node.second.second.front().second < temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
            }
            else//作为输出
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+3, temppos1.second+2);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+3);
                   
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+1, temppos1.second+2);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+1);
                }
                
            }
            
        }
        else if (node.first.second == "wire")
        {
            if ((node.second.first.front().first == temppos.first)&&(node.second.second.front().first == temppos.first))//上下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
            }
            else if ((node.second.first.front().second == temppos.second)&&(node.second.second.front().second == temppos.second))//左右
            {
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.second.front().second < temppos.second))
            ||((node.second.first.front().second < temppos.second)&&(node.second.second.front().first < temppos.first)))//左上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.second.front().second < temppos.second))
            ||((node.second.first.front().second < temppos.second)&&(node.second.second.front().first > temppos.first)))//右上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.second.front().second > temppos.second))
            ||((node.second.first.front().second > temppos.second)&&(node.second.second.front().first < temppos.first)))//左下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.second.front().second > temppos.second))
            ||((node.second.first.front().second > temppos.second)&&(node.second.second.front().first > temppos.first)))//右下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
        }
        else if (node.first.second == "fanout")
        {
            auto size = node.second.second.size();
            if (size == 1)
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                
            }
            else
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    // qDebug() << 
                    if ((node.second.second.front().second < temppos.second)||(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().second > temppos.second)||(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    if ((node.second.second.front().first < temppos.first)||(node.second.second.back().first < temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)||(node.second.second.back().first > temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if ((node.second.second.front().second < temppos.second)||(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().second > temppos.second)||(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                // else((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                else
                {
                    if ((node.second.second.front().first < temppos.first)||(node.second.second.back().first < temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)||(node.second.second.back().first > temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                
            }
        }
        else
        {
            return;
        }
        
    }
    }
}

void Mapping::not_check(std::vector<std::vector<position>> &_routepos_list){
    std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list;
    std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list;

    for (size_t i = 0; i < _routepos_list.size(); i++)
    {
        std::vector<position> oneroute = _routepos_list[i];
        for (size_t i1 = 0; i1 < oneroute.size(); i1++) 
        {  
            for (size_t j1 = i1 + 1; j1 < oneroute.size(); j1++) 
            {  
                if (oneroute[i1] == oneroute[j1]) 
                {  
                    std::vector<position> existpos_list;
                    if(!temppos_list.empty())
                    {
                        for (auto &v: temppos_list)
                        {
                            existpos_list.push_back(v.second);
                        }
                    }
                    auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), oneroute[i1]); 
                    if (exitpos == existpos_list.end())
                    {
                        oneroutepos_list.emplace_back(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), oneroute[i1]));
                    }
                }  
            }  
        }
        for (size_t j = i+1; j < _routepos_list.size(); j++)
        {
            if (_routepos_list[i].front() == _routepos_list[j].front())
            {
                int i1 = 0;
                while (i1 < _routepos_list[i].size() && i1 < _routepos_list[j].size() && _routepos_list[i][i1] == _routepos_list[j][i1])
                {
                    ++i1;
                }
                if ((i1 <= _routepos_list[i].size()) && (i1 <= _routepos_list[j].size()))
                {
                    std::vector<position> itpart(_routepos_list[i].begin()+i1, _routepos_list[i].end());
                    std::vector<position> temppart(_routepos_list[j].begin()+i1, _routepos_list[j].end());
                    if(!temppart.empty())
                    {
                        for (auto &pos1 : temppart)
                        {
                            auto temppos = std::find(itpart.begin(), itpart.end(), pos1);
                            
                            std::vector<position> existpos_list;
                            if(!temppos_list.empty())
                            {
                                for (auto &v: temppos_list)
                                {
                                    existpos_list.push_back(v.second);
                                }
                            }
                            auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos1); 

                            if (temppos != itpart.end() && (*temppos) != itpart.back() && (*temppos) != temppart.back() && exitpos == existpos_list.end())
                            {
                                temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), std::make_pair(_routepos_list[j].front(), _routepos_list[j].back()))), pos1));
                            }
                        }
                        
                    }
                    
                }
                
            }
            else
            {
                for (auto &pos2 : _routepos_list[j])
                {
                    auto temppos = std::find(_routepos_list[i].begin(), _routepos_list[i].end(), pos2);

                    //避免重复检查其他线与扇出前的重合线导致重复输出交叉点
                    std::vector<position> existpos_list;
                    if(!temppos_list.empty())
                    {
                        for (auto &v: temppos_list)
                        {
                            existpos_list.push_back(v.second);
                        }
                    }
                    auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos2); 

                    if (temppos != _routepos_list[i].end() && (*temppos) != _routepos_list[i].back() && (*temppos) != _routepos_list[j].back() && exitpos == existpos_list.end())
                    {
                        temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), std::make_pair(_routepos_list[j].front(), _routepos_list[j].back()))), pos2));
                    }
                    
                }
                
            }
        }
        
    }
    oneroutepos_list_examp = oneroutepos_list;
    temppos_list_examp = temppos_list;
}
};