`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_bfsk_mapper
//
// 검증 항목:
//   1. Reset 후 IDLE
//   2. tx_bit=0 -> BIT0
//   3. tx_bit=1 -> BIT1
//   4. sync_valid -> SYNC
//   5. symbol_start 1-clock pulse
//   6. symbol_done 전까지 symbol_type/symbol_valid 유지
//   7. symbol_done 후 IDLE 복귀
//   8. SYNC와 DATA 동시 요청 시 SYNC 우선
//////////////////////////////////////////////////////////////////////////////////

module tb_bfsk_mapper;

    localparam [1:0] SYMBOL_IDLE = 2'b00;
    localparam [1:0] SYMBOL_BIT0 = 2'b01;
    localparam [1:0] SYMBOL_BIT1 = 2'b10;
    localparam [1:0] SYMBOL_SYNC = 2'b11;

    reg        clk;
    reg        rst;

    reg        tx_bit;
    reg        tx_bit_valid;
    wire       tx_bit_ready;

    reg        sync_valid;
    wire       sync_ready;

    wire [1:0] symbol_type;
    wire       symbol_valid;
    wire       symbol_start;
    reg        symbol_done;

    integer pass_count;
    integer fail_count;

    bfsk_mapper dut (
        .clk          (clk),
        .rst          (rst),
        .tx_bit       (tx_bit),
        .tx_bit_valid (tx_bit_valid),
        .tx_bit_ready (tx_bit_ready),
        .sync_valid   (sync_valid),
        .sync_ready   (sync_ready),
        .symbol_type  (symbol_type),
        .symbol_valid (symbol_valid),
        .symbol_start (symbol_start),
        .symbol_done  (symbol_done)
    );

    // 100 MHz Clock
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task check_1bit;
        input actual;
        input expected;
        input [8*48-1:0] test_name;
    begin
        if (actual === expected) begin
            $display("[PASS] %0s", test_name);
            pass_count = pass_count + 1;
        end
        else begin
            $display("[FAIL] %0s : actual=%b expected=%b", test_name, actual, expected);
            fail_count = fail_count + 1;
        end
    end
    endtask

    task check_2bit;
        input [1:0] actual;
        input [1:0] expected;
        input [8*48-1:0] test_name;
    begin
        if (actual === expected) begin
            $display("[PASS] %0s", test_name);
            pass_count = pass_count + 1;
        end
        else begin
            $display("[FAIL] %0s : actual=%b expected=%b", test_name, actual, expected);
            fail_count = fail_count + 1;
        end
    end
    endtask

    task request_data;
        input bit_value;
    begin
        @(negedge clk);
        tx_bit       = bit_value;
        tx_bit_valid = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        tx_bit_valid = 1'b0;
    end
    endtask

    task request_sync;
    begin
        @(negedge clk);
        sync_valid = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        sync_valid = 1'b0;
    end
    endtask

    task finish_symbol;
    begin
        @(negedge clk);
        symbol_done = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        symbol_done = 1'b0;
    end
    endtask

    initial begin
        rst          = 1'b1;
        tx_bit       = 1'b0;
        tx_bit_valid = 1'b0;
        sync_valid   = 1'b0;
        symbol_done  = 1'b0;

        pass_count   = 0;
        fail_count   = 0;

        repeat (3) @(negedge clk);
        rst = 1'b0;

        @(posedge clk);
        #1;

        $display("----------------------------------------");
        $display("TEST 1 : RESET / IDLE");
        $display("----------------------------------------");

        check_2bit(symbol_type, SYMBOL_IDLE, "Reset -> IDLE");
        check_1bit(symbol_valid, 1'b0, "Reset -> symbol_valid=0");
        check_1bit(tx_bit_ready, 1'b1, "IDLE -> tx_bit_ready=1");
        check_1bit(sync_ready, 1'b1, "IDLE -> sync_ready=1");

        $display("----------------------------------------");
        $display("TEST 2 : BIT0");
        $display("----------------------------------------");

        request_data(1'b0);

        check_2bit(symbol_type, SYMBOL_BIT0, "tx_bit=0 -> BIT0");
        check_1bit(symbol_valid, 1'b1, "BIT0 -> symbol_valid=1");
        check_1bit(symbol_start, 1'b1, "BIT0 -> symbol_start pulse");

        @(posedge clk);
        #1;

        check_1bit(symbol_start, 1'b0, "BIT0 -> symbol_start returns 0");
        check_2bit(symbol_type, SYMBOL_BIT0, "BIT0 held before symbol_done");
        check_1bit(tx_bit_ready, 1'b0, "Busy -> tx_bit_ready=0");

        finish_symbol();

        check_2bit(symbol_type, SYMBOL_IDLE, "BIT0 done -> IDLE");
        check_1bit(symbol_valid, 1'b0, "BIT0 done -> symbol_valid=0");

        $display("----------------------------------------");
        $display("TEST 3 : BIT1");
        $display("----------------------------------------");

        request_data(1'b1);

        check_2bit(symbol_type, SYMBOL_BIT1, "tx_bit=1 -> BIT1");
        check_1bit(symbol_valid, 1'b1, "BIT1 -> symbol_valid=1");

        finish_symbol();

        check_2bit(symbol_type, SYMBOL_IDLE, "BIT1 done -> IDLE");

        $display("----------------------------------------");
        $display("TEST 4 : SYNC");
        $display("----------------------------------------");

        request_sync();

        check_2bit(symbol_type, SYMBOL_SYNC, "sync_valid -> SYNC");
        check_1bit(symbol_valid, 1'b1, "SYNC -> symbol_valid=1");
        check_1bit(sync_ready, 1'b0, "SYNC active -> sync_ready=0");

        finish_symbol();

        check_2bit(symbol_type, SYMBOL_IDLE, "SYNC done -> IDLE");

        $display("----------------------------------------");
        $display("TEST 5 : SYNC PRIORITY");
        $display("----------------------------------------");

        @(negedge clk);
        tx_bit       = 1'b1;
        tx_bit_valid = 1'b1;
        sync_valid   = 1'b1;

        #1;
        check_1bit(tx_bit_ready, 1'b0, "SYNC request blocks DATA ready");
        check_1bit(sync_ready, 1'b1, "SYNC request accepted while IDLE");

        @(posedge clk);
        #1;

        check_2bit(symbol_type, SYMBOL_SYNC, "SYNC has priority over DATA");
        check_1bit(symbol_start, 1'b1, "Priority SYNC -> symbol_start pulse");

        @(negedge clk);
        tx_bit_valid = 1'b0;
        sync_valid   = 1'b0;

        finish_symbol();

        check_2bit(symbol_type, SYMBOL_IDLE, "Priority SYNC done -> IDLE");

        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("BFSK MAPPER TEST RESULT : PASS");
        end
        else begin
            $display("BFSK MAPPER TEST RESULT : FAIL");
        end

        $display("PASS = %0d, FAIL = %0d", pass_count, fail_count);
        $display("----------------------------------------");

        #20;
        $finish;
    end

endmodule
