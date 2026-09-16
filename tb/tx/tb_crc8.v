`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_crc8
//
// Test:
//   CRC-8
//   POLY   = 8'h07
//   INIT   = 8'h00
//   MSB First
//
// Test Vector:
//   Frame ID = 8'h00
//   DATA     = 8'h41
//   Expected = 8'hC0
//////////////////////////////////////////////////////////////////////////////////

module tb_crc8;

    reg        clk;
    reg        rst;
    reg        crc_init;
    reg  [7:0] data_in;
    reg        data_valid;

    wire [7:0] crc_out;

    integer pass_count;
    integer fail_count;

    crc8 #(
        .POLY(8'h07),
        .INIT(8'h00)
    ) dut (
        .clk        (clk),
        .rst        (rst),
        .crc_init   (crc_init),
        .data_in    (data_in),
        .data_valid (data_valid),
        .crc_out    (crc_out)
    );

    // 100 MHz clock
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task crc_initialize;
    begin
        @(negedge clk);
        crc_init = 1'b1;

        @(negedge clk);
        crc_init = 1'b0;
    end
    endtask

    task send_byte;
        input [7:0] byte_data;
    begin
        @(negedge clk);
        data_in    = byte_data;
        data_valid = 1'b1;

        @(negedge clk);
        data_valid = 1'b0;
    end
    endtask

    task check_crc;
        input [7:0] expected;
    begin
        #1;
        if (crc_out === expected) begin
            $display("[PASS] CRC = 0x%02h, Expected = 0x%02h",
                     crc_out, expected);
            pass_count = pass_count + 1;
        end
        else begin
            $display("[FAIL] CRC = 0x%02h, Expected = 0x%02h",
                     crc_out, expected);
            fail_count = fail_count + 1;
        end
    end
    endtask

    initial begin
        rst        = 1'b1;
        crc_init   = 1'b0;
        data_in    = 8'h00;
        data_valid = 1'b0;

        pass_count = 0;
        fail_count = 0;

        // Reset
        repeat (2) @(negedge clk);
        rst = 1'b0;

        // Initialize CRC
        crc_initialize();
        check_crc(8'h00);

        // Frame ID = 0x00
        send_byte(8'h00);
        check_crc(8'h00);

        // DATA = 0x41
        send_byte(8'h41);
        check_crc(8'hC0);

        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("CRC8 TEST RESULT : PASS");
        end
        else begin
            $display("CRC8 TEST RESULT : FAIL");
        end

        $display("PASS = %0d, FAIL = %0d",
                 pass_count, fail_count);
        $display("----------------------------------------");

        #20;
        $finish;
    end

endmodule
