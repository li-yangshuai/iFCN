// Four-bit free-running Johnson counter.
// The physical fixture exposes four simultaneous state captures; its initial
// state is supplied externally because this minimal benchmark has no reset.
module johnson4_free_running (
    input  wire       clk,
    output reg  [3:0] q
);
    always @(posedge clk)
        q <= {q[2:0], ~q[3]};
endmodule
