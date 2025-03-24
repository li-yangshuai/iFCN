module top(A, H, I, J, K, L, M, O, P, Q);
input  A, H, I, J, K, L, M;
output O ,P, Q;
wire B, C, D, E, F, G;

assign B = A & H;
assign C = A & I;
assign D = A & J;
assign E = A & K;
assign F = A & L;
assign G = A & M;

assign O = B | C;
assign P = D | E;
assign Q = F | G;


endmodule
