// Level-sensitive state reconstruction of Deng et al. (2022), Fig. 9.
// The paper feeds S, ~R, and feedback Q into a 3-input majority gate:
// Q_next = M(S, ~R, Q).  Both 00 and 11 hold.  This intentionally infers a
// latch and is an expected rejection for SeqIR v0.
module sr_majority_latch_1b (
    input  wire s,
    input  wire r,
    output reg  q
);
    always @(*) begin
        if (s && !r)
            q = 1'b1;
        else if (!s && r)
            q = 1'b0;
    end
endmodule
