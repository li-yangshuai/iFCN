// Four-bit serial-in/serial-out shift register with clock enable.
// QCA P&R purpose: exercise several state macros with matched capture epochs.
module shift_register4 (
    input  wire       clk,
    input  wire       rst,
    input  wire       en,
    input  wire       serial_in,
    output wire       serial_out,
    output reg  [3:0] q
);
    assign serial_out = q[3];

    always @(posedge clk) begin
        if (rst)
            q <= 4'b0000;
        else if (en)
            q <= {q[2:0], serial_in};
        else
            q <= q;
    end
endmodule
