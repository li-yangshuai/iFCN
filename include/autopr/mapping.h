#pragma once
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <iterator>
#include <string>

namespace fcngraph{
    using position = std::pair<unsigned int , unsigned int>;

    class Mapping
    {
    public:
        Mapping(){}
        ~Mapping(){}

        std::map<std::pair<position, position>, std::vector<std::vector<position>>> mapping_line(std::vector<std::vector<position>>& _example);
        void routepos_Deviate(std::vector<position>& _oneroutepos_list);
        void deviate_mapping(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list);
        std::string findInVectorPairFirst(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair);
        bool isfindpostype(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair, std::string& _type);
        void crossline_mapping(std::vector<std::vector<position>> &_routepos_list);
        void node_mapping(std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>>& _Nodelink);
        void not_check(std::vector<std::vector<position>> &_routepos_list);

        std::map<std::pair<position, position>, std::vector<std::vector<position>>> deviatemapping_list;//线映射坐标
        std::map<std::pair<position, position>, std::vector<std::vector<position>>> crossline_list;//交叉线映射坐标cross
        std::map<std::string, std::vector<position>> nodecell_list;//节点的组成元胞映射坐标

        std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list_examp;
        std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list_examp;
        std::vector<std::vector<position>> example = {
        {{5, 5}, {4, 5}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {8, 4}, {8, 3}, {7, 3}, {6, 3}},
        {{2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {6, 7}, {6, 6}, {6, 5}, {6, 4}, {6, 3}},
        {{6, 2}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {3, 2}, {3, 3}},
        {{2, 6}, {3, 6}, {4, 6}, {4, 5}, {4, 4}, {4, 3}, {3, 3}},
        {{6, 2}, {6, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5}},
        {{2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {6, 5}, {5, 5}},
        {{3, 3}, {3, 4}, {3, 5}, {2, 5}},
        {{2, 5}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {2, 8}}};
        
    private:
        std::vector<std::vector<position>> routepos_list;//作为mapping的输入
        std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>> deviate_list;
        // std::map<std::pair<position, position>, std::vector<std::vector<position>>> deviatemapping_list;//线映射坐标
        // std::multimap<std::pair<position, position>, std::vector<position>> crossline_list;//交叉线映射坐标
        // std::vector<std::vector<position>> example = {
        // {{5, 5}, {4, 5}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {8, 4}, {8, 3}, {7, 3}, {6, 3}},
        // {{2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {6, 7}, {6, 6}, {6, 5}, {6, 4}, {6, 3}},
        // {{6, 2}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {3, 2}, {3, 3}},
        // {{2, 6}, {3, 6}, {4, 6}, {4, 5}, {4, 4}, {4, 3}, {3, 3}},
        // {{6, 2}, {6, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5}},
        // {{2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {6, 5}, {5, 5}},
        // {{3, 3}, {3, 4}, {3, 5}, {2, 5}},
        // {{2, 5}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {2, 8}}};
    };
    
    
};