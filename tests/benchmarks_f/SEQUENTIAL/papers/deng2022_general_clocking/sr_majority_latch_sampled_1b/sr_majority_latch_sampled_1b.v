// Discrete state-equation adapter for automatic iFCN experiments based on
// Deng et al. (2022), Fig. 9.  "tick" is an abstract observation boundary;
// the combinational expression is the paper's majority feedback equation.
module sr_majority_latch_sampled_1b (
    input  wire tick,
    input  wire s,
    input  wire r,
    output reg  q
);
    wire q_next;

    assign q_next = (s & ~r) | (s & q) | (~r & q);

    always @(posedge tick)
        q <= q_next;
endmodule
