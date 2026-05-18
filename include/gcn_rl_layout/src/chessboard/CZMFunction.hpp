#pragma once

#include <vector>
#include <string>
#include <random>
#include <cstdint>
#include <cmath>

namespace iFCN_Lab {

constexpr int CLOCK_MOD = 4; // 支持4相时钟（如需扩展只需修改此值）

// ----------- CZMFunction: 多项式编码与解析 -----------
class CZMFunction {
public:
    explicit CZMFunction(int encodedValue) { decode(encodedValue); }

    // 多项式计算，结果始终限制在 0~3 范围
    int evaluate(int x) const {
        int result = 0, x_pow = 1;
        for (int c : coefficients) {
            result += c * x_pow;
            x_pow *= x;
        }
        return ((result % CLOCK_MOD) + CLOCK_MOD) % CLOCK_MOD; // always 0~3
    }

    // 反向推导：将一组相位（低位在后）组合为十进制编码
    static int reverseEvaluate(const std::vector<int>& phases) {
        if (phases.empty()) return 0;
        int decimalValue = 0;
        for (int ph : phases) {
            decimalValue = decimalValue * CLOCK_MOD + ph;
        }
        return decimalValue;
    }

private:
    std::vector<int> coefficients;

    // 解码 CZM 编码，得到多项式系数
    void decode(int encodedValue) {
        coefficients.clear();
        if (encodedValue == 0) {
            coefficients.push_back(0);
            return;
        }
        while (encodedValue > 0) {
            coefficients.push_back(encodedValue % CLOCK_MOD);
            encodedValue /= CLOCK_MOD;
        }
    }
};


// ----------- 抽象时钟策略接口 -----------
class ClockStrategy {
public:
    virtual std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) = 0;
    virtual std::string name() const = 0;
    virtual ~ClockStrategy() = default;

    // 获取最近一次生成的 phase values
    const std::vector<uint64_t>& lastPhaseValues() const { return phaseCache_; }
protected:
    std::vector<uint64_t> phaseCache_;

};


// ----------- 随机时钟策略示例 -----------
class RandomClock : public ClockStrategy {
public:
    RandomClock(uint64_t seed = std::random_device{}()) : rng(seed) {}

    // 为每个4x4叶子节点生成一个随机编码值
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        int nodeCount = (gridSize * gridSize) / 16;
        phaseCache_.resize(nodeCount);
        std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFull);
        for (auto& v : phaseCache_) {
            v = dist(rng);
        }
        return phaseCache_;
    }

    std::string name() const override {
        return "RandomClock";
    }
private:
    std::mt19937_64 rng;
};

//2DDWave 是固定时钟策略：67438087
class DWaveClock : public ClockStrategy {
public:
    DWaveClock() = default;

    // 为每个4x4叶子节点生成一个固定编码值
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        int nodeCount = (gridSize * gridSize) / 16;
        phaseCache_ = std::vector<uint64_t>(nodeCount, 67438087);
        return phaseCache_;
    }

    std::string name() const override {
        return "DWaveClock";
    }
};


//USE 是固定时钟策略：68093453
class USEClock : public ClockStrategy {
public:
    USEClock() = default;

    // 为每个4x4叶子节点生成一个固定编码值
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        int nodeCount = (gridSize * gridSize) / 16;
        phaseCache_ = std::vector<uint64_t>(nodeCount, 68093453);
        return phaseCache_;
    }

    std::string name() const override {
        return "USEClock";
    }
};

//RES 是固定时钟策略：121636108
class RESClock : public ClockStrategy {
public:
    RESClock() = default;

    // 为每个4x4叶子节点生成一个固定编码值
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        int nodeCount = (gridSize * gridSize) / 16;
        phaseCache_ = std::vector<uint64_t>(nodeCount, 121636108);
        return phaseCache_;
    }
    
    std::string name() const override {
        return "RESClock";
    }
};

class SplicingClock : public ClockStrategy {
public:
    SplicingClock() = default;

    // 为每个4x4叶子节点生成一个固定编码值
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        int nodeCount = (gridSize * gridSize) / 16;
        phaseCache_ = std::vector<uint64_t>{67438087, 121636108, 67438087,121636108};
        return phaseCache_;
    }

    std::string name() const override {
        return "SplicingClock";
    }
};


} // namespace iFCN_Lab
