// Sampled-state adapter for a complete one-bit T flip-flop.
//
// sample_tick advances the reference model by one observation boundary and is
// distinct from the circuit's logical clock input.  The published contract has
// no reset: q toggles only when both T and logical_clk are asserted; otherwise
// it holds its previous value.
module tff_sampled (
    input  wire sample_tick,
    input  wire logical_clk,
    input  wire t,
    output reg  q
);
    always @(posedge sample_tick) begin
        if (logical_clk && t)
            q <= ~q;
        else
            q <= q;
    end
endmodule
