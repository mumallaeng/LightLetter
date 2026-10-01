`timescale 1ns / 1ps
// Bit-exact test: output_buffer -> relu_quant against the C golden model.
//   stimulus / expected files come from `make -f output_buffer.mk rtl-vectors` (cnn/golden_model):
//   frame 1 = real image from the Python model, frame 2 = synthetic corner cases
//   (negatives, .5 rounding ties, 32767 clamp, widest inputs)
//   run with `make -f sim.mk` (tb/cnn); defaults below are the conv2 configuration.
//   +vcd dumps build/<config>.vcd

module tb_output_buffer;

    parameter N = 121;
    parameter C_OUT = 16;
    parameter NUM_GROUPS = 2;
    parameter PACK = 1;
    parameter SCALE_EXP = 16;
    parameter NSTIM = 7744;  // 2 frames
    parameter NSUM = 3872;
    parameter NOUT = 3872;
    parameter VALID_PCT = 75;  // chance that mac_array delivers a result each cycle
    parameter READY_PCT = 100;  // chance that MaxPooling accepts each cycle
    parameter SEED = 1;
    parameter BIAS_FILE = "../../rtl/cnn/mem/conv2_bias.mem";
    parameter STIM_FILE = "vectors/conv2_stim.mem";
    parameter SUM_FILE = "vectors/conv2_sum.mem";
    parameter OUT_FILE = "vectors/conv2_out.mem";
    parameter VCD_FILE = "build/tb_output_buffer.vcd";

    localparam CH_W = 36;
    localparam ACC_W = 40;
    localparam OUT_W = 16 * PACK + 1;      // {out_ch_done, lanes}

    localparam FRAME_STIM = NUM_GROUPS * N * C_OUT;  // mac_valid count per frame
    localparam FRAME_OUT = N * C_OUT / PACK;  // output entries per frame

    reg                     clk;
    reg                     rst_n;
    reg  signed [CH_W-1:0]  ch_result0;
    reg  signed [CH_W-1:0]  ch_result1;
    reg  signed [CH_W-1:0]  ch_result2;
    reg                     mac_valid;
    reg                     out_ready;

    wire                    ch3_5_en;
    wire signed [ACC_W-1:0] sum_data;
    wire                    sum_valid;
    wire [15:0]             out_data0;
    wire [15:0]             out_data1;
    wire [15:0]             out_data2;
    wire                    out_ch_done;
    wire                    out_valid;

    output_buffer #(
        .N         (N),
        .C_OUT     (C_OUT),
        .NUM_GROUPS(NUM_GROUPS),
        .CH_W      (CH_W),
        .ACC_W     (ACC_W),
        .BIAS_FILE (BIAS_FILE)
    ) u_output_buffer (
        .clk       (clk),
        .rst_n     (rst_n),
        .ch_result0(ch_result0),
        .ch_result1(ch_result1),
        .ch_result2(ch_result2),
        .mac_valid (mac_valid),
        .ch3_5_en  (ch3_5_en),
        .sum_data  (sum_data),
        .sum_valid (sum_valid)
    );

    relu_quant #(
        .ACC_W    (ACC_W),
        .N        (N),
        .C_OUT    (C_OUT),
        .PACK     (PACK),
        .SCALE_EXP(SCALE_EXP)
    ) u_relu_quant (
        .clk        (clk),
        .rst_n      (rst_n),
        .sum_data   (sum_data),
        .sum_valid  (sum_valid),
        .out_ready  (out_ready),
        .out_data0  (out_data0),
        .out_data1  (out_data1),
        .out_data2  (out_data2),
        .out_ch_done(out_ch_done),
        .out_valid  (out_valid)
    );

    // golden data
    reg [3*CH_W-1:0] stim_mem [0:NSTIM-1];    // {ch_result2, ch_result1, ch_result0}
    reg [ACC_W-1:0]  sum_mem  [0:NSUM-1];     // sum_data
    reg [OUT_W-1:0]  out_mem  [0:NOUT-1];     // {out_ch_done, out_data2, out_data1, out_data0}, group-major order

    integer fed, sum_idx, out_idx, fail_count, cycle, seed;
    integer exp_group, max_fill;
    reg     overrun;
    reg              fire;
    reg              frame_gap;
    reg              stalled;
    reg [OUT_W-1:0] out_now, out_prev;

    // a push while the frame's slots are still held by unread entries is dropped by the buffer
    wire rb_overrun = u_relu_quant.u_out_reorder.push & u_relu_quant.u_out_reorder.frame_full;

    always #5 clk = ~clk;

    task fail;
        input [8*40-1:0] what;
        begin
            fail_count = fail_count + 1;
            if (fail_count <= 10) $display("  FAIL @cycle %0d: %0s", cycle, what);
        end
    endtask

    initial begin
        $readmemh(STIM_FILE, stim_mem);
        $readmemh(SUM_FILE, sum_mem);
        $readmemh(OUT_FILE, out_mem);
        if ($test$plusargs("vcd")) begin
            $dumpfile(VCD_FILE);
            $dumpvars(0, tb_output_buffer);
        end

        clk        = 1'b0;
        rst_n      = 1'b0;
        mac_valid  = 1'b0;
        out_ready  = 1'b0;
        ch_result0 = 0;
        ch_result1 = 0;
        ch_result2 = 0;
        fed        = 0;
        sum_idx    = 0;
        out_idx    = 0;
        fail_count = 0;
        exp_group  = 0;
        max_fill   = 0;
        stalled    = 1'b0;
        out_prev   = 0;
        seed       = SEED;

        repeat (3) @(negedge clk);
        rst_n = 1'b1;
        @(negedge clk);  // Output Buffer leaves IDLE on this edge

        for (cycle = 0; out_idx < NOUT && cycle < 40 * NSTIM + 1000; cycle = cycle + 1) begin
            // ---- drive (inputs change on the falling edge) ----
            // a new frame starts only after MaxPooling has drained the previous one
            frame_gap  = (fed % FRAME_STIM == 0) && (out_idx < (fed / FRAME_STIM) * FRAME_OUT);
            fire       = (fed < NSTIM) && !frame_gap && (({$random(seed)} % 100) < $unsigned(VALID_PCT));
            mac_valid  = fire;
            ch_result0 = $signed(fire ? stim_mem[fed][CH_W-1:0]        : 36'h0BAD0BAD0);   // garbage while idle
            ch_result1 = $signed(fire ? stim_mem[fed][2*CH_W-1:CH_W]   : 36'h1BAD1BAD1);
            ch_result2 = $signed(fire ? stim_mem[fed][3*CH_W-1:2*CH_W] : 36'h2BAD2BAD2);
            out_ready  = ({$random(seed)} % 100) < $unsigned(READY_PCT);
            #1;

            // ---- Output Buffer stream: sum_data ----
            if (sum_valid === 1'bx) fail("sum_valid is X");
            if (sum_valid === 1'b1) begin
                if (sum_idx >= NSUM) fail("more sum outputs than golden");
                else if (sum_data !== sum_mem[sum_idx]) begin
                    fail("sum_data mismatch");
                    if (fail_count <= 10) $display("      sum #%0d: rtl %h golden %h", sum_idx, sum_data, sum_mem[sum_idx]);
                end
                sum_idx = sum_idx + 1;
            end
            if (ch3_5_en !== (exp_group != 0)) fail("ch3_5_en wrong");

            // ---- MaxPooling side: {out_ch_done, out_data2, out_data1, out_data0} ----
            if (PACK == 1) out_now = {out_ch_done, out_data0};
            else if (PACK == 2) out_now = {out_ch_done, out_data1, out_data0};
            else out_now = {out_ch_done, out_data2, out_data1, out_data0};

            if (out_valid === 1'bx) fail("out_valid is X");
            if (stalled && !(out_valid === 1'b1 && out_now === out_prev)) fail("output changed while out_ready = 0");
            if (out_valid === 1'b1 && out_ready) begin
                if (out_now !== out_mem[out_idx]) begin
                    fail("out_data / out_ch_done mismatch");
                    if (fail_count <= 10) $display("      entry #%0d: rtl %h golden %h", out_idx, out_now, out_mem[out_idx]);
                end
                out_idx = out_idx + 1;
            end
            if (PACK < 2 && out_data1 !== 16'd0) fail("out_data1 must be 0");
            if (PACK < 3 && out_data2 !== 16'd0) fail("out_data2 must be 0");
            if (rb_overrun) fail("reorder buffer overrun (next frame started before the previous one was read out)");

            stalled  = (out_valid === 1'b1) && !out_ready;
            out_prev = out_now;
            if (u_relu_quant.u_out_reorder.wr_cnt > max_fill) max_fill = u_relu_quant.u_out_reorder.wr_cnt;

            // ---- bookkeeping for the next cycle ----
            if (fire) begin
                if (fed % (N * C_OUT) == N * C_OUT - 1) exp_group = (exp_group == NUM_GROUPS - 1) ? 0 : exp_group + 1;
                fed = fed + 1;
            end
            @(negedge clk);
        end

        if (fed != NSTIM) fail("not all stimulus consumed");
        if (sum_idx != NSUM) fail("sum output count");
        if (out_idx != NOUT) fail("output entry count");

        $display("[%0s] N=%0d C_OUT=%0d groups=%0d PACK=%0d valid=%0d%% ready=%0d%% seed=%0d : sum %0d/%0d, out %0d/%0d, buffer peak %0d, %0d failures",
                 (fail_count == 0) ? " ok " : "FAIL", N, C_OUT, NUM_GROUPS, PACK, VALID_PCT, READY_PCT, SEED, sum_idx, NSUM, out_idx, NOUT, max_fill,
                 fail_count);
        $finish;
    end

endmodule
