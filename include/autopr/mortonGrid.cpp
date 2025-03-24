#include"mortonGrid.h"

namespace fcngraph{

void MortonChessboard::set_clockType(CLOCK_SCHEME  _clockType){
    directionMap.clear();
    clockScheme = ClockSchemeFactory::createClockScheme(_clockType);
    patternData = clockScheme->getPattern(patternWidth, patternHeight);
    for(int x = chessboard_nw.first; x < chessboard_se.first; x++){
        for(int y = chessboard_nw.second; y < chessboard_se.second; y++){
            unsigned int mortonCode = calculateMortonCode(x, y);
            directionMap[mortonCode] = getPosssibleDirection(mortonCode);
            GridCell gridcell;
            gridMap[mortonCode] = gridcell;
        }
    }
}



std::vector<unsigned int> MortonChessboard::getPosssibleDirection(unsigned int morton, bool regularClockScheme)  {
    auto pos = decodeMortonCode(morton);
    unsigned int x = pos.first, y = pos.second;
    std::vector<Direction> vecDir;
    //如果是规律时钟方案就是获取固定的时钟走向
    if(regularClockScheme){
        vecDir = getPatternAt(x, y).directions;
    }else{
        //否则是下、左、右
        vecDir = {Direction::LEFT, Direction::RIGHT, Direction::UP, Direction::DOWN};
    }

    std::vector<unsigned int> wirePossibleMorton;
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
        wirePossibleMorton.push_back(calculateMortonCode(target_x, target_y));
    }
    return wirePossibleMorton;
}


};
