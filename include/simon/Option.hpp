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

#if !defined(HFUT_SIMON_OPTION_HPP)
#define      HFUT_SIMON_OPTION_HPP

#include <array>
#include "Util.hpp"


namespace simon {
    struct QCABistableOption {
        //tecnology options
        double epsilon_r = 12.9;
        double layer_separation = 11.5;
        double radius_effect = 65;
        //clock options
        double amplitude = 2.0;
        double high = 9.800000e-22;
        double low  = 3.800000e-23;
        double shift = 0.0;
        std::array<double, 4> jitters = {0,0,0,0};
        //algorithm options
        std::size_t number_of_samples = 12800;
        std::size_t max_iteration_per_sample = 100;
        double convergence_tolerance = 0.001;
    };

    struct QCACoherenceOption {
        //tecnology options
        double epsilon_r = 12.9;
        double layer_separation = 11.5;
        double radius_effect = 80;
        //clock options
        double amplitude =2.0;
        double high = 9.80000e-22;
        double low  = 3.80000e-23;
        double shift= 0.0;
        std::array<double, 4> jitters = {0,0,0,0};
        //algorithm options
        double T = 1.0;
        double relaxation = 1e-15;
        double time_step  = 1e-16;
        double duration   = 7e-11;
        NumericMethod algorithm = NumericMethod::Euler;
    };

    struct EnergyAnalysisOption : QCACoherenceOption {
        EnergyAnalysisOption() : QCACoherenceOption() {
            time_step = 1.00000e-17;
            duration  = 8.00000e-11; //duration = 2.50000e-9;

            clock_slope  = 1.00000e-12;
            clock_period = 1.00000e-11; // 4.00000e-10;
            input_period = 1.00000e-11; // 4.00000e-10;
        }
        double clock_slope; //for RAMP, GAUSS
        double clock_period;
        double input_period;
    };
    
    struct NMLOption {
        std::size_t number_of_samples = 12800;
        std::size_t max_iteration_per_sample = 100;
        double      convergence_tolerance = 0.001;
    };
}//namespace simon

#endif
