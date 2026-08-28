// Topology-faithful behavioral reconstruction of Fig. 12 in Bhowmik et al.
// (2022): a cross-coupled NAND latch.  The paper labels its pins S/R, but the
// NAND topology makes them active low; use s_n/r_n to make that explicit.
// S_n=R_n=0 is forbidden.  This latch is an expected rejection for SeqIR v0.
module sr_nand_latch_active_low_1b (
    input  wire s_n,
    input  wire r_n,
    output reg  q
);
    always @(*) begin
        if (!s_n && r_n)
            q = 1'b1;
        else if (s_n && !r_n)
            q = 1'b0;
    end
endmodule
