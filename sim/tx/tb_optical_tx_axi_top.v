`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_optical_tx_axi_top
//
// TX-8 AXI4-Lite + Optical TX 전체 통합 검증
//
// 검증 흐름:
//   AXI DATA Write
//       ↓
//   AXI START Write
//       ↓
//   char_valid / char_ready
//       ↓
//   Preamble SYNC x4
//       ↓
//   32-bit Frame
//       ↓
//   BFSK Carrier
//       ↓
//   최종 READY 복귀
//
// 검증 항목:
//   1. Reset 후 STATUS = READY
//   2. AXI Write 0x41 -> START
//   3. BUSY / READY Status 변화
//   4. SYNC Symbol x4
//   5. Data Symbol 32개
//   6. Frame D5 00 41 C0 복원
//   7. Optical Rising Edge 816회
//   8. Busy 중 START Ignore
//   9. 완료 후 Frame ID 00 -> 01
//  10. AW First 방식으로 두 번째 Character 전체 송신
//  11. W First 방식으로 세 번째 Character 전체 송신
//  12. 연속 송신 시 Frame ID 증가
//  13. AXI Read / Write Response OKAY
//
// 시뮬레이션 가속:
//   실제 RTL Default Parameter는 다음과 같다.
//     FS_HZ    = 160 kHz
//     F0       = 10 kHz
//     F1       = 20 kHz
//     FSYNC    = 25 kHz
//     Symbol   = 1.6 ms
//
//   TX-8은 AXI + TX 통합 Handshake 검증이 목적이므로,
//   TB에서만 모든 주파수를 100배로 Scaling한다.
//     FS_HZ    = 16 MHz
//     F0       = 1 MHz
//     F1       = 2 MHz
//     FSYNC    = 2.5 MHz
//     Symbol   = 16 us
//
//   주파수 비율은 동일하므로 Symbol당 Rising Edge는 그대로 유지된다.
//     BIT0  = 16회
//     BIT1  = 32회
//     SYNC  = 40회
//
//   실제 10/20/25 kHz 및 1.6 ms Timing은 TX-4/TX-6에서 이미 검증하였다.
//////////////////////////////////////////////////////////////////////////////////

module tb_optical_tx_axi_top;

    localparam integer DATA_WIDTH = 32;
    localparam integer ADDR_WIDTH = 4;

    localparam [3:0] ADDR_TX_DATA   = 4'h0;
    localparam [3:0] ADDR_TX_CTRL   = 4'h4;
    localparam [3:0] ADDR_TX_STATUS = 4'h8;

    // 실제 System Clock은 100 MHz 유지
    localparam integer CLK_FREQ_HZ      = 100_000_000;

    // TX-8 TB 전용 100x Simulation Acceleration
    localparam integer FS_HZ            = 16_000_000;
    localparam integer SYMBOL_SAMPLES   = 256;
    localparam integer F0_HZ            = 1_000_000;
    localparam integer F1_HZ            = 2_000_000;
    localparam integer FSYNC_HZ         = 2_500_000;

    localparam integer PREAMBLE_SYMBOLS = 4;

    localparam [1:0] SYMBOL_BIT0 = 2'b01;
    localparam [1:0] SYMBOL_BIT1 = 2'b10;
    localparam [1:0] SYMBOL_SYNC = 2'b11;

    // ============================================================
    // AXI Signals
    // ============================================================
    reg                       clk;
    reg                       aresetn;

    reg  [ADDR_WIDTH-1:0]     awaddr;
    reg                       awvalid;
    wire                      awready;

    reg  [DATA_WIDTH-1:0]     wdata;
    reg  [(DATA_WIDTH/8)-1:0] wstrb;
    reg                       wvalid;
    wire                      wready;

    wire [1:0]                bresp;
    wire                      bvalid;
    reg                       bready;

    reg  [ADDR_WIDTH-1:0]     araddr;
    reg                       arvalid;
    wire                      arready;

    wire [DATA_WIDTH-1:0]     rdata;
    wire [1:0]                rresp;
    wire                      rvalid;
    reg                       rready;

    wire                      optical_tx;
    wire                      tx_enable;

    // ============================================================
    // Test Result / Monitor
    // ============================================================
    integer pass_count;
    integer fail_count;

    integer sync_symbol_count;
    integer data_symbol_count;
    integer optical_rising_count;
    integer char_valid_pulse_count;
    integer sequence_error_count;

    integer pulse_before;
    integer expected_rising;

    reg [31:0] captured_frame;
    reg [31:0] expected_frame;
    reg [31:0] read_value;

    reg        capture_enable;
    reg        data_phase_started;

    // ============================================================
    // DUT
    // ============================================================
    optical_tx_axi_top #(
        .C_S_AXI_DATA_WIDTH(DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH(ADDR_WIDTH),

        .CLK_FREQ_HZ      (CLK_FREQ_HZ),
        .FS_HZ            (FS_HZ),
        .SYMBOL_SAMPLES   (SYMBOL_SAMPLES),

        .F0_HZ            (F0_HZ),
        .F1_HZ            (F1_HZ),
        .FSYNC_HZ         (FSYNC_HZ),

        .PREAMBLE_SYMBOLS (PREAMBLE_SYMBOLS),
        .SFD              (8'hD5)
    ) dut (
        .s_axi_aclk    (clk),
        .s_axi_aresetn (aresetn),

        .s_axi_awaddr  (awaddr),
        .s_axi_awvalid (awvalid),
        .s_axi_awready (awready),

        .s_axi_wdata   (wdata),
        .s_axi_wstrb   (wstrb),
        .s_axi_wvalid  (wvalid),
        .s_axi_wready  (wready),

        .s_axi_bresp   (bresp),
        .s_axi_bvalid  (bvalid),
        .s_axi_bready  (bready),

        .s_axi_araddr  (araddr),
        .s_axi_arvalid (arvalid),
        .s_axi_arready (arready),

        .s_axi_rdata   (rdata),
        .s_axi_rresp   (rresp),
        .s_axi_rvalid  (rvalid),
        .s_axi_rready  (rready),

        .optical_tx    (optical_tx),
        .tx_enable     (tx_enable)
    );

    // ============================================================
    // 100 MHz Clock
    // ============================================================
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    // ============================================================
    // CRC-8 Reference Function
    // Poly = 0x07, Init = 0x00, MSB First
    // ============================================================
    function [7:0] calc_crc8;
        input [7:0] frame_id_value;
        input [7:0] data_value;

        reg [15:0] payload;
        reg [7:0]  crc;
        reg        feedback;
        integer    i;
    begin
        payload = {frame_id_value, data_value};
        crc     = 8'h00;

        for (i = 15; i >= 0; i = i - 1) begin
            feedback = crc[7] ^ payload[i];

            if (feedback)
                crc = (crc << 1) ^ 8'h07;
            else
                crc = (crc << 1);
        end

        calc_crc8 = crc;
    end
    endfunction

    // ============================================================
    // 한 Frame에서 기대되는 Optical Rising Edge 계산
    //
    // Preamble:
    //   4 x SYNC = 4 x 40 = 160
    //
    // Data:
    //   BIT0 = 16 Rising Edge / Symbol
    //   BIT1 = 32 Rising Edge / Symbol
    // ============================================================
    function integer calc_expected_rising;
        input [31:0] frame_value;

        integer i;
        integer total;
    begin
        total = 160;

        for (i = 0; i < 32; i = i + 1) begin
            if (frame_value[i])
                total = total + 32;
            else
                total = total + 16;
        end

        calc_expected_rising = total;
    end
    endfunction

    // ============================================================
    // Internal Character Event Monitor
    // ============================================================
    always @(posedge clk) begin
        if (aresetn && dut.char_valid)
            char_valid_pulse_count = char_valid_pulse_count + 1;
    end

    // ============================================================
    // Symbol Sequence Monitor
    // ============================================================
    always @(posedge dut.U_OPTICAL_TX_TOP.symbol_start) begin
        if (capture_enable) begin
            if (!data_phase_started) begin
                if (dut.U_OPTICAL_TX_TOP.symbol_type == SYMBOL_SYNC) begin
                    sync_symbol_count = sync_symbol_count + 1;
                end
                else begin
                    data_phase_started = 1'b1;

                    if (sync_symbol_count != PREAMBLE_SYMBOLS)
                        sequence_error_count = sequence_error_count + 1;
                end
            end

            if ((dut.U_OPTICAL_TX_TOP.symbol_type == SYMBOL_BIT0) ||
                (dut.U_OPTICAL_TX_TOP.symbol_type == SYMBOL_BIT1)) begin

                captured_frame = {
                    captured_frame[30:0],
                    (dut.U_OPTICAL_TX_TOP.symbol_type == SYMBOL_BIT1)
                };

                data_symbol_count = data_symbol_count + 1;
            end
            else if (data_phase_started &&
                     (dut.U_OPTICAL_TX_TOP.symbol_type == SYMBOL_SYNC)) begin

                sequence_error_count = sequence_error_count + 1;
            end
        end
    end

    // ============================================================
    // Optical Rising Edge Monitor
    // ============================================================
    always @(posedge optical_tx) begin
        if (aresetn && capture_enable)
            optical_rising_count = optical_rising_count + 1;
    end

    // ============================================================
    // Check Tasks
    // ============================================================
    task check_1bit;
        input actual;
        input expected;
        input [8*72-1:0] test_name;
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
        input [8*72-1:0] test_name;
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
        input [8*72-1:0] test_name;
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
        input [8*72-1:0] test_name;
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
    // AXI Write - AW/W 동시
    // ============================================================
    task axi_write_simultaneous;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
    begin
        @(negedge clk);

        awaddr  = address;
        awvalid = 1'b1;

        wdata   = data;
        wstrb   = 4'hF;
        wvalid  = 1'b1;

        // AW/W가 모두 Ready인 Rising Edge까지 유지
        while (!(awready && wready))
            @(negedge clk);

        @(posedge clk);
        #1;

        @(negedge clk);
        awvalid = 1'b0;
        wvalid  = 1'b0;

        bready = 1'b1;

        while (!bvalid)
            @(negedge clk);

        check_1bit(
            (bresp == 2'b00),
            1'b1,
            "AXI simultaneous write response OKAY"
        );

        @(negedge clk);
        bready = 1'b0;
    end
    endtask

    // ============================================================
    // AXI Write - AW First
    // ============================================================
    task axi_write_aw_first;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
    begin
        // Address 먼저 전달
        @(negedge clk);
        awaddr  = address;
        awvalid = 1'b1;

        @(posedge clk);
        while (!awready)
            @(posedge clk);

        @(negedge clk);
        awvalid = 1'b0;

        // 일부 Clock 뒤 Data 전달
        repeat (2) @(negedge clk);

        wdata  = data;
        wstrb  = 4'hF;
        wvalid = 1'b1;

        @(posedge clk);
        while (!wready)
            @(posedge clk);

        @(negedge clk);
        wvalid = 1'b0;

        bready = 1'b1;

        while (!bvalid)
            @(negedge clk);

        check_1bit(
            (bresp == 2'b00),
            1'b1,
            "AXI AW-first write response OKAY"
        );

        @(negedge clk);
        bready = 1'b0;
    end
    endtask

    // ============================================================
    // AXI Write - W First
    // ============================================================
    task axi_write_w_first;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
    begin
        // Data 먼저 전달
        @(negedge clk);
        wdata  = data;
        wstrb  = 4'hF;
        wvalid = 1'b1;

        @(posedge clk);
        while (!wready)
            @(posedge clk);

        @(negedge clk);
        wvalid = 1'b0;

        // 일부 Clock 뒤 Address 전달
        repeat (2) @(negedge clk);

        awaddr  = address;
        awvalid = 1'b1;

        @(posedge clk);
        while (!awready)
            @(posedge clk);

        @(negedge clk);
        awvalid = 1'b0;

        bready = 1'b1;

        while (!bvalid)
            @(negedge clk);

        check_1bit(
            (bresp == 2'b00),
            1'b1,
            "AXI W-first write response OKAY"
        );

        @(negedge clk);
        bready = 1'b0;
    end
    endtask

    // ============================================================
    // AXI Read
    // ============================================================
    task axi_read;
        input  [ADDR_WIDTH-1:0] address;
        output [DATA_WIDTH-1:0] data;
    begin
        @(negedge clk);

        araddr  = address;
        arvalid = 1'b1;

        @(posedge clk);
        while (!arready)
            @(posedge clk);

        @(negedge clk);
        arvalid = 1'b0;

        rready = 1'b1;

        while (!rvalid)
            @(negedge clk);

        data = rdata;

        check_1bit(
            (rresp == 2'b00),
            1'b1,
            "AXI read response OKAY"
        );

        @(negedge clk);
        rready = 1'b0;
    end
    endtask

    // ============================================================
    // Frame Monitor 초기화
    // ============================================================
    task clear_frame_monitor;
    begin
        sync_symbol_count    = 0;
        data_symbol_count    = 0;
        optical_rising_count = 0;
        sequence_error_count = 0;

        captured_frame       = 32'h00000000;
        data_phase_started   = 1'b0;
    end
    endtask

    // ============================================================
    // 한 Frame 결과 검증
    // ============================================================
    task check_frame_result;
        input [7:0] expected_frame_id;
        input [7:0] expected_data;
    begin
        expected_frame = {
            8'hD5,
            expected_frame_id,
            expected_data,
            calc_crc8(expected_frame_id, expected_data)
        };

        expected_rising = calc_expected_rising(expected_frame);

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
            expected_frame,
            "Recovered TX frame"
        );

        check_integer(
            optical_rising_count,
            expected_rising,
            "Optical carrier rising-edge count"
        );

        check_integer(
            sequence_error_count,
            0,
            "Symbol sequence error count"
        );

        check_8bit(
            dut.U_OPTICAL_TX_TOP.frame_id,
            expected_frame_id + 1'b1,
            "Frame ID increment"
        );

        check_1bit(
            dut.char_ready,
            1'b1,
            "Frame complete -> READY=1"
        );

        check_1bit(
            dut.tx_busy,
            1'b0,
            "Frame complete -> BUSY=0"
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
    end
    endtask

    initial begin
        // ========================================================
        // 초기값
        // ========================================================
        aresetn = 1'b0;

        awaddr  = {ADDR_WIDTH{1'b0}};
        awvalid = 1'b0;

        wdata   = {DATA_WIDTH{1'b0}};
        wstrb   = {(DATA_WIDTH/8){1'b0}};
        wvalid  = 1'b0;

        bready  = 1'b0;

        araddr  = {ADDR_WIDTH{1'b0}};
        arvalid = 1'b0;

        rready  = 1'b0;

        pass_count             = 0;
        fail_count             = 0;
        char_valid_pulse_count = 0;

        capture_enable         = 1'b0;
        data_phase_started     = 1'b0;

        clear_frame_monitor();

        // ========================================================
        // TEST 1 : RESET / IDLE STATUS
        // ========================================================
        repeat (5) @(negedge clk);
        aresetn = 1'b1;

        repeat (2) @(posedge clk);
        #1;

        $display("----------------------------------------");
        $display("TEST 1 : RESET / AXI STATUS");
        $display("----------------------------------------");

        axi_read(ADDR_TX_STATUS, read_value);

        check_32bit(
            read_value,
            32'h00000001,
            "Reset -> STATUS READY=1 BUSY=0"
        );

        check_1bit(
            optical_tx,
            1'b0,
            "Reset -> optical_tx=0"
        );

        check_1bit(
            tx_enable,
            1'b0,
            "Reset -> tx_enable=0"
        );

        // ========================================================
        // TEST 2 : FRAME #0 / Simultaneous AW+W
        // DATA = 0x41
        // Expected = D5 00 41 C0
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 2 : FRAME #0 / DATA=0x41");
        $display("----------------------------------------");

        clear_frame_monitor();
        capture_enable = 1'b1;

        axi_write_simultaneous(
            ADDR_TX_DATA,
            32'h00000041
        );

        check_8bit(
            dut.char_id,
            8'h41,
            "AXI DATA -> char_id=41"
        );

        pulse_before = char_valid_pulse_count;

        axi_write_simultaneous(
            ADDR_TX_CTRL,
            32'h00000001
        );

        wait (dut.tx_busy === 1'b1);

        check_integer(
            char_valid_pulse_count,
            pulse_before + 1,
            "START -> char_valid pulse count +1"
        );

        axi_read(ADDR_TX_STATUS, read_value);

        check_32bit(
            read_value,
            32'h00000002,
            "During TX -> STATUS READY=0 BUSY=1"
        );

        // Busy 중 START가 무시되는지 확인
        pulse_before = char_valid_pulse_count;

        axi_write_simultaneous(
            ADDR_TX_CTRL,
            32'h00000001
        );

        repeat (3) @(posedge clk);

        check_integer(
            char_valid_pulse_count,
            pulse_before,
            "Busy START ignored"
        );

        // 첫 번째 Frame 완료 대기
        wait (dut.tx_busy === 1'b0);
        repeat (2) @(posedge clk);

        capture_enable = 1'b0;

        check_frame_result(
            8'h00,
            8'h41
        );

        axi_read(ADDR_TX_STATUS, read_value);

        check_32bit(
            read_value,
            32'h00000001,
            "Frame #0 complete -> STATUS READY"
        );

        // ========================================================
        // TEST 3 : FRAME #1 / AW First
        // DATA = 0x42
        // Expected CRC = DC
        // Expected Frame = D5 01 42 DC
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 3 : FRAME #1 / AW FIRST / DATA=0x42");
        $display("----------------------------------------");

        clear_frame_monitor();
        capture_enable = 1'b1;

        axi_write_aw_first(
            ADDR_TX_DATA,
            32'h00000042
        );

        axi_write_aw_first(
            ADDR_TX_CTRL,
            32'h00000001
        );

        wait (dut.tx_busy === 1'b1);
        wait (dut.tx_busy === 1'b0);

        repeat (2) @(posedge clk);

        capture_enable = 1'b0;

        check_frame_result(
            8'h01,
            8'h42
        );

        // ========================================================
        // TEST 4 : FRAME #2 / W First
        // DATA = 0x43
        // Expected CRC = E4
        // Expected Frame = D5 02 43 E4
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 4 : FRAME #2 / W FIRST / DATA=0x43");
        $display("----------------------------------------");

        clear_frame_monitor();
        capture_enable = 1'b1;

        axi_write_w_first(
            ADDR_TX_DATA,
            32'h00000043
        );

        axi_write_w_first(
            ADDR_TX_CTRL,
            32'h00000001
        );

        wait (dut.tx_busy === 1'b1);
        wait (dut.tx_busy === 1'b0);

        repeat (2) @(posedge clk);

        capture_enable = 1'b0;

        check_frame_result(
            8'h02,
            8'h43
        );

        // ========================================================
        // FINAL
        // ========================================================
        $display("----------------------------------------");

        if (fail_count == 0)
            $display("OPTICAL TX AXI TOP TEST RESULT : PASS");
        else
            $display("OPTICAL TX AXI TOP TEST RESULT : FAIL");

        $display(
            "PASS = %0d, FAIL = %0d",
            pass_count,
            fail_count
        );

        $display("----------------------------------------");

        #100;
        $finish;
    end

endmodule
