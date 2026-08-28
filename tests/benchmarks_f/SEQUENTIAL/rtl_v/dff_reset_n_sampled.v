// Sampled-state adapter for a rising-edge DFF with dominant active-low reset.
//
// sample_tick is only the state-iteration boundary.  logical_clk is an input
// event flag: high denotes the active logical clock edge at this boundary and
// low denotes no edge, which retains the DFF hold transition without adding a
// fictitious state bit.  reset_n dominates both capture and hold.
module dff_reset_n_sampled (
    input  wire sample_tick,
    input  wire logical_clk,
    input  wire reset_n,
    input  wire d,
    output reg  q
);
    always @(posedge sample_tick) begin
        if (!reset_n)
            q <= 1'b0;
        else if (logical_clk)
            q <= d;
        else
            q <= q;
    end
endmodule
