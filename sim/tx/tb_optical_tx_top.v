`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_optical_tx_top
//
// 통합 검증 목표:
//   1. Reset 후 IDLE 상태 확인
//   2. Character ID 8'h41 수락 확인
//   3. Preamble SYNC Symbol 정확히 4개 확인
//   4. Data Symbol 32개 확인
//   5. Data Symbol에서 원래 Frame D5 00 41 C0 복원
//   6. Carrier 출력 Rising Edge 수 확인
//   7. Frame 완료 후 tx_busy=0 / char_ready=1 확인
//   8. Frame ID 00 -> 01 증가 확인
//
// 알려진 기준 벡터:
//   Frame ID = 8'h00
//   DATA     = 8'h41
//   CRC      = 8'hC0
//
//   Full Data Frame = 32'hD50041C0
//
// 시뮬레이션 가속:
//   통합 TB에서는 이벤트 수를 줄이기 위해 1 MHz System Clock을 사용한다.
//   FS_HZ / Symbol / BFSK 주파수 자체는 실제 규격값을 그대로 유지하므로
//   각 Symbol 시간은 여전히 1.6 ms이고 Carrier 주파수도 10/20/25 kHz이다.
//
// 1.6 ms 기준:
//   SYNC 25 kHz -> Rising Edge 40회
//   BIT1 20 kHz -> Rising Edge 32회
//   BIT0 10 kHz -> Rising Edge 16회
//
// D50041C0:
//   '1' Bit = 9개
//   '0' Bit = 23개
//
// 전체 예상 optical_tx Rising Edge:
//   (4 * 40) + (9 * 32) + (23 * 16)
//   = 816회
//////////////////////////////////////////////////////////////////////////////////

module tb_optical_tx_top;

    localparam integer CLK_FREQ_HZ      = 1_000_000;
    localparam integer FS_HZ            = 160_000;
    localparam integer SYMBOL_SAMPLES   = 256;

    localparam integer F0_HZ            = 10_000;
    localparam integer F1_HZ            = 20_000;
    localparam integer FSYNC_HZ         = 25_000;

    localparam integer PREAMBLE_SYMBOLS = 4;

    localparam [1:0] SYMBOL_IDLE = 2'b00;
    localparam [1:0] SYMBOL_BIT0 = 2'b01;
    localparam [1:0] SYMBOL_BIT1 = 2'b10;
    localparam [1:0] SYMBOL_SYNC = 2'b11;

    localparam [31:0] EXPECTED_FRAME = 32'hD50041C0;
    localparam integer EXPECTED_OPTICAL_RISING = 816;

    reg        clk;
    reg        rst;

    reg  [7:0] char_id;
    reg        char_valid;

    wire       char_ready;
    wire       tx_busy;

    wire       optical_tx;
    wire       tx_enable;

    integer pass_count;
    integer fail_count;

    integer sync_symbol_count;
    integer data_symbol_count;
    integer frame_gen_start_count;
    integer frame_done_count;
    integer optical_rising_count;
    integer sequence_error_count;

    reg [31:0] captured_frame;

    reg data_phase_started;

    optical_tx_top #(
        .CLK_FREQ_HZ      (CLK_FREQ_HZ),
        .FS_HZ            (FS_HZ),
        .SYMBOL_SAMPLES   (SYMBOL_SAMPLES),

        .F0_HZ            (F0_HZ),
        .F1_HZ            (F1_HZ),
        .FSYNC_HZ         (FSYNC_HZ),

        .PREAMBLE_SYMBOLS (PREAMBLE_SYMBOLS),
        .SFD              (8'hD5)
    ) dut (
        .clk        (clk),
        .rst        (rst),

        .char_id    (char_id),
        .char_valid (char_valid),
        .char_ready (char_ready),
        .tx_busy    (tx_busy),

        .optical_tx (optical_tx),
        .tx_enable  (tx_enable)
    );

    // ============================================================
    // 1 MHz Clock
    // ============================================================
    initial begin
        clk = 1'b0;
        forever #500 clk = ~clk;
    end

    // ============================================================
    // Symbol Sequence Monitor
    // ============================================================
    always @(posedge dut.symbol_start) begin
        // Preamble 단계
        if (!data_phase_started) begin
            if (dut.symbol_type == SYMBOL_SYNC) begin
                sync_symbol_count = sync_symbol_count + 1;
            end
            else begin
                data_phase_started = 1'b1;

                if (sync_symbol_count != PREAMBLE_SYMBOLS) begin
                    $display(
                        "[FAIL] DATA started before SYNC x4 : sync_count=%0d",
                        sync_symbol_count
                    );
                    sequence_error_count = sequence_error_count + 1;
                end
            end
        end

        // Data Frame 단계
        if (dut.symbol_type == SYMBOL_BIT0 ||
            dut.symbol_type == SYMBOL_BIT1) begin

            captured_frame = {
                captured_frame[30:0],
                (dut.symbol_type == SYMBOL_BIT1)
            };

            data_symbol_count = data_symbol_count + 1;
        end
        else if (data_phase_started &&
                 dut.symbol_type == SYMBOL_SYNC) begin

            $display("[FAIL] Unexpected SYNC during DATA frame");
            sequence_error_count = sequence_error_count + 1;
        end
    end

    // ============================================================
    // Frame Control Monitor
    // ============================================================
    always @(posedge dut.frame_gen_start) begin
        frame_gen_start_count = frame_gen_start_count + 1;
    end

    always @(posedge dut.frame_done) begin
        frame_done_count = frame_done_count + 1;
    end

    // ============================================================
    // Optical Carrier Rising Edge Monitor
    // ============================================================
    always @(posedge optical_tx) begin
        if (!rst)
            optical_rising_count = optical_rising_count + 1;
    end

    task check_1bit;
        input actual;
        input expected;
        input [8*64-1:0] test_name;
    begin
        if (actual === expected) begin
            $display("[PASS] %0s", test_name);
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] %0s : actual=%b expected=%b",
                test_name,
                actual,
                expected
            );
            fail_count = fail_count + 1;
        end
    end
    endtask

    task check_8bit;
        input [7:0] actual;
        input [7:0] expected;
        input [8*64-1:0] test_name;
    begin
        if (actual === expected) begin
            $display(
                "[PASS] %0s : 0x%02h",
                test_name,
                actual
            );
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] %0s : actual=0x%02h expected=0x%02h",
                test_name,
                actual,
                expected
            );
            fail_count = fail_count + 1;
        end
    end
    endtask

    task check_32bit;
        input [31:0] actual;
        input [31:0] expected;
        input [8*64-1:0] test_name;
    begin
        if (actual === expected) begin
            $display(
                "[PASS] %0s : 0x%08h",
                test_name,
                actual
            );
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] %0s : actual=0x%08h expected=0x%08h",
                test_name,
                actual,
                expected
            );
            fail_count = fail_count + 1;
        end
    end
    endtask

    task check_integer;
        input integer actual;
        input integer expected;
        input [8*64-1:0] test_name;
    begin
        if (actual == expected) begin
            $display(
                "[PASS] %0s : %0d",
                test_name,
                actual
            );
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] %0s : actual=%0d expected=%0d",
                test_name,
                actual,
                expected
            );
            fail_count = fail_count + 1;
        end
    end
    endtask

    // ============================================================
    // Character 입력
    // ============================================================
    task send_character;
        input [7:0] data_value;
    begin
        wait (char_ready === 1'b1);

        @(negedge clk);
        char_id    = data_value;
        char_valid = 1'b1;

        @(negedge clk);
        char_valid = 1'b0;
    end
    endtask

    initial begin
        rst                   = 1'b1;
        char_id               = 8'h00;
        char_valid            = 1'b0;

        pass_count            = 0;
        fail_count            = 0;

        sync_symbol_count     = 0;
        data_symbol_count     = 0;
        frame_gen_start_count = 0;
        frame_done_count      = 0;
        optical_rising_count  = 0;
        sequence_error_count  = 0;

        captured_frame        = 32'h00000000;
        data_phase_started    = 1'b0;

        // ========================================================
        // TEST 1 : RESET / IDLE
        // ========================================================
        repeat (3) @(negedge clk);
        rst = 1'b0;

        @(posedge clk);
        #1;

        $display("----------------------------------------");
        $display("TEST 1 : RESET / IDLE");
        $display("----------------------------------------");

        check_1bit(char_ready, 1'b1, "Reset -> char_ready=1");
        check_1bit(tx_busy,    1'b0, "Reset -> tx_busy=0");
        check_1bit(optical_tx, 1'b0, "Reset -> optical_tx=0");
        check_1bit(tx_enable,  1'b0, "Reset -> tx_enable=0");

        // ========================================================
        // TEST 2 : FULL TX INTEGRATION
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 2 : FULL TX / DATA=0x41");
        $display("----------------------------------------");

        send_character(8'h41);

        wait (tx_busy === 1'b1);

        check_1bit(char_ready, 1'b0, "TX Busy -> char_ready=0");

        // 전체 Preamble + 32 Data Symbol 완료 대기
        wait (tx_busy === 1'b0);

        // 마지막 상태 변경 안정화
        @(posedge clk);
        #1;

        // ========================================================
        // 결과 확인
        // ========================================================
        $display("----------------------------------------");
        $display("INTEGRATION RESULT CHECK");
        $display("----------------------------------------");

        check_integer(
            sync_symbol_count,
            4,
            "Preamble SYNC symbol count"
        );

        check_integer(
            data_symbol_count,
            32,
            "Data symbol count"
        );

        check_32bit(
            captured_frame,
            EXPECTED_FRAME,
            "Recovered TX frame"
        );

        check_integer(
            frame_gen_start_count,
            1,
            "frame_gen_start pulse count"
        );

        check_integer(
            frame_done_count,
            1,
            "frame_done pulse count"
        );

        check_integer(
            optical_rising_count,
            EXPECTED_OPTICAL_RISING,
            "Total optical carrier rising-edge count"
        );

        check_integer(
            sequence_error_count,
            0,
            "Symbol sequence error count"
        );

        check_8bit(
            dut.frame_id,
            8'h01,
            "Frame complete -> frame_id=01"
        );

        check_8bit(
            dut.latched_char_id,
            8'h41,
            "Latched Character ID"
        );

        check_1bit(
            char_ready,
            1'b1,
            "Frame complete -> char_ready=1"
        );

        check_1bit(
            tx_busy,
            1'b0,
            "Frame complete -> tx_busy=0"
        );

        check_1bit(
            optical_tx,
            1'b0,
            "Frame complete -> optical_tx=0"
        );

        check_1bit(
            tx_enable,
            1'b0,
            "Frame complete -> tx_enable=0"
        );

        // ========================================================
        // FINAL
        // ========================================================
        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("OPTICAL TX TOP TEST RESULT : PASS");
        end
        else begin
            $display("OPTICAL TX TOP TEST RESULT : FAIL");
        end

        $display(
            "PASS = %0d, FAIL = %0d",
            pass_count,
            fail_count
        );

        $display("----------------------------------------");

        #2000;
        $finish;
    end

endmodule
