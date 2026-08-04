module top(a, b, out);
input a, b;
output out;
wire and_ab, or_ab, not_and_ab;

assign and_ab = a & b;
assign or_ab = a | b;
assign not_and_ab = ~and_ab;
assign out = or_ab & not_and_ab;

endmodule
