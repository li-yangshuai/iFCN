// Two-bit synchronous Johnson counter with active-high reset.
// After reset: 00 -> 01 -> 11 -> 10 -> 00.
module johnson2_sync (
    input  wire       clk,
    input  wire       rst,
    output reg  [1:0] q
);
    always @(posedge clk) begin
        if (rst)
            q <= 2'b00;
        else
            q <= {q[0], ~q[1]};
    end
endmodule
