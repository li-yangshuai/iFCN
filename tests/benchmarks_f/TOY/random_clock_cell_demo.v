// Compact two-output benchmark for the random-clock cell-level demonstration.
// It contains convergent paths and repeated fanouts without using unsupported
// sequential constructs, so the layout and routing stages remain visible.
module random_clock_cell_demo(a, b, c, d, y0, y1);
input a, b, c, d;
output y0, y1;

wire m0, m1, m2, m3, m4;

assign m0 = (a & b) | (a & c) | (b & c);
assign m1 = ~d;
assign m2 = m0 & m1;
assign m3 = a | d;
assign m4 = m2 | m3;
assign y0 = m4;
assign y1 = m0 & m3;

endmodule
