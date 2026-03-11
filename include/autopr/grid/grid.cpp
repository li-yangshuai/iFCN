#include "grid.h"

namespace fcngraph{

void GridChessboard::set_clockType(CLOCK_SCHEME  _clockType){
    directionMap.clear();
    clockScheme = ClockSchemeFactory::createClockScheme(_clockType);
    patternData = clockScheme->getPattern(patternWidth, patternHeight);
    for(int x = chessboard_nw.first; x < chessboard_se.first; x++){
        for(int y = chessboard_nw.second; y < chessboard_se.second; y++){
            position pos{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
            directionMap[pos] = getPosssibleDirection(pos);
            GridCell gridcell;
            gridMap[pos] = gridcell;
        }
    }
}



std::vector<position> GridChessboard::getPosssibleDirection(const position& pos, bool regularClockScheme)  {
    unsigned int x = pos.first, y = pos.second;
    std::vector<Direction> vecDir;
    //如果是规律时钟方案就是获取固定的时钟走向
    if(regularClockScheme){
        vecDir = getPatternAt(x, y).directions;
    }else{
        //否则是下、左、右
        vecDir = {Direction::LEFT, Direction::RIGHT, Direction::UP, Direction::DOWN};
    }

    std::vector<position> wirePossiblePos;
    for(auto &dir :vecDir){
        unsigned int target_x = x, target_y = y;
        switch (dir) {
            case Direction::UP:
                target_y = y - 1;
                break;
            case Direction::DOWN:
                target_y = y + 1;
                break;
            case Direction::LEFT:
                target_x = x - 1;
                break;
            case Direction::RIGHT:
                target_x = x + 1;
                break;
        }
        if(regularClockScheme){
            if(target_x < chessboard_nw.first ||  target_x > chessboard_se.first || target_y < chessboard_nw.second || target_y > chessboard_se.second)
                continue;
        }
        wirePossiblePos.push_back({target_x, target_y});
    }
    return wirePossiblePos;
}


};
