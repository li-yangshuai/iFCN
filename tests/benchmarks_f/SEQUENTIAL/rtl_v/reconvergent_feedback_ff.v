// A state output fans out into two logic cones and reconverges at next_q.
// QCA P&R purpose: require absolute-epoch alignment at a reconvergent gate.
module reconvergent_feedback_ff (
    input  wire clk,
    input  wire rst,
    input  wire a,
    input  wire b,
    output reg  q
);
    wire xor_branch;
    wire and_branch;
    wire next_q;

    assign xor_branch = q ^ a;
    assign and_branch = q & b;
    assign next_q     = xor_branch ^ and_branch;

    always @(posedge clk) begin
        if (rst)
            q <= 1'b0;
        else
            q <= next_q;
    end
endmodule
