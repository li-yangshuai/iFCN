// Minimal positive-edge D flip-flop with synchronous active-high reset.
// QCA P&R purpose: characterize one state boundary and its D/Q ports.
module dff_sync (
    input  wire clk,
    input  wire rst,
    input  wire d,
    output reg  q
);
    always @(posedge clk) begin
        if (rst)
            q <= 1'b0;
        else
            q <= d;
    end
endmodule
