#pragma once

namespace ifcn::verilog {

// A sequential D event is a distinct output boundary after the inverter.
// Fusing that boundary into the NOT would turn q -> NOT -> d into a self-loop
// when cyclic placement reconnects the cut q event to d.
enum class OutputBoundaryMode {
    Combinational,
    PreserveSequential
};

} // namespace ifcn::verilog
