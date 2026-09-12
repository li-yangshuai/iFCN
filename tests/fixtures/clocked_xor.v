module xor2(a, b, y);
input a, b;
output y;
wire ifcn_tmp_0, ifcn_tmp_1, ifcn_tmp_2;
assign ifcn_tmp_0 = a | b;
assign ifcn_tmp_1 = a & b;
assign ifcn_tmp_2 = ~ifcn_tmp_1;
assign y = ifcn_tmp_0 & ifcn_tmp_2;
endmodule
