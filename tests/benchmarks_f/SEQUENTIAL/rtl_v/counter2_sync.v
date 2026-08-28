// Two-bit modulo-4 up-counter with clock enable.
// QCA P&R purpose: align two register captures through unequal logic cones.
module counter2_sync (
    input  wire       clk,
    input  wire       rst,
    input  wire       en,
    output reg  [1:0] q
);
    always @(posedge clk) begin
        if (rst)
            q <= 2'b00;
        else if (en)
            q <= q + 2'b01;
        else
            q <= q;
    end
endmodule
