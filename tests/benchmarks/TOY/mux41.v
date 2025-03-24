module top(i0, i1, i2, i3, s0, s1, out);
input i0, i1, i2, i3, s0, s1;
output out;


wire w1, w2, w3, w4, w5, w6, w7, w8, w9, mux1, mux2;

assign w1 = ~s0;
assign w2 = i0 & w1;
assign w3 = i1 & s0;
assign mux1 = w2 | w3;

assign w4 = ~s0;
assign w5 = i2 & w4;
assign w6 = i3 & s0;
assign mux2 = w5 | w6;

assign w7 = ~s1;
assign w8 = mux1 & w7;
assign w9 = mux2 & s1;
assign out = w8 | w9;

endmodule
