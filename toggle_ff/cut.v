module ifcn_cut_toggle_ff(q0,i0,d0);
input q0,i0;
output d0;
wire d0,n0;
assign n0=i0|q0;
assign d0=~n0;
endmodule
