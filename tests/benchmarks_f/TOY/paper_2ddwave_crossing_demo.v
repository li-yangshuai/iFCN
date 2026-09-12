module top(a, b, c, carry);
input a, b, c;
output carry;
wire ab, bc, ac, merge;

assign ab = a & b;
assign bc = b & c;
assign ac = a & c;
assign merge = ab | bc;
assign carry = ac | merge;

endmodule
