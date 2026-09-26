`timescale 1ns / 1ps
// Bit-exact test: fc_top against the C golden model.
//   vectors come from `make -f fc.mk rtl-vectors` (cnn/golden_model):
//   frame 1 = the real image, frame 2 = every input at 32767, which clamps the quantizers
//   the FC1 and FC2 streams are checked at the layer boundaries, FC3 at the logit port
//   run with `make -f sim.mk fc` (tb/cnn); +vcd dumps build/<config>.vcd

module tb_fc;

    parameter N_IN = 400;  // inputs per frame
    parameter NSTIM = 800;  // 2 frames
    parameter NOUT1 = 240;  // 2 x 120 neurons
    parameter NOUT2 = 168;  // 2 x 84
    parameter NOUT3 = 72;  // 2 x 36 logits
    parameter LOGITS = 36;  // logits per frame
    parameter VALID_PCT = 100;  // chance that MaxPooling offers a value each cycle
    parameter READY_PCT = 80;  // chance that Argmax accepts each cycle
    parameter SEED = 1;
    parameter VCD_FILE = "build/tb_fc.vcd";
    // ROM files: "." is where Vivado puts the project's .mem files for a run; sim.mk points
    // iverilog at rtl/cnn/mem instead
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
        .FC1_WEIGHT({MEM, "/fc1_weight.mem"}),
        .FC1_BIAS  ({MEM, "/fc1_bias.mem"}),
        .FC2_WEIGHT({MEM, "/fc2_weight.mem"}),
        .FC2_BIAS  ({MEM, "/fc2_bias.mem"}),
        .FC3_WEIGHT({MEM, "/fc3_weight.mem"}),
        .FC3_BIAS  ({MEM, "/fc3_bias.mem"})
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

    integer fed, idx1, idx2, idx3, fail_count, cycle, seed;
    reg     fire_in;
    reg     stalled;
    reg [15:0] logit_prev;

    // layer boundaries: a value moves when the producer is valid and the consumer is ready
    wire fc1_fire = u_fc_top.u_fc1.out_valid & u_fc_top.u_fc1.out_ready;
    wire fc2_fire = u_fc_top.u_fc2.out_valid & u_fc_top.u_fc2.out_ready;

    // a push while the previous frame is still held by unread entries is dropped by the buffer
    wire rb1_overrun = u_fc_top.u_fc1.GEN_RELU_QUANT.u_relu_quant.u_out_reorder.push
                     & u_fc_top.u_fc1.GEN_RELU_QUANT.u_relu_quant.u_out_reorder.frame_full;
    wire rb2_overrun = u_fc_top.u_fc2.GEN_RELU_QUANT.u_relu_quant.u_out_reorder.push
                     & u_fc_top.u_fc2.GEN_RELU_QUANT.u_relu_quant.u_out_reorder.frame_full;
    wire rb3_overrun = u_fc_top.u_fc3.GEN_QUANT_SIGNED.u_fc_quant_signed.u_out_reorder.push
                     & u_fc_top.u_fc3.GEN_QUANT_SIGNED.u_fc_quant_signed.u_out_reorder.frame_full;

    always #5 clk = ~clk;

    task fail;
        input [8*48-1:0] what;
        begin
            fail_count = fail_count + 1;
            if (fail_count <= 10) $display("  FAIL @cycle %0d: %0s", cycle, what);
        end
    endtask

    // $readmemh resolves a relative path against the simulator's working directory, which
    // differs per flow, and it leaves the array at x rather than failing when the file is not
    // there. Try each place the vectors can sit and keep the set that loaded, so no flow needs
    // a path override, and say so plainly if none of them held the files.
    task load_vectors;
        begin
            // tb/cnn, where iverilog runs
            $readmemh("vectors/fc_stim.mem", stim_mem);
            $readmemh("vectors/fc1_out.mem", out1_mem);
            $readmemh("vectors/fc2_out.mem", out2_mem);
            $readmemh("vectors/fc3_out.mem", out3_mem);

            // rtl/cnn/mem, where a batch run reads the ROMs from
            if (stim_mem[0] === 16'hxxxx) begin
                $readmemh("../../tb/cnn/vectors/fc_stim.mem", stim_mem);
                $readmemh("../../tb/cnn/vectors/fc1_out.mem", out1_mem);
                $readmemh("../../tb/cnn/vectors/fc2_out.mem", out2_mem);
                $readmemh("../../tb/cnn/vectors/fc3_out.mem", out3_mem);
            end

            // <project>.sim/sim_1/behav/xsim, where Vivado runs
            if (stim_mem[0] === 16'hxxxx) begin
                $readmemh("../../../../tb/cnn/vectors/fc_stim.mem", stim_mem);
                $readmemh("../../../../tb/cnn/vectors/fc1_out.mem", out1_mem);
                $readmemh("../../../../tb/cnn/vectors/fc2_out.mem", out2_mem);
                $readmemh("../../../../tb/cnn/vectors/fc3_out.mem", out3_mem);
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
                else if (u_fc_top.u_fc1.out_data !== out1_mem[idx1]) begin
                    fail("FC1 out_data mismatch");
                    if (fail_count <= 10)
                        $display("      FC1 #%0d: rtl %h golden %h", idx1, u_fc_top.u_fc1.out_data, out1_mem[idx1]);
                end
                idx1 = idx1 + 1;
            end
            if (fc2_fire) begin
                if (idx2 >= NOUT2) fail("more FC2 values than golden");
                else if (u_fc_top.u_fc2.out_data !== out2_mem[idx2]) begin
                    fail("FC2 out_data mismatch");
                    if (fail_count <= 10)
                        $display("      FC2 #%0d: rtl %h golden %h", idx2, u_fc_top.u_fc2.out_data, out2_mem[idx2]);
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
            if (rb1_overrun) fail("FC1 reorder buffer overrun");
            if (rb2_overrun) fail("FC2 reorder buffer overrun");
            if (rb3_overrun) fail("FC3 reorder buffer overrun");

            stalled    = (logit_valid === 1'b1) && !logit_ready;
            logit_prev = logit_data;

            // ---- bookkeeping for the next cycle ----
            if (fire_in && fc_in_ready) fed = fed + 1;
            @(negedge clk);
        end

        if (fed != NSTIM) fail("not all stimulus consumed");
        if (idx1 != NOUT1) fail("FC1 value count");
        if (idx2 != NOUT2) fail("FC2 value count");
        if (idx3 != NOUT3) fail("logit count");

        $display("[%0s] fc_top valid=%0d%% ready=%0d%% seed=%0d : in %0d/%0d, FC1 %0d/%0d, FC2 %0d/%0d, logits %0d/%0d, %0d failures",
                 (fail_count == 0) ? " ok " : "FAIL", VALID_PCT, READY_PCT, SEED, fed, NSTIM,
                 idx1, NOUT1, idx2, NOUT2, idx3, NOUT3, fail_count);
        $finish;
    end

endmodule
