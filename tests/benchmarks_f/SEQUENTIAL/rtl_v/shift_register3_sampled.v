// Sampled-state adapter for a three-bit SISO latch chain with a shared clock.
//
// sample_tick advances one observation boundary and is not a logical circuit
// input.  At an asserted logical_clk boundary the three stages shift
// simultaneously; otherwise every stage holds.  This captures the published
// boundary-state truth contract, not transparent-window waveform timing.
module shift_register3_sampled (
    input  wire       sample_tick,
    input  wire       logical_clk,
    input  wire       serial_in,
    output wire       serial_out,
    output reg  [2:0] q
);
    assign serial_out = q[2];

    always @(posedge sample_tick) begin
        if (logical_clk)
            q <= {q[1:0], serial_in};
        else
            q <= q;
    end
endmodule
