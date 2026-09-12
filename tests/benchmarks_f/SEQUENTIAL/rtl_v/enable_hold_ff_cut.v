// Register-cut fixture generated from enable_hold_ff.v.  q0 is the sampled
// state pseudo-input and d0 is its next-state event.
module ifcn_cut_enable_hold_ff(q0, i0, i1, i2, d0);
input q0, i0, i1, i2;
output d0;
wire d0, n0, n1, n2, n3, n4;
assign n0 = ~i1;
assign n1 = ~i2;
assign n2 = q0 & n0;
assign n3 = i0 & i1;
assign n4 = n2 | n3;
assign d0 = n1 & n4;
endmodule
