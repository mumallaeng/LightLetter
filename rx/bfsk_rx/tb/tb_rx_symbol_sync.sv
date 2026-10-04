`timescale 1ns/1ps

// -----------------------------------------------------------------------------
// Testbench: rx_symbol_sync
//
// Each transmitted BFSK symbol occupies two consecutive 128-sample FFT
// blocks.  After at least eight SYNC blocks, the DUT stores 17 data-frequency
// blocks and evaluates the two possible block pairings:
//   Phase A: [0,1], [2,3], ... [14,15]
//   Phase B: [1,2], [3,4], ... [15,16]
// The expected D5 SFD powers are accumulated for both candidates.  The higher
// scoring phase is selected, its eight symbol pairs are classified, and the
// resulting byte must equal 0xD5 before frame_start is asserted.  The verified
// SFD is then replayed to rx_frame_decoder and payload pairs are forwarded.
//
// Planned functional coverage contains eleven behaviors: short SYNC reject,
// continuous SYNC wait, both phase choices, weak-half summation, BIT0/BIT1
// payload output, frame_finish recovery, changed-SFD rejection, ambiguous-pair
// rejection, score-tie rejection, and invalid-payload abort.
// -----------------------------------------------------------------------------
module rx_symbol_sync_coverage_tb;
    localparam MAG_W = 40;
    localparam [1:0] B0 = 2'b00, B1 = 2'b01;
    localparam [1:0] SYNC = 2'b10, INV = 2'b11;
    localparam [7:0] D5 = 8'hD5;

    reg clk = 0;
    reg rst_n = 0;
    reg [1:0] block_code = INV;
    reg block_code_valid = 0;
    reg [MAG_W-1:0] p8 = 0, p16 = 0, p20 = 0;
    reg fft_block_done = 0;
    reg frame_finish = 0;

    wire [1:0] symbol_code;
    wire symbol_valid, frame_start, frame_abort;

    integer failures = 0;
    integer coverage_hits = 0;
    integer i;

    rx_symbol_sync #(
        .MAG_W(MAG_W),
        .SYNC_MIN_BLOCKS(8),
        .SCORE_MARGIN(0)
    ) dut (
        .clk(clk), .rst_n(rst_n),
        .block_code(block_code), .block_code_valid(block_code_valid),
        .bin8_power(p8), .bin16_power(p16), .bin20_power(p20),
        .fft_block_done(fft_block_done), .frame_finish(frame_finish),
        .symbol_code(symbol_code), .symbol_valid(symbol_valid),
        .frame_start(frame_start), .frame_abort(frame_abort)
    );

    always #5 clk = ~clk;

    task reset_dut;
    begin
        rst_n = 0;
        block_code_valid = 0;
        fft_block_done = 0;
        frame_finish = 0;
        repeat (2) @(posedge clk);
        #1 rst_n = 1;
        @(posedge clk);
        #1;
    end
    endtask

    task code_pulse;
        input [1:0] code_value;
        input integer v8;
        input integer v16;
        input integer v20;
    begin
        block_code = code_value;
        p8 = v8; p16 = v16; p20 = v20;
        block_code_valid = 1;
        @(posedge clk);
        #1 block_code_valid = 0;
    end
    endtask

    task sync_block;
    begin
        code_pulse(SYNC, 10, 10, 1000);
    end
    endtask

    task raw_block;
        input integer v8;
        input integer v16;
        input integer v20;
    begin
        p8 = v8; p16 = v16; p20 = v20;
        fft_block_done = 1;
        @(posedge clk);
        #1 fft_block_done = 0;
    end
    endtask

    task send_half;
        input integer bit_value;
        input integer strength;
    begin
        if (bit_value)
            raw_block(10, strength, 10);
        else
            raw_block(strength, 10, 10);
    end
    endtask

    task start_half;
        input integer bit_value;
        input integer strength;
    begin
        if (bit_value)
            code_pulse(B1, 10, strength, 10);
        else
            code_pulse(B0, strength, 10, 10);
    end
    endtask

    // Build phase-A candidates. corrupt_index flips one SFD bit.
    // invalid_index makes one SFD pair ambiguous.
    task send_phase_a_sfd;
        input integer corrupt_index;
        input integer invalid_index;
        input integer first_payload_bit;
        integer bit_value;
    begin
        for (i = 0; i < 8; i = i + 1) begin
            bit_value = D5[7-i];
            if (i == corrupt_index)
                bit_value = !bit_value;

            if (i == invalid_index) begin
                if (i == 0)
                    code_pulse(B0, 500, 500, 10);
                else
                    raw_block(500, 500, 10);
                raw_block(500, 500, 10);
            end else begin
                if (i == 0)
                    start_half(bit_value, 900);
                else
                    send_half(bit_value, 1000);

                // A weak half still passes after it is added to the strong half.
                if (i == 0)
                    send_half(bit_value, 80);
                else
                    send_half(bit_value, 1000);
            end
        end

        // Candidate block 16 is the first half of the first payload bit.
        send_half(first_payload_bit, 1000);
    end
    endtask

    task send_phase_b_sfd;
        input integer first_payload_bit;
        integer bit_value;
    begin
        // This is block zero. The valid SFD begins at block one.
        code_pulse(B0, 1000, 10, 10);
        for (i = 0; i < 8; i = i + 1) begin
            bit_value = D5[7-i];
            send_half(bit_value, 1000);
            send_half(bit_value, 1000);
        end
    end
    endtask

    task wait_for_frame_start;
        input integer maximum_cycles;
        integer cycles;
        integer found;
    begin
        cycles = 0;
        found = 0;
        while ((cycles < maximum_cycles) && !found) begin
            @(posedge clk);
            #1;
            if (frame_start)
                found = 1;
            cycles = cycles + 1;
        end
        if (!found) begin
            $display("FAIL: frame_start timeout");
            failures = failures + 1;
        end
    end
    endtask

    task expect_no_frame_start;
        input integer cycles_to_watch;
        integer cycles;
    begin
        for (cycles = 0; cycles < cycles_to_watch; cycles = cycles + 1) begin
            @(posedge clk);
            #1;
            if (frame_start || symbol_valid) begin
                $display("FAIL: rejected input started a frame");
                failures = failures + 1;
                cycles = cycles_to_watch;
            end
        end
    end
    endtask

    task check_d5_replay;
        integer received;
        integer cycles;
    begin
        received = 0;
        cycles = 0;
        while ((received < 8) && (cycles < 30)) begin
            @(posedge clk);
            #1;
            if (symbol_valid) begin
                if (symbol_code[0] !== D5[7-received]) begin
                    $display("FAIL: replayed SFD bit %0d", received);
                    failures = failures + 1;
                end
                received = received + 1;
            end
            cycles = cycles + 1;
        end
        if (received != 8) begin
            $display("FAIL: expected eight replayed SFD bits, got %0d", received);
            failures = failures + 1;
        end
    end
    endtask

    task case_short_sync;
    begin
        $display("CASE 1: fewer than eight SYNC blocks");
        for (i = 0; i < 7; i = i + 1)
            sync_block;
        code_pulse(B1, 10, 1000, 10);
        expect_no_frame_start(20);
        coverage_hits = coverage_hits | 1;
    end
    endtask

    task case_phase_a_and_wait;
    begin
        $display("CASE 2: continuous SYNC wait, phase A, weak SFD half, payload BIT1");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        for (i = 0; i < 5; i = i + 1)
            sync_block;
        coverage_hits = coverage_hits | 2;
        send_phase_a_sfd(-1, -1, 1);
        wait_for_frame_start(30);
        check_d5_replay;
        send_half(1, 1000);
        if (!symbol_valid || (symbol_code != B1)) begin
            $display("FAIL: phase-A payload BIT1");
            failures = failures + 1;
        end
        coverage_hits = coverage_hits | 4 | 8;

        frame_finish = 1;
        @(posedge clk);
        #1 frame_finish = 0;
        if (dut.c_state != 0) begin
            $display("FAIL: frame_finish did not return to ST_SEARCH");
            failures = failures + 1;
        end
        coverage_hits = coverage_hits | 16;
    end
    endtask

    task case_phase_b;
    begin
        $display("CASE 3: phase B and payload BIT0");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        send_phase_b_sfd(0);
        wait_for_frame_start(30);
        check_d5_replay;
        send_half(0, 1000);
        send_half(0, 1000);
        if (!symbol_valid || (symbol_code != B0)) begin
            $display("FAIL: phase-B payload BIT0");
            failures = failures + 1;
        end
        coverage_hits = coverage_hits | 32 | 64;
    end
    endtask

    task case_changed_sfd;
    begin
        $display("CASE 4: changed SFD must be rejected");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        send_phase_a_sfd(4, -1, 1);
        expect_no_frame_start(30);
        coverage_hits = coverage_hits | 128;
    end
    endtask

    task case_invalid_sfd_pair;
    begin
        $display("CASE 5: ambiguous SFD pair must be rejected");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        send_phase_a_sfd(-1, 3, 1);
        expect_no_frame_start(30);
        coverage_hits = coverage_hits | 256;
    end
    endtask

    task case_score_tie;
    begin
        $display("CASE 6: equal A/B scores must be rejected");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        code_pulse(B0, 500, 500, 10);
        for (i = 1; i < 17; i = i + 1)
            raw_block(500, 500, 10);
        expect_no_frame_start(20);
        coverage_hits = coverage_hits | 512;
    end
    endtask

    task case_invalid_payload;
    begin
        $display("CASE 7: invalid payload pair must assert frame_abort");
        for (i = 0; i < 8; i = i + 1)
            sync_block;
        send_phase_a_sfd(-1, -1, 0);
        wait_for_frame_start(30);
        check_d5_replay;
        // The stored first half is BIT0 (bin8=1000).  Make the second half
        // BIT1 (bin16=1000), so the combined bin8/bin16 powers are equal.
        raw_block(10, 1000, 10);
        if (!frame_abort) begin
            $display("FAIL: invalid payload did not assert frame_abort");
            failures = failures + 1;
        end
        coverage_hits = coverage_hits | 1024;
    end
    endtask

    initial begin
        reset_dut; case_short_sync;
        reset_dut; case_phase_a_and_wait;
        reset_dut; case_phase_b;
        reset_dut; case_changed_sfd;
        reset_dut; case_invalid_sfd_pair;
        reset_dut; case_score_tie;
        reset_dut; case_invalid_payload;

        if (coverage_hits == 2047)
            $display("FUNCTIONAL COVERAGE: 100%% (11/11 planned scenarios)");
        else
            $display("FUNCTIONAL COVERAGE: incomplete, mask=%0h", coverage_hits);

        if (failures == 0)
            $display("TEST RESULT: PASS");
        else
            $display("TEST RESULT: FAIL (%0d failures)", failures);

        #20;
        $finish;
    end
endmodule

