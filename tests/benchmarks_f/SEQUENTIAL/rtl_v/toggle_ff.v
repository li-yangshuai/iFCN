// One-register feedback loop: q(t+1) = ~q(t).
// QCA P&R purpose: require a positive-delay feedback route across one II.
module toggle_ff (
    input  wire clk,
    input  wire rst,
    output reg  q
);
    always @(posedge clk) begin
        if (rst)
            q <= 1'b0;
        else
            q <= ~q;
    end
endmodule
