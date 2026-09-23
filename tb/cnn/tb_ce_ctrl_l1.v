`timescale 1ns / 1ps

// Self-checking testbench for ce_ctrl_l1.
//
// Vivado XSim example (run from tb/cnn):
//   xvlog ../../rtl/cnn/ce_ctrl_l1.v tb_ce_ctrl_l1.v
//   xelab tb_ce_ctrl_l1 -s tb_ce_ctrl_l1_sim
//   xsim tb_ce_ctrl_l1_sim -runall

module tb_ce_ctrl_l1;

    localparam [1:0] IDLE     = 2'd0;
    localparam [1:0] IMG_IN   = 2'd1;
    localparam [1:0] WAIT_MAC = 2'd2;
    localparam [1:0] STOP     = 2'd3;

    reg  clk;
    reg  rst_n;
    reg  s_axis_tvalid;
    wire s_axis_tready;
    reg  s_axis_tuser;
    reg  s_axis_tlast;
    reg  win_valid;
    wire pixel_valid;
    wire phase_clear;
    wire mac_start;
    reg  mac_done;

    integer check_count;
    integer fail_count;

    ce_ctrl_l1 dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .win_valid    (win_valid),
        .pixel_valid  (pixel_valid),
        .phase_clear  (phase_clear),
        .mac_start    (mac_start),
        .mac_done     (mac_done)
    );

    always #5 clk = ~clk;

    task check_outputs;
        input [8*48-1:0] name;
        input [1:0]      exp_state;
        input            exp_ready;
        input            exp_pixel_valid;
        input            exp_phase_clear;
        input            exp_mac_start;
        input            exp_ch_count;
        begin
            check_count = check_count + 1;
            if ((dut.c_state   !== exp_state)       ||
                (s_axis_tready !== exp_ready)       ||
                (pixel_valid   !== exp_pixel_valid) ||
                (phase_clear   !== exp_phase_clear) ||
                (mac_start     !== exp_mac_start)   ||
                (dut.ch_count  !== exp_ch_count)) begin
                fail_count = fail_count + 1;
                $display("[FAIL] %0s @ %0t", name, $time);
                $display("       state=%0d ready=%b pixel_valid=%b phase_clear=%b mac_start=%b ch_count=%b",
                         dut.c_state, s_axis_tready, pixel_valid,
                         phase_clear, mac_start, dut.ch_count);
                $display("       exp  =%0d ready=%b pixel_valid=%b phase_clear=%b mac_start=%b ch_count=%b",
                         exp_state, exp_ready, exp_pixel_valid,
                         exp_phase_clear, exp_mac_start, exp_ch_count);
            end else begin
                $display("[ OK ] %0s @ %0t", name, $time);
            end
        end
    endtask

    task drive_on_negedge;
        input valid;
        input user_bit;
        input last;
        input window_valid;
        input done;
        begin
            @(negedge clk);
            s_axis_tvalid = valid;
            s_axis_tuser  = user_bit;
            s_axis_tlast  = last;
            win_valid     = window_valid;
            mac_done      = done;
        end
    endtask

    task sample_after_posedge;
        begin
            @(posedge clk);
            #1;
        end
    endtask

    initial begin
        $timeformat(-9, 0, " ns", 10);
        clk           = 1'b0;
        rst_n         = 1'b0;
        s_axis_tvalid = 1'b0;
        s_axis_tuser  = 1'b0;
        s_axis_tlast  = 1'b0;
        win_valid     = 1'b0;
        mac_done      = 1'b0;
        check_count   = 0;
        fail_count    = 0;

        // Asynchronous reset must immediately restore the registered state.
        #2;
        check_outputs("asynchronous reset", IDLE, 1'b1, 1'b0,
                      1'b0, 1'b0, 1'b0);

        repeat (2) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;
        #1;
        check_outputs("reset released / idle", IDLE, 1'b1, 1'b0,
                      1'b0, 1'b0, 1'b0);

        // First accepted AXI-stream item starts image input.
        drive_on_negedge(1'b1, 1'b1, 1'b0, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("IDLE -> IMG_IN", IMG_IN, 1'b1, 1'b1,
                      1'b0, 1'b0, 1'b0);

        // A valid window blocks stream input and starts the MAC.
        drive_on_negedge(1'b1, 1'b0, 1'b0, 1'b1, 1'b0);
        #1;
        if ((s_axis_tready !== 1'b0) || (pixel_valid !== 1'b0)) begin
            fail_count = fail_count + 1;
            $display("[FAIL] win_valid did not apply backpressure before clock @ %0t", $time);
        end
        sample_after_posedge;
        check_outputs("IMG_IN -> WAIT_MAC", WAIT_MAC, 1'b0, 1'b0,
                      1'b0, 1'b1, 1'b0);

        // WAIT_MAC holds until mac_done, regardless of input valid.
        drive_on_negedge(1'b1, 1'b0, 1'b1, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("WAIT_MAC hold", WAIT_MAC, 1'b0, 1'b0,
                      1'b0, 1'b0, 1'b0);

        // With channel count zero, MAC completion returns to image input.
        drive_on_negedge(1'b0, 1'b0, 1'b0, 1'b0, 1'b1);
        sample_after_posedge;
        check_outputs("first MAC done -> IMG_IN", IMG_IN, 1'b1, 1'b0,
                      1'b0, 1'b0, 1'b0);

        // tlast only increments the channel count on an actual pixel transfer.
        drive_on_negedge(1'b1, 1'b0, 1'b1, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("last pixel accepted", IMG_IN, 1'b1, 1'b1,
                      1'b0, 1'b0, 1'b1);

        // A second window starts another MAC operation.
        drive_on_negedge(1'b0, 1'b0, 1'b0, 1'b1, 1'b0);
        sample_after_posedge;
        check_outputs("second IMG_IN -> WAIT_MAC", WAIT_MAC, 1'b0, 1'b0,
                      1'b0, 1'b1, 1'b1);

        // With channel count one, completion enters STOP and pulses clear.
        drive_on_negedge(1'b0, 1'b0, 1'b0, 1'b0, 1'b1);
        sample_after_posedge;
        check_outputs("final MAC done -> STOP", STOP, 1'b0, 1'b0,
                      1'b1, 1'b0, 1'b1);

        // STOP lasts one clock. mac_start has already returned low because it
        // is asserted for only the first WAIT_MAC cycle.
        drive_on_negedge(1'b0, 1'b0, 1'b0, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("STOP -> IDLE", IDLE, 1'b1, 1'b0,
                      1'b0, 1'b0, 1'b0);

        drive_on_negedge(1'b0, 1'b0, 1'b0, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("IDLE keeps mac_start low", IDLE, 1'b1, 1'b0,
                      1'b0, 1'b0, 1'b0);

        // Verify asynchronous reset while the FSM is active.
        drive_on_negedge(1'b1, 1'b0, 1'b0, 1'b0, 1'b0);
        sample_after_posedge;
        check_outputs("start before reset test", IMG_IN, 1'b1, 1'b1,
                      1'b0, 1'b0, 1'b0);

        #2 rst_n = 1'b0;
        #1;
        check_outputs("active-state asynchronous reset", IDLE, 1'b1, 1'b1,
                      1'b0, 1'b0, 1'b0);

        // pixel_valid is combinational, so remove tvalid before the summary.
        s_axis_tvalid = 1'b0;
        #1;

        if (fail_count == 0)
            $display("\n[PASS] ce_ctrl_l1: %0d checks completed with no failures.", check_count);
        else
            $display("\n[FAIL] ce_ctrl_l1: %0d of %0d checks failed.", fail_count, check_count);

        $finish;
    end

endmodule
