`timescale 1ns / 1ps
// Bit-exact test of fc_top against the C golden model; frame 2 has every input at 32767 so the quantizers clamp.
// Run with `make -f sim.mk fc` (tx/cnn/tb); +vcd dumps build/<config>.vcd

module tb_fc;

    parameter N_IN = 400;  // inputs per frame
    parameter NSTIM = 800;  // 2 frames
    parameter NOUT1 = 240;  // 2 x 120 neurons
    parameter NOUT2 = 168;  // 2 x 84
    parameter NOUT3 = 52;  // 2 x 26 logits
    parameter LOGITS = 26;  // logits per frame
    parameter VALID_PCT = 100;  // chance that MaxPooling offers a value each cycle
    parameter READY_PCT = 80;  // chance that Argmax accepts each cycle
    parameter SEED = 1;
    parameter VCD_FILE = "build/tb_fc.vcd";
    // "." is the xsim run directory; sim.mk points iverilog at tx/cnn/rtl/mem
    parameter MEM = ".";

    reg         clk;
    reg         rst_n;
    reg  [15:0] fc_in_data;
    reg         fc_in_valid;
    reg         logit_ready;

    wire        fc_in_ready;
    wire [15:0] logit_data;
    wire        logit_valid;

    fc_top #(
        .WEIGHT_FILE({MEM, "/fc_weight.mem"}),
        .BIAS_FILE  ({MEM, "/fc_bias.mem"})
    ) u_fc_top (
        .clk        (clk),
        .rst_n      (rst_n),
        .fc_in_data (fc_in_data),
        .fc_in_valid(fc_in_valid),
        .logit_ready(logit_ready),
        .fc_in_ready(fc_in_ready),
        .logit_data (logit_data),
        .logit_valid(logit_valid)
    );

    // golden data
    reg [15:0] stim_mem[0:NSTIM-1];
    reg [15:0] out1_mem[0:NOUT1-1];
    reg [15:0] out2_mem[0:NOUT2-1];
    reg [15:0] out3_mem[0:NOUT3-1];

    integer fed, idx1, idx2, idx3, fail_count, cycle, seed, first_in, last_logit;
    reg     fire_in;
    reg     stalled;
    reg [15:0] logit_prev;

    // layer boundaries: fc_quant_out writes one FC1 / FC2 value per clock into the next buffer
    wire        fc1_fire = u_fc_top.u_quant_out.feature_we & (u_fc_top.u_quant_out.feature_layer == 2'd1);
    wire        fc2_fire = u_fc_top.u_quant_out.feature_we & (u_fc_top.u_quant_out.feature_layer == 2'd2);
    wire [15:0] fc_wdata = u_fc_top.u_quant_out.feature_wdata;

    // a group's sums arriving while the hold register is still full would be lost
    wire quant_out_overrun = u_fc_top.u_mac.sum_valid & u_fc_top.u_quant_out.valid;

    always #5 clk = ~clk;

    task fail;
        input [8*48-1:0] what;
        begin
            fail_count = fail_count + 1;
            if (fail_count <= 10) $display("  FAIL @cycle %0d: %0s", cycle, what);
        end
    endtask

    // $readmemh resolves against the simulator's working directory, which differs per flow
    // and leaves the array at x when the file is missing: try each place and keep the set that loaded
    task load_vectors;
        begin
            // Vivado: the .mem files of sim_1 sit in the xsim run directory
            $readmemh("fc_stim.mem", stim_mem);
            $readmemh("fc1_out.mem", out1_mem);
            $readmemh("fc2_out.mem", out2_mem);
            $readmemh("fc3_out.mem", out3_mem);

            // iverilog: tx/cnn/tb
            if (stim_mem[0] === 16'hxxxx) begin
                $readmemh("vectors/fc_stim.mem", stim_mem);
                $readmemh("vectors/fc1_out.mem", out1_mem);
                $readmemh("vectors/fc2_out.mem", out2_mem);
                $readmemh("vectors/fc3_out.mem", out3_mem);
            end

            // batch run: tx/cnn/rtl/mem
            if (stim_mem[0] === 16'hxxxx) begin
                $readmemh("../../tb/vectors/fc_stim.mem", stim_mem);
                $readmemh("../../tb/vectors/fc1_out.mem", out1_mem);
                $readmemh("../../tb/vectors/fc2_out.mem", out2_mem);
                $readmemh("../../tb/vectors/fc3_out.mem", out3_mem);
            end

            if (stim_mem[0] === 16'hxxxx) begin
                $display("  FAIL: vectors not found - run make -f fc.mk rtl-vectors");
                $finish;
            end
        end
    endtask

    initial begin
        load_vectors;
        if ($test$plusargs("vcd")) begin
            $dumpfile(VCD_FILE);
            $dumpvars(0, tb_fc);
        end

        clk         = 1'b0;
        rst_n       = 1'b0;
        fc_in_data  = 16'd0;
        fc_in_valid = 1'b0;
        logit_ready = 1'b0;
        fed         = 0;
        idx1        = 0;
        idx2        = 0;
        idx3        = 0;
        fail_count  = 0;
        stalled     = 1'b0;
        logit_prev  = 16'd0;
        seed        = SEED;
        first_in    = -1;
        last_logit  = -1;

        repeat (3) @(negedge clk);
        rst_n = 1'b1;
        @(negedge clk);

        for (cycle = 0; idx3 < NOUT3 && cycle < 400 * NSTIM; cycle = cycle + 1) begin
            // ---- drive (inputs change on the falling edge) ----
            // a frame starts only after the previous one has left through the logit port
            fire_in     = (fed < NSTIM) && (fed < ((idx3 / LOGITS) + 1) * N_IN)
                          && (({$random(seed)} % 100) < $unsigned(VALID_PCT));
            fc_in_valid = fire_in;
            fc_in_data  = fire_in ? stim_mem[fed] : 16'hBAAD;  // garbage while idle
            logit_ready = ({$random(seed)} % 100) < $unsigned(READY_PCT);
            #1;

            // ---- FC1 / FC2 boundaries ----
            if (fc1_fire) begin
                if (idx1 >= NOUT1) fail("more FC1 values than golden");
                else if (fc_wdata !== out1_mem[idx1]) begin
                    fail("FC1 value mismatch");
                    if (fail_count <= 10)
                        $display("      FC1 #%0d: rtl %h golden %h", idx1, fc_wdata, out1_mem[idx1]);
                end
                idx1 = idx1 + 1;
            end
            if (fc2_fire) begin
                if (idx2 >= NOUT2) fail("more FC2 values than golden");
                else if (fc_wdata !== out2_mem[idx2]) begin
                    fail("FC2 value mismatch");
                    if (fail_count <= 10)
                        $display("      FC2 #%0d: rtl %h golden %h", idx2, fc_wdata, out2_mem[idx2]);
                end
                idx2 = idx2 + 1;
            end

            // ---- logit stream to Argmax ----
            if (logit_valid === 1'bx) fail("logit_valid is X");
            if (stalled && !(logit_valid === 1'b1 && logit_data === logit_prev))
                fail("logit_data changed while logit_ready = 0");
            if (logit_valid === 1'b1 && logit_ready) begin
                if (logit_data !== out3_mem[idx3]) begin
                    fail("logit_data mismatch");
                    if (fail_count <= 10)
                        $display("      logit #%0d: rtl %h golden %h", idx3, logit_data, out3_mem[idx3]);
                end
                idx3 = idx3 + 1;
            end

            if (fc_in_ready === 1'bx) fail("fc_in_ready is X");
            if (quant_out_overrun) fail("hold register overrun");

            stalled    = (logit_valid === 1'b1) && !logit_ready;
            logit_prev = logit_data;

            // ---- bookkeeping for the next cycle ----
            if (fire_in && fc_in_ready) begin
                if (fed == 0) first_in = cycle;
                fed = fed + 1;
            end
            if (idx3 == LOGITS && last_logit < 0) last_logit = cycle;
            @(negedge clk);
        end

        if (fed != NSTIM) fail("not all stimulus consumed");
        if (idx1 != NOUT1) fail("FC1 value count");
        if (idx2 != NOUT2) fail("FC2 value count");
        if (idx3 != NOUT3) fail("logit count");

        $display("[%0s] fc_top valid=%0d%% ready=%0d%% seed=%0d : in %0d/%0d, FC1 %0d/%0d, FC2 %0d/%0d, logits %0d/%0d, %0d failures, frame 1 %0d cycles",
                 (fail_count == 0) ? " ok " : "FAIL", VALID_PCT, READY_PCT, SEED, fed, NSTIM,
                 idx1, NOUT1, idx2, NOUT2, idx3, NOUT3, fail_count, last_logit - first_in);
        $finish;
    end

endmodule
