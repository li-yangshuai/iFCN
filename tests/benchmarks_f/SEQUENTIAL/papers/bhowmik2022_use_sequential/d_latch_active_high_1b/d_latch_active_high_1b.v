// Topology-faithful behavioral reconstruction of Fig. 9 in Bhowmik et al.
// (2022).  Although the paper calls this a D flip-flop, its AND/OR feedback
// network implements Q_next = D*CLOCK + Q*~CLOCK: a high-level D latch.
// This file must remain a latch and is an expected rejection for SeqIR v0.
module d_latch_active_high_1b (
    input  wire d,
    input  wire clock,
    output reg  q
);
    always @(*) begin
        if (clock)
            q = d;
    end
endmodule
