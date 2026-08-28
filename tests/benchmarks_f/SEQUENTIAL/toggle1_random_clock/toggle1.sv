module toggle1 (
    input  logic clk,
    output logic q
);
    // Free-running one-bit state machine: q(t+1) = not q(t).
    always_ff @(posedge clk) begin
        q <= ~q;
    end
endmodule
