//simon: C++ FCN network simulation framework
//Copyright (c) 2019 HFUT
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#if !defined(HFUT_SIMON_UTIL_HPP)
#define      HFUT_SIMON_UTIL_HPP

#include <iostream>

namespace simon
{
    enum class FCNCellFunction {
        NORMAL,
        INPUT,
        OUTPUT,
        FIXED,
        LAST_FUNCTION
    };

    inline std::ostream &operator<<(std::ostream &out, FCNCellFunction func) {

        switch(func) {
            case FCNCellFunction::NORMAL:
                out << "NORMAL";
                break;
            case FCNCellFunction::INPUT:
                out << "INPUT";
                break;
            case FCNCellFunction::OUTPUT:
                out << "OUTPUT";
                break;
            case FCNCellFunction::FIXED:
                out << "FIXED";
                break;
            case FCNCellFunction::LAST_FUNCTION:
                out << "LAST_FUNCTION";
                break;
        }
        return out;
    }

    enum class QCACellMode {
        NORMAL,
        CROSSOVER,
        VERTICAL,
        CLUSTER
    };

    inline std::ostream &operator<<(std::ostream &out, QCACellMode mode) {
        switch(mode) {
            case QCACellMode::NORMAL:
                out << "NORMAL";
                break;
            case QCACellMode::CROSSOVER:
                out << "CROSSOVER";
                break;
            case QCACellMode::VERTICAL:
                out << "VERTICAL";
                break;
            case QCACellMode::CLUSTER:
                out << "CLUSTER";
                break;
            default:
                std::cerr << "Error cell mode detected";
                break;
        }
        return out;
    }


    enum class QCALayerType {
        SUBSTRATE,
        CELLS,
        CLOCKING,
        DRAWING,
        DISTRIBUTION,
        LAST_TYPE
    };

    inline std::ostream &operator<<(std::ostream &out, QCALayerType layer_type) {

        switch(layer_type) {
            case QCALayerType::SUBSTRATE:
                out << "LAYER_TYPE_SUBSTRATE";
                break;
            case QCALayerType::CELLS:
                out << "LAYER_TYPE_CELLS";
                break;
            case QCALayerType::CLOCKING:
                out << "LAYER_TYPE_CLOKCING";
                break;
            case QCALayerType::DRAWING:
                out << "LAYER_TYPE_DRAWING";
                break;
            case QCALayerType::DISTRIBUTION:
                out << "LAYER_TYPE_DISTRIBUTION";
                break;
            case QCALayerType::LAST_TYPE:
                out << "LAYER_TYPE_LAST_TYPE";
                break;
        }

        return out;
    }

    enum class QCALayerStatus {
        ACTIVE,     /* Editable (=> visible) */
        VISIBLE,    /* Non-editable */
        HIDDEN,     /* not shown */
        LAST_STATUS
    };

    inline std::ostream &operator<<(std::ostream &out, QCALayerStatus layer_status) {

        switch(layer_status) {
            case QCALayerStatus::ACTIVE:
                out << "LAYER_STATUS_ACTIVE";
                break;
            case QCALayerStatus::VISIBLE:
                out << "LAYER_STATUS_VISIBLE";
                break;
            case QCALayerStatus::HIDDEN:
                out << "LAYER_STATUS_HIDDEN";
                break;
            case QCALayerStatus::LAST_STATUS:
                out << "LAYER_STATUS_LAST_STATUS";
                break;
        }

        return out;
    }

    enum class NumericMethod { Euler, RungeKutta };

    enum class SimulationMode {Exhaustive, Selective};

    enum class TraceFunction {INPUT, OUTPUT, GAMMAR, CLOCK, BUS};

    enum class ClockSignalType {COS, RAMP, GAUSS};

    namespace constants
    {
        constexpr double QCHARGE = 1.60217646200000007055474082341e-19;
        constexpr double HALF_QCHARGE = 8.01088231000000035277370411706e-20;    //!
        constexpr double OVER_QCHARGE = 6.24150974451152486400000000000e18;     //!!

        constexpr double QCHARGE_SQUARE_OVER_FOUR = 6.41742353846709430467559076549e-39; //!!
        constexpr double THIRD_QCHARGE = 5.34058820666666650061373099737e-20;
        constexpr double TWO_THIRDS_QCHARGE = 1.06811764133333330012274619947e-19;
        constexpr double TWO_FIFTHS_QCHARGE = 6.40870584800000052296020634206e-20;
        constexpr double ONE_FIFTHS_QCHARGE = 3.20435292400000026148010317103e-20;
        constexpr double THREE_FIFTHS_QCHARGE = 9.61305877200000138629341713410e-20;
        constexpr double FOUR_FIFTHS_QCHARGE = 1.28174116960000010459204126841e-19;
        constexpr double ONE_OVER_FOUR_HALF_QCHARGE = 3.12075487225576243200000000000e18;

        constexpr double PI = 3.141592653589793115997963468544;       //!!
        constexpr double TWO_PI = 6.283185307179586231995926937088;   //!!
        constexpr double FOUR_PI = 12.56637061435917246399185387418;
        constexpr double HBAR = 1.05457159642078551348871563291e-34;

        constexpr double OVER_HBAR = 9.48252355168675613949607578855e33;
        constexpr double PRECISION  = 1e-5;

        constexpr double EPSION = 8.85418781700000047157054854521e-12;
        constexpr double FOUR_PI_EPSILON = 1.11265005597565794635320037482e-10; //!!
        constexpr double hbar = 1.05457266e-34;
        constexpr double over_hbar = 9.48252e33;
        constexpr double hbar_sqr = 1.11212e-68;
        constexpr double over_hbar_sqr = 8.99183e67;
        constexpr double kB = 1.381e-23;
        constexpr double over_kB = 7.24296e22;
        constexpr double E = 1.602e-19L;

    }//namespace constants

}//namespace simon

#endif
