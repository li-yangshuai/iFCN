#ifndef CONFIG_H
#define CONFIG_H


enum class EditMode {
    Select,
    Insert,
    DragScene
};

enum class CellType{
    NormalCell, 
    InputCell, 
    OutputCell, 
    FixedCell_0, 
    FixedCell_1, 
    VerticalCell, 
    CrossoverCell
};

#define GRID_SIZE 20
#define SCENE_WIDTH 10000
#define SCENE_HEIGHT 8000


#define MAIN_SCREEN "#50556b"

//cell color style
#define PARSE_0 "#86e291"
#define PARSE_1 "#ffa5fa"
#define PARSE_2 "#00c8bc"
#define PARSE_3 "#f2f2f2"
#define INPUT_COLOR "#008dc8"
#define OUTPUT_COLOR "#e28686"

//clock scheme color style
#define CLOCK_ZONE_0 "#f2f2f2"
#define CLOCK_ZONE_1 "#dfdfdf"
#define CLOCK_ZONE_2 "#bfbfbf"
#define CLOCK_ZONE_3 "#808080"

#define CLOCK_SCHEME_SIZE_5 100
#define CLOCK_SCHEME_SIZE_7 140

static constexpr int ONEDIMEN_CLOKC_SCHEME [4][4]= {{0,1,2,3},
                                                    {0,1,2,3},
                                                    {0,1,2,3},
                                                    {0,1,2,3}};

static constexpr int TDDWAVE_CLOCK_SCHEME [4][4] = {{0,1,2,3},
                                                    {1,2,3,0},
                                                    {2,3,0,1},
                                                    {3,0,1,2}};

static constexpr int USE_CLOKC_SCHEME [4][4]= {{0,1,2,3},
                                               {3,2,1,0},
                                               {2,3,0,1},
                                               {1,0,3,2}};

static constexpr int RES_CLOKC_SCHEME [4][4]= {{3,0,1,2},
                                               {0,1,0,3},
                                               {1,2,3,0},
                                               {0,3,2,1}};



#endif 