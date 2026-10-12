`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_bfsk_carrier_gen
//
// 검증 항목:
//   1. Reset 후 optical_tx=0, tx_enable=0
//   2. BIT0  -> 10 kHz
//   3. BIT1  -> 20 kHz
//   4. SYNC  -> 25 kHz
//   5. IDLE  -> No Carrier
//   6. 각 Symbol 길이 = 1.6 ms
//   7. symbol_done = 1 Clock Pulse
//
// 100 MHz 기준:
//   Symbol Clock 수 = 160,000
//
//   10 kHz / 1.6 ms -> 16 cycles
//   20 kHz / 1.6 ms -> 32 cycles
//   25 kHz / 1.6 ms -> 40 cycles
//////////////////////////////////////////////////////////////////////////////////

module tb_bfsk_carrier_gen;

    localparam integer CLK_FREQ_HZ = 100_000_000;
    localparam integer FS_HZ = 160_000;
    localparam integer SYMBOL_SAMPLES = 256;

    localparam [1:0] SYMBOL_IDLE = 2'b00;
    localparam [1:0] SYMBOL_BIT0 = 2'b01;
    localparam [1:0] SYMBOL_BIT1 = 2'b10;
    localparam [1:0] SYMBOL_SYNC = 2'b11;

    localparam integer SYMBOL_CYCLES = CLK_FREQ_HZ / (FS_HZ / SYMBOL_SAMPLES);

    reg           clk;
    reg           rst;

    reg     [1:0] symbol_type;
    reg           symbol_valid;
    reg           symbol_start;

    wire          symbol_done;
    wire          optical_tx;
    wire          tx_enable;

    integer       pass_count;
    integer       fail_count;

    integer       clock_count;
    integer       rising_count;
    integer       done_pulse_count;

    reg           optical_tx_d;

    bfsk_carrier_gen #(
        .CLK_FREQ_HZ   (CLK_FREQ_HZ),
        .FS_HZ         (FS_HZ),
        .SYMBOL_SAMPLES(SYMBOL_SAMPLES),
        .F0_HZ         (10_000),
        .F1_HZ         (20_000),
        .FSYNC_HZ      (25_000)
    ) dut (
        .clk(clk),
        .rst(rst),

        .symbol_type (symbol_type),
        .symbol_valid(symbol_valid),
        .symbol_start(symbol_start),

        .symbol_done(symbol_done),

        .optical_tx(optical_tx),
        .tx_enable (tx_enable)
    );

    // 100 MHz Clock
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task check_integer;
        input integer actual;
        input integer expected;
        input [8*64-1:0] test_name;
        begin
            if (actual == expected) begin
                $display("[PASS] %0s : %0d", test_name, actual);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=%0d expected=%0d", test_name,
                         actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    task check_1bit;
        input actual;
        input expected;
        input [8*64-1:0] test_name;
        begin
            if (actual === expected) begin
                $display("[PASS] %0s", test_name);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=%b expected=%b", test_name,
                         actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    // -------------------------------------------------------------------------
    // 한 Symbol을 시작하고 완료될 때까지
    // Clock 수와 optical_tx Rising Edge 수를 측정한다.
    // -------------------------------------------------------------------------
    task run_symbol;
        input [1:0] test_symbol;
        input integer expected_rising;
        input expected_enable;
        input [8*64-1:0] test_name;
        begin
            clock_count      = 0;
            rising_count     = 0;
            done_pulse_count = 0;
            optical_tx_d     = 1'b0;

            // Symbol 요청
            @(negedge clk);
            symbol_type  = test_symbol;
            symbol_valid = 1'b1;
            symbol_start = 1'b1;

            @(negedge clk);
            symbol_start = 1'b0;

            // Carrier Generator가 Symbol을 수락한 뒤 출력 상태 확인
            #1;
            check_1bit(tx_enable, expected_enable, "tx_enable check");

            // symbol_done 발생까지 매 Clock 관찰
            while (symbol_done !== 1'b1) begin
                @(posedge clk);
                #1;

                clock_count = clock_count + 1;

                if (!optical_tx_d && optical_tx)
                    rising_count = rising_count + 1;

                optical_tx_d = optical_tx;
            end

            done_pulse_count = done_pulse_count + 1;

            // 요청 해제
            @(negedge clk);
            symbol_valid = 1'b0;

            // symbol_done이 다음 Clock에서 Low로 복귀하는지 확인
            @(posedge clk);
            #1;

            if (symbol_done) done_pulse_count = done_pulse_count + 1;

            $display("----------------------------------------");
            $display("%0s", test_name);
            $display("----------------------------------------");

            check_integer(clock_count, SYMBOL_CYCLES,
                          "Symbol length clock count");

            check_integer(rising_count, expected_rising,
                          "Carrier rising-edge count");

            check_integer(done_pulse_count, 1, "symbol_done pulse count");

            check_1bit(optical_tx, 1'b0, "Symbol done -> optical_tx=0");

            check_1bit(tx_enable, 1'b0, "Symbol done -> tx_enable=0");
        end
    endtask

    initial begin
        rst          = 1'b1;

        symbol_type  = SYMBOL_IDLE;
        symbol_valid = 1'b0;
        symbol_start = 1'b0;

        pass_count   = 0;
        fail_count   = 0;

        repeat (3) @(negedge clk);
        rst = 1'b0;

        @(posedge clk);
        #1;

        $display("----------------------------------------");
        $display("TEST 1 : RESET / IDLE");
        $display("----------------------------------------");

        check_1bit(optical_tx, 1'b0, "Reset -> optical_tx=0");
        check_1bit(tx_enable, 1'b0, "Reset -> tx_enable=0");
        check_1bit(symbol_done, 1'b0, "Reset -> symbol_done=0");

        // 1.6 ms 동안 10 kHz = 16 cycles
        run_symbol(SYMBOL_BIT0, 16, 1'b1, "TEST 2 : BIT0 / 10 kHz");

        // 1.6 ms 동안 20 kHz = 32 cycles
        run_symbol(SYMBOL_BIT1, 32, 1'b1, "TEST 3 : BIT1 / 20 kHz");

        // 1.6 ms 동안 25 kHz = 40 cycles
        run_symbol(SYMBOL_SYNC, 40, 1'b1, "TEST 4 : SYNC / 25 kHz");

        // IDLE은 1.6 ms 동안 Carrier가 없어야 한다.
        run_symbol(SYMBOL_IDLE, 0, 1'b0, "TEST 5 : IDLE / NO CARRIER");

        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("BFSK CARRIER GENERATOR TEST RESULT : PASS");
        end else begin
            $display("BFSK CARRIER GENERATOR TEST RESULT : FAIL");
        end

        $display("PASS = %0d, FAIL = %0d", pass_count, fail_count);

        $display("----------------------------------------");

        #20;
        $finish;
    end

endmodule
