// Enabled register. When en=0, the current state is held.
// QCA P&R purpose: exercise a feedback MUX in front of a state boundary.
module enable_hold_ff (
    input  wire clk,
    input  wire rst,
    input  wire en,
    input  wire d,
    output reg  q
);
    always @(posedge clk) begin
        if (rst)
            q <= 1'b0;
        else if (en)
            q <= d;
        else
            q <= q;
    end
endmodule
