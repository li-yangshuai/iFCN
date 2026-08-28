// Four-bit maximal-length Fibonacci LFSR.
// Polynomial: x^4 + x^3 + 1. Reset seed 0001 has a 15-state period.
// QCA P&R purpose: exercise XOR feedback, fanout, and a long state cycle.
module lfsr4 (
    input  wire       clk,
    input  wire       rst,
    input  wire       en,
    output reg  [3:0] q
);
    wire feedback;

    assign feedback = q[3] ^ q[2];

    always @(posedge clk) begin
        if (rst)
            q <= 4'b0001;
        else if (en)
            q <= {q[2:0], feedback};
        else
            q <= q;
    end
endmodule
