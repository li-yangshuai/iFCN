module top(x, y, z, carry);
input x, y, z;
output carry;
wire or_yz, and_yz, xor_yz, and_x;

assign or_yz = y | z;
assign and_yz = y & z;
assign xor_yz = or_yz & ~and_yz;
assign and_x = x & xor_yz;
assign carry = and_yz | and_x;

endmodule
