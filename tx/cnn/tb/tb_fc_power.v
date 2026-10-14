`timescale 1ns / 1ps
// fc_top driven through its ports only, so the same bench runs on the routed netlist (fc_saif.sh) to record a SAIF.
// Stimulus and logit check match tb_fc; FRAMES = 1 keeps the real image only (frame 2 of tb_fc is the clamp test).

module tb_fc_power;

    parameter N_IN = 400;
    parameter LOGITS = 26;
    parameter FRAMES = 1;
    parameter VALID_PCT = 100;
    parameter READY_PCT = 80;
    parameter SEED = 1;
    parameter STIM_FILE = "fc_stim.mem";
    parameter OUT_FILE = "fc3_out.mem";

    localparam NSTIM = FRAMES * N_IN;
    localparam NOUT3 = FRAMES * LOGITS;

    reg         clk;
    reg         rst_n;
    reg  [15:0] fc_in_data;
    reg         fc_in_valid;
    reg         logit_ready;

    wire        fc_in_ready;
    wire [15:0] logit_data;
    wire        logit_valid;

    fc_top u_fc_top (
        .clk        (clk),
        .rst_n      (rst_n),
        .fc_in_data (fc_in_data),
        .fc_in_valid(fc_in_valid),
        .logit_ready(logit_ready),
        .fc_in_ready(fc_in_ready),
        .logit_data (logit_data),
        .logit_valid(logit_valid)
    );

    reg [15:0] stim_mem[0:2*N_IN-1];
    reg [15:0] out3_mem[0:2*LOGITS-1];

    integer fed, idx3, fail_count, cycle, seed, first_in, last_logit;
    integer valid_pct, ready_pct, seed0;
    reg     fire_in;

    always #5 clk = ~clk;

    initial begin
        $readmemh(STIM_FILE, stim_mem);
        $readmemh(OUT_FILE, out3_mem);
        if (stim_mem[0] === 16'hxxxx) begin
            $display("  FAIL: vectors not found");
            $finish;
        end

        clk         = 1'b0;
        rst_n       = 1'b0;
        fc_in_data  = 16'd0;
        fc_in_valid = 1'b0;
        logit_ready = 1'b0;
        fed         = 0;
        idx3        = 0;
        fail_count  = 0;
        // +VALID40 +READY60 +SEED3 override the parameters; a generic override renames the top scope in xsim, which breaks the SAIF paths
        valid_pct   = VALID_PCT;
        ready_pct   = READY_PCT;
        seed0       = SEED;
        if ($value$plusargs("VALID%d", valid_pct)) ;
        if ($value$plusargs("READY%d", ready_pct)) ;
        if ($value$plusargs("SEED%d", seed0)) ;
        seed        = seed0;
        first_in    = -1;
        last_logit  = -1;

        // glbl holds the routed netlist in reset for its first 100 ns
        repeat (20) @(negedge clk);
        rst_n = 1'b1;
        @(negedge clk);

        for (cycle = 0; idx3 < NOUT3 && cycle < 400 * NSTIM; cycle = cycle + 1) begin
            fire_in     = (fed < NSTIM) && (fed < ((idx3 / LOGITS) + 1) * N_IN)
                          && (({$random(seed)} % 100) < $unsigned(valid_pct));
            fc_in_valid = fire_in;
            fc_in_data  = fire_in ? stim_mem[fed] : 16'hBAAD;
            logit_ready = ({$random(seed)} % 100) < $unsigned(ready_pct);
            #1;

            if (logit_valid === 1'b1 && logit_ready) begin
                if (logit_data !== out3_mem[idx3]) fail_count = fail_count + 1;
                idx3 = idx3 + 1;
            end

            if (fire_in && fc_in_ready) begin
                if (fed == 0) first_in = cycle;
                fed = fed + 1;
            end
            if (idx3 == LOGITS && last_logit < 0) last_logit = cycle;
            @(negedge clk);
        end

        if (idx3 != NOUT3) fail_count = fail_count + 1;
        $display("[%0s] fc_power valid=%0d%% ready=%0d%% seed=%0d : logits %0d/%0d, %0d failures, frame 1 %0d cycles",
                 (fail_count == 0) ? " ok " : "FAIL", valid_pct, ready_pct, seed0, idx3, NOUT3, fail_count,
                 last_logit - first_in);
        $finish;
    end

endmodule
