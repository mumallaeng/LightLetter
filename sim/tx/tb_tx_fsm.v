`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_tx_fsm
//
// 검증 항목:
//   1. Reset 후 IDLE / char_ready / tx_busy
//   2. char_valid && char_ready에서 Character ID Latch
//   3. tx_busy 동안 새로운 Character 입력 차단
//   4. sync_valid / sync_ready Handshake
//   5. SYNC Symbol 정확히 4개
//   6. Mapper Ready Stall 중 sync_valid 유지
//   7. 4번째 SYNC 완료 후 frame_gen_start 1 Clock Pulse
//   8. Frame 완료 후 IDLE 복귀
//   9. Frame ID 증가
//  10. Frame ID 8'hFF -> 8'h00 Roll-over
//////////////////////////////////////////////////////////////////////////////////

module tb_tx_fsm;

    localparam integer PREAMBLE_SYMBOLS = 4;

    reg        clk;
    reg        rst;

    reg  [7:0] char_id;
    reg        char_valid;
    wire       char_ready;
    wire       tx_busy;

    wire [7:0] latched_char_id;
    wire [7:0] frame_id;
    wire       frame_gen_start;
    reg        frame_done;

    wire       sync_valid;
    reg        sync_ready;

    reg        symbol_done;

    integer pass_count;
    integer fail_count;

    integer sync_handshake_count;
    integer frame_start_count;

    tx_fsm #(
        .PREAMBLE_SYMBOLS(PREAMBLE_SYMBOLS)
    ) dut (
        .clk            (clk),
        .rst            (rst),

        .char_id        (char_id),
        .char_valid     (char_valid),
        .char_ready     (char_ready),
        .tx_busy        (tx_busy),

        .latched_char_id(latched_char_id),
        .frame_id       (frame_id),
        .frame_gen_start(frame_gen_start),
        .frame_done     (frame_done),

        .sync_valid     (sync_valid),
        .sync_ready     (sync_ready),

        .symbol_done    (symbol_done)
    );

    // 100 MHz Clock
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    // 실제 SYNC Handshake 횟수 계수
    always @(posedge clk) begin
        if (!rst && sync_valid && sync_ready)
            sync_handshake_count = sync_handshake_count + 1;
    end

    // Frame Generator Start Pulse 횟수 계수
    always @(posedge frame_gen_start) begin
        frame_start_count = frame_start_count + 1;
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

    // ------------------------------------------------------------
    // CNN Character 입력
    // ------------------------------------------------------------
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

    // ------------------------------------------------------------
    // SYNC 요청 1개 처리
    //
    // ready_stall_cycles:
    //   Mapper가 준비되지 않은 상황을 모사한다.
    // ------------------------------------------------------------
    task complete_sync;
        input integer ready_stall_cycles;
        integer i;
    begin
        wait (sync_valid === 1'b1);

        // Ready Stall 동안 sync_valid이 유지되어야 한다.
        sync_ready = 1'b0;

        for (i = 0; i < ready_stall_cycles; i = i + 1) begin
            @(posedge clk);
            #1;

            if (sync_valid !== 1'b1) begin
                $display("[FAIL] sync_valid dropped during ready stall");
                fail_count = fail_count + 1;
            end
        end

        // SYNC 요청 Handshake
        @(negedge clk);
        sync_ready = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        sync_ready = 1'b0;

        // 실제 Carrier 동작 시간을 축약하여 symbol_done Pulse만 모사
        repeat (2) @(negedge clk);

        symbol_done = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        symbol_done = 1'b0;
    end
    endtask

    // ------------------------------------------------------------
    // Frame Generator 완료 Pulse
    // ------------------------------------------------------------
    task complete_frame;
    begin
        repeat (3) @(negedge clk);

        frame_done = 1'b1;

        @(posedge clk);
        #1;

        @(negedge clk);
        frame_done = 1'b0;

        // ST_FRAME_DONE 처리
        @(posedge clk);
        #1;
    end
    endtask

    // ------------------------------------------------------------
    // Roll-over 검증용 빠른 Frame 1회
    // ------------------------------------------------------------
    task quick_frame;
        input [7:0] data_value;
        integer n;
    begin
        send_character(data_value);

        for (n = 0; n < PREAMBLE_SYMBOLS; n = n + 1)
            complete_sync(0);

        // frame_gen_start Pulse 이후 SEND_FRAME 진입
        @(posedge clk);
        #1;

        complete_frame();
    end
    endtask

    initial begin
        rst                  = 1'b1;

        char_id              = 8'h00;
        char_valid           = 1'b0;

        frame_done           = 1'b0;

        sync_ready           = 1'b0;
        symbol_done          = 1'b0;

        pass_count           = 0;
        fail_count           = 0;

        sync_handshake_count = 0;
        frame_start_count    = 0;

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
        check_8bit(frame_id,   8'h00, "Reset -> frame_id=00");

        // ========================================================
        // TEST 2 : CHARACTER LATCH
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 2 : CHARACTER LATCH");
        $display("----------------------------------------");

        send_character(8'h41);

        @(posedge clk);
        #1;

        check_8bit(
            latched_char_id,
            8'h41,
            "char_id=41 latched"
        );

        check_1bit(
            char_ready,
            1'b0,
            "Busy -> char_ready=0"
        );

        check_1bit(
            tx_busy,
            1'b1,
            "Character accepted -> tx_busy=1"
        );

        // Busy 상태에서 다른 Character를 넣어도 기존 값 유지
        @(negedge clk);
        char_id    = 8'h55;
        char_valid = 1'b1;

        repeat (2) @(negedge clk);

        char_valid = 1'b0;

        check_8bit(
            latched_char_id,
            8'h41,
            "Busy input ignored"
        );

        // ========================================================
        // TEST 3 : PREAMBLE SYNC x4
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 3 : PREAMBLE SYNC x4");
        $display("----------------------------------------");

        sync_handshake_count = 0;
        frame_start_count    = 0;

        // 첫 번째 Symbol에서는 Ready Stall도 검증
        complete_sync(3);
        complete_sync(0);
        complete_sync(1);
        complete_sync(0);

        // 4번째 symbol_done 처리 직후 frame_gen_start가 High
        check_1bit(
            frame_gen_start,
            1'b1,
            "4th SYNC done -> frame_gen_start pulse"
        );

        check_integer(
            sync_handshake_count,
            4,
            "SYNC handshake count"
        );

        check_integer(
            frame_start_count,
            1,
            "frame_gen_start pulse count"
        );

        // 다음 Clock에서 Pulse가 내려가는지 확인
        @(posedge clk);
        #1;

        check_1bit(
            frame_gen_start,
            1'b0,
            "frame_gen_start returns 0"
        );

        // SEND_FRAME 구간에서는 추가 SYNC 요청 금지
        repeat (4) begin
            @(posedge clk);
            #1;

            if (sync_valid !== 1'b0) begin
                $display("[FAIL] Extra SYNC request during SEND_FRAME");
                fail_count = fail_count + 1;
            end
        end

        // ========================================================
        // TEST 4 : FRAME DONE / FRAME ID
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 4 : FRAME DONE / FRAME ID");
        $display("----------------------------------------");

        complete_frame();

        check_8bit(
            frame_id,
            8'h01,
            "Frame complete -> frame_id increment"
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

        // ========================================================
        // TEST 5 : FRAME ID ROLL-OVER
        //
        // 첫 Frame 완료 후 frame_id=01이므로
        // 추가 255 Frame 완료 후 00으로 돌아와야 한다.
        // ========================================================
        $display("----------------------------------------");
        $display("TEST 5 : FRAME ID ROLL-OVER");
        $display("----------------------------------------");

        repeat (255)
            quick_frame(8'h41);

        check_8bit(
            frame_id,
            8'h00,
            "Frame ID FF -> 00 rollover"
        );

        // ========================================================
        // FINAL
        // ========================================================
        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("TX FSM TEST RESULT : PASS");
        end
        else begin
            $display("TX FSM TEST RESULT : FAIL");
        end

        $display(
            "PASS = %0d, FAIL = %0d",
            pass_count,
            fail_count
        );

        $display("----------------------------------------");

        #20;
        $finish;
    end

endmodule
