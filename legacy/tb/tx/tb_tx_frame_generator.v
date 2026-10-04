`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_tx_frame_generator
//
// Verification Targets:
//   1. SFD = 8'hD5
//   2. Frame ID transmission
//   3. DATA transmission
//   4. CRC-8 transmission
//   5. MSB-first bit order
//   6. valid/ready backpressure behavior
//
// Test Vector #1:
//   Frame ID = 8'h00
//   DATA     = 8'h41
//   CRC      = 8'hC0
//   Expected = 32'hD50041C0
//
// Test Vector #2:
//   Frame ID = 8'h12
//   DATA     = 8'h34
//   CRC      = 8'hF1
//   Expected = 32'hD51234F1
//////////////////////////////////////////////////////////////////////////////////

module tb_tx_frame_generator;

    reg        clk;
    reg        rst;

    reg        start;
    reg  [7:0] frame_id;
    reg  [7:0] data_in;

    wire       tx_bit;
    wire       tx_bit_valid;
    reg        tx_bit_ready;

    wire       frame_start;
    wire       frame_done;
    wire       busy;

    reg [31:0] captured_frame;
    integer    captured_bits;

    integer    pass_count;
    integer    fail_count;

    integer    frame_start_count;
    integer    frame_done_count;

    reg        stall_enable;

    tx_frame_generator dut (
        .clk          (clk),
        .rst          (rst),

        .start        (start),
        .frame_id     (frame_id),
        .data_in      (data_in),

        .tx_bit       (tx_bit),
        .tx_bit_valid (tx_bit_valid),
        .tx_bit_ready (tx_bit_ready),

        .frame_start  (frame_start),
        .frame_done   (frame_done),
        .busy         (busy)
    );

    // -------------------------------------------------------------------------
    // 100 MHz clock
    // -------------------------------------------------------------------------
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    // -------------------------------------------------------------------------
    // Backpressure generator
    // When enabled, ready alternates 1/0 while a frame is being transmitted.
    // -------------------------------------------------------------------------
    always @(negedge clk) begin
        if (rst) begin
            tx_bit_ready <= 1'b1;
        end
        else if (stall_enable && busy) begin
            tx_bit_ready <= ~tx_bit_ready;
        end
        else begin
            tx_bit_ready <= 1'b1;
        end
    end

    // -------------------------------------------------------------------------
    // Capture only successfully transferred bits.
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!rst) begin
            if (tx_bit_valid && tx_bit_ready) begin
                captured_frame = {
                    captured_frame[30:0],
                    tx_bit
                };

                captured_bits = captured_bits + 1;
            end
        end
    end

    always @(posedge frame_start) begin
        frame_start_count = frame_start_count + 1;
    end

    always @(posedge frame_done) begin
        frame_done_count = frame_done_count + 1;
    end

    // -------------------------------------------------------------------------
    // Start one frame.
    // -------------------------------------------------------------------------
    task send_frame;
        input [7:0] test_frame_id;
        input [7:0] test_data;
    begin
        @(negedge clk);

        frame_id = test_frame_id;
        data_in  = test_data;
        start    = 1'b1;

        @(negedge clk);
        start    = 1'b0;
    end
    endtask

    // -------------------------------------------------------------------------
    // Verify result.
    // -------------------------------------------------------------------------
    task check_frame;
        input [31:0] expected_frame;
        input integer expected_bits;
    begin
        #1;

        if (captured_frame === expected_frame) begin
            $display(
                "[PASS] FRAME = 0x%08h, Expected = 0x%08h",
                captured_frame,
                expected_frame
            );

            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] FRAME = 0x%08h, Expected = 0x%08h",
                captured_frame,
                expected_frame
            );

            fail_count = fail_count + 1;
        end

        if (captured_bits == expected_bits) begin
            $display(
                "[PASS] BIT COUNT = %0d",
                captured_bits
            );

            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] BIT COUNT = %0d, Expected = %0d",
                captured_bits,
                expected_bits
            );

            fail_count = fail_count + 1;
        end

        if (frame_start_count == 1) begin
            $display("[PASS] frame_start pulse count = 1");
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] frame_start pulse count = %0d",
                frame_start_count
            );

            fail_count = fail_count + 1;
        end

        if (frame_done_count == 1) begin
            $display("[PASS] frame_done pulse count = 1");
            pass_count = pass_count + 1;
        end
        else begin
            $display(
                "[FAIL] frame_done pulse count = %0d",
                frame_done_count
            );

            fail_count = fail_count + 1;
        end
    end
    endtask

    // -------------------------------------------------------------------------
    // Main test
    // -------------------------------------------------------------------------
    initial begin
        rst               = 1'b1;
        start             = 1'b0;
        frame_id          = 8'h00;
        data_in           = 8'h00;
        tx_bit_ready      = 1'b1;
        stall_enable      = 1'b0;

        captured_frame    = 32'h0000_0000;
        captured_bits     = 0;

        frame_start_count = 0;
        frame_done_count  = 0;

        pass_count        = 0;
        fail_count        = 0;

        // Reset
        repeat (3) @(negedge clk);
        rst = 1'b0;

        // ---------------------------------------------------------------------
        // TEST 1
        // Known project vector, no backpressure.
        // ---------------------------------------------------------------------
        $display("----------------------------------------");
        $display("TEST 1 : Frame ID=00, DATA=41");
        $display("----------------------------------------");

        captured_frame    = 32'h0000_0000;
        captured_bits     = 0;
        frame_start_count = 0;
        frame_done_count  = 0;
        stall_enable      = 1'b0;

        send_frame(8'h00, 8'h41);

        wait (frame_done == 1'b1);
        #1;

        check_frame(32'hD500_41C0, 32);

        // Wait until generator is fully idle.
        @(negedge clk);

        // ---------------------------------------------------------------------
        // TEST 2
        // Verify valid/ready backpressure.
        // ---------------------------------------------------------------------
        $display("----------------------------------------");
        $display("TEST 2 : Frame ID=12, DATA=34 + STALL");
        $display("----------------------------------------");

        captured_frame    = 32'h0000_0000;
        captured_bits     = 0;
        frame_start_count = 0;
        frame_done_count  = 0;
        stall_enable      = 1'b1;

        send_frame(8'h12, 8'h34);

        wait (frame_done == 1'b1);
        #1;

        stall_enable = 1'b0;

        check_frame(32'hD512_34F1, 32);

        // ---------------------------------------------------------------------
        // Final result
        // ---------------------------------------------------------------------
        $display("----------------------------------------");

        if (fail_count == 0) begin
            $display("TX FRAME GENERATOR TEST RESULT : PASS");
        end
        else begin
            $display("TX FRAME GENERATOR TEST RESULT : FAIL");
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
