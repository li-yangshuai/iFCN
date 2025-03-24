#include "patterns.h"



namespace fcngraph
{

    const Pattern tdd_pattern[4][4] = {
        {{1, {Direction::RIGHT, Direction::DOWN}},  {2, {Direction::RIGHT, Direction::DOWN}}, {3, {Direction::RIGHT, Direction::DOWN}}, {4, {Direction::RIGHT, Direction::DOWN}}},
        {{2, {Direction::RIGHT, Direction::DOWN}},  {3, {Direction::RIGHT, Direction::DOWN}}, {4, {Direction::RIGHT, Direction::DOWN}}, {1, {Direction::RIGHT, Direction::DOWN}}},
        {{3, {Direction::RIGHT, Direction::DOWN}},  {4, {Direction::RIGHT, Direction::DOWN}}, {1, {Direction::RIGHT, Direction::DOWN}}, {2, {Direction::RIGHT, Direction::DOWN}}},
        {{4, {Direction::RIGHT, Direction::DOWN}},  {1, {Direction::RIGHT, Direction::DOWN}}, {2, {Direction::RIGHT, Direction::DOWN}}, {3, {Direction::RIGHT, Direction::DOWN}}}
    };

    //输入时候要与图片的时钟相位上下颠倒
    const Pattern use_pattern[4][4] = {
        {{1, {Direction::RIGHT, Direction::UP}},  {2, {Direction::RIGHT, Direction::DOWN}},  {3, {Direction::RIGHT, Direction::UP}},  {4, {Direction::RIGHT, Direction::DOWN}}},
        {{4, {Direction::LEFT, Direction::UP}},   {3, {Direction::LEFT, Direction::DOWN}},   {2, {Direction::LEFT, Direction::UP}},   {1, {Direction::LEFT, Direction::DOWN}}},
        {{3, {Direction::RIGHT, Direction::UP}},  {4, {Direction::RIGHT, Direction::DOWN}},  {1, {Direction::RIGHT, Direction::UP}},  {2, {Direction::RIGHT, Direction::DOWN}}},
        {{2, {Direction::LEFT, Direction::UP}},   {1, {Direction::LEFT, Direction::DOWN}},   {4, {Direction::LEFT, Direction::UP}},   {3, {Direction::LEFT, Direction::DOWN}}}
    };

    const Pattern res_pattern[4][4] = {
        {{4, {Direction::RIGHT, Direction::DOWN}},  {1, {Direction::RIGHT, Direction::DOWN}}, {2, {Direction::RIGHT, Direction::UP}},  {3, {Direction::RIGHT, Direction::DOWN}}},
        {{1, {Direction::RIGHT, Direction::DOWN}},  {2, {Direction::DOWN}},                   {1, {Direction::LEFT}},                  {4, {Direction::LEFT, Direction::DOWN}}},
        {{2, {Direction::RIGHT}},                   {3, {Direction::RIGHT, Direction::DOWN}}, {4, {Direction::RIGHT, Direction::UP}},  {1, {Direction::RIGHT, Direction::DOWN}}},
        {{1, {Direction::LEFT, Direction::UP}},     {4, {Direction::LEFT, Direction::DOWN}},  {3, {Direction::LEFT, Direction::UP}},   {2, {Direction::LEFT, Direction::DOWN}}}
    };




};// namespace fcngraph




