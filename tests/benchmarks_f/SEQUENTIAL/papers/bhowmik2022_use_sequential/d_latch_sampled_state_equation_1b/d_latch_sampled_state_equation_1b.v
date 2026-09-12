// Discrete state-equation adapter for automatic iFCN experiments based on
// Bhowmik et al. (2022), Fig. 9.  At each abstract observation tick it applies
// Q_next = D*CLOCK + Q*~CLOCK.  This preserves the stable sampled transition
// relation, but does not claim transparent-latch timing equivalence.
module d_latch_sampled_state_equation_1b (
    input  wire tick,
    input  wire d,
    input  wire clock,
    output reg  q
);
    always @(posedge tick)
        q <= clock ? d : q;
endmodule
