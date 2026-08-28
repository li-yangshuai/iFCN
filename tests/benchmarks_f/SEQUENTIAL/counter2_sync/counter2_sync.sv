module counter2_sync (
    input  logic       clk,
    input  logic       rst,
    input  logic       en,
    output logic [1:0] q
);
    always_ff @(posedge clk) begin
        if (rst) begin
            q <= 2'b00;
        end else if (en) begin
            q <= q + 2'b01;
        end
    end
endmodule

