// Valid-domain sampled state equation reconstructed from Bhowmik et al.
// (2022), Fig. 12.  For the active-low NAND latch,
// Q_next = ~S_n | (R_n & Q); S_n=R_n=0 remains outside the comparison domain.
module sr_nand_latch_sampled_valid_domain_1b (
    input  wire tick,
    input  wire s_n,
    input  wire r_n,
    output reg  q
);
    always @(posedge tick)
        q <= ~s_n | (r_n & q);
endmodule
