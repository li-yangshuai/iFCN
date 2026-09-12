// Sampled, topology-faithful reconstruction of the cross-coupled NAND pair in
// Bhowmik et al. (2022), Fig. 12.  The paper labels the pins S/R, while NAND
// realization makes them active low; s_n/r_n make that polarity explicit.
//
// sample_clk is an adapter transaction boundary, not a logical clock pin in
// the source latch.  Both nonblocking assignments use the preceding rail
// values, preserving the two cross-coupled NAND equations as one sampled
// iteration.  s_n=r_n=0 is forbidden and carries no source-equivalence claim.
module sr_nand_latch_sampled_topology_2b (
    input  logic sample_clk,
    input  logic s_n,
    input  logic r_n,
    output logic q,
    output logic q_bar
);
    always_ff @(posedge sample_clk) begin
        q     <= ~(s_n & q_bar);
        q_bar <= ~(r_n & q);
    end
endmodule
