#pragma once
#include<vector>
#include<memory>
#include<stdexcept>

namespace fcngraph{

enum class Direction { UP, DOWN, LEFT, RIGHT };
enum class CLOCK_SCHEME {TDD, USE, RES, BANCS, CFE};

struct Pattern {
    uint8_t value;  // 当前单元的值（1-4)
    std::vector<Direction> directions;  
};

// 定义模式
extern const Pattern tdd_pattern[4][4];
extern const Pattern use_pattern[4][4];
extern const Pattern res_pattern[4][4];


/*
采用工厂模式，灵活地创建需要的时钟方案对象，可以在 MortonGrid 类中根据当前的时钟方案来更新或初始化网格。
*/

class ClockScheme {
public:
    virtual ~ClockScheme() = default;
    virtual const Pattern* getPattern(int& width, int& height) const = 0; // 返回 Pattern 指针和尺寸
};


class TddClockScheme : public ClockScheme {
public:
    const Pattern* getPattern(int& width, int& height) const override {
        width = 4; 
        height = 4; 
        return &tdd_pattern[0][0];
    }
};

class UseClockScheme : public ClockScheme {
public:
    const Pattern* getPattern(int& width, int& height) const override {
        width = 4; 
        height = 4; 
        return &use_pattern[0][0];
    }
};

class ResClockScheme : public ClockScheme {
public:
    const Pattern* getPattern(int& width, int& height) const override {
        width = 4; 
        height = 4; 
        return &res_pattern[0][0];
    }
};

//工厂类
class ClockSchemeFactory {
public:
    static std::unique_ptr<ClockScheme> createClockScheme(CLOCK_SCHEME scheme) {
        switch (scheme) {
            case CLOCK_SCHEME::TDD:
                return std::make_unique<TddClockScheme>();
            case CLOCK_SCHEME::USE:
                return std::make_unique<UseClockScheme>();
            case CLOCK_SCHEME::RES:
                return std::make_unique<ResClockScheme>();
            default:
                throw std::invalid_argument("Unknown clock scheme");
        }
    }
};






};