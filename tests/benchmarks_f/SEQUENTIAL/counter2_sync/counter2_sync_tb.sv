`timescale 1ns/1ps

module counter2_sync_tb;
    logic       clk;
    logic       rst;
    logic       en;
    logic [1:0] q;
    integer     capture;

    counter2_sync dut (
        .clk(clk),
        .rst(rst),
        .en(en),
        .q(q)
    );

    task automatic capture_and_check(
        input logic       next_rst,
        input logic       next_en,
        input logic [1:0] expected_q
    );
        begin
            rst = next_rst;
            en  = next_en;
            #4 clk = 1'b1;
            #1;

            $display("%0d,%0b,%0b,%02b", capture, rst, en, q);
            if (q !== expected_q) begin
                $fatal(1,
                       "capture %0d: rst=%0b en=%0b expected=%02b got=%02b",
                       capture, rst, en, expected_q, q);
            end

            capture = capture + 1;
            #4 clk = 1'b0;
            #1;
        end
    endtask

    initial begin
        clk     = 1'b0;
        rst     = 1'b0;
        en      = 1'b0;
        capture = 0;

        $display("capture,rst,en,q");
        #1;

        capture_and_check(1'b1, 1'b0, 2'b00);
        capture_and_check(1'b1, 1'b1, 2'b00);
        capture_and_check(1'b0, 1'b1, 2'b01);
        capture_and_check(1'b0, 1'b1, 2'b10);
        capture_and_check(1'b0, 1'b0, 2'b10);
        capture_and_check(1'b0, 1'b1, 2'b11);
        capture_and_check(1'b0, 1'b1, 2'b00);
        capture_and_check(1'b1, 1'b1, 2'b00);
        capture_and_check(1'b0, 1'b0, 2'b00);
        capture_and_check(1'b0, 1'b1, 2'b01);

        $display("counter2_sync: PASS (%0d captures)", capture);
        $finish;
    end
endmodule

