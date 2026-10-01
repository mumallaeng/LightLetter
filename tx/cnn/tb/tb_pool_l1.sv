`timescale 1ns / 1ps

// Self-checking testbench for pool_l1 (2x2 max pooling, stride 2).
//   - N_CH random IF_H x IF_W frames on 3 lanes, streamed back to back
//   - random out_valid gaps (VALID_PCT) and pool_ready backpressure (READY_PCT)
//   - every pooled output is compared with a behavioral 2x2 max model
//   - handshake check: out_ready / pool_valid / pool_ch_done vs. input position
//
// Vivado XSim (run from tb/cnn):
//   xvlog ../../rtl/cnn/max_logic.v ../../rtl/cnn/pool_datapath_l1.v \
//         ../../rtl/cnn/pool_ctrl_l1.v ../../rtl/cnn/pool_l1.v
//   xvlog -sv tb_pool_l1.sv
//   xelab tb_pool_l1 -s tb_pool_l1_sim
//   xsim tb_pool_l1_sim -runall
//   options: xelab ... -generic_top "VALID_PCT=100" -generic_top "READY_PCT=100"
//
// Waveform (GUI):
//   xelab tb_pool_l1 -s tb_pool_l1_sim -debug typical
//   xsim tb_pool_l1_sim -gui -tclbatch tb_pool_l1_wave.tcl
//
// Result files (written in the xsim run directory)
//   REPORT_FILE : per channel / lane, input grid (2x2 windows split, [xxxx] = window max)
//                 -> RTL output grid (mismatch marked with '*')
//   TRACE_FILE  : every input / output handshake with its sim time (= posedge in the waveform)

module tb_pool_l1;
    parameter IF_H        = 26;
    parameter IF_W        = 26;
    parameter N_CH        = 3;
    parameter VALID_PCT   = 70;  // probability that out_valid is driven in a cycle
    parameter READY_PCT   = 60;  // probability that pool_ready is high in a cycle
    parameter SEED        = 1;
    parameter REPORT_FILE = "tb_pool_l1_report.txt";
    parameter TRACE_FILE  = "tb_pool_l1_trace.txt";

    localparam LANES   = 3;
    localparam OF_H    = IF_H / 2;
    localparam OF_W    = IF_W / 2;
    localparam N_IN    = N_CH * IF_H * IF_W;
    localparam N_OUT   = N_CH * OF_H * OF_W;
    localparam TIMEOUT = N_IN * 400;  // cycles
    localparam MAX_ERR_PRINT = 20;

    // ========== DUT ==========
    logic        clk = 0;
    logic        rst_n = 0;
    logic [15:0] out_data[LANES];
    logic        out_valid = 0;
    logic        out_ready;
    logic        out_ch_done = 0;
    logic [15:0] pool_data[LANES];
    logic        pool_valid;
    logic        pool_ready = 0;
    logic        pool_ch_done;

    pool_l1 #(
        .IF_H(IF_H),
        .IF_W(IF_W)
    ) dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_data0   (out_data[0]),
        .out_data1   (out_data[1]),
        .out_data2   (out_data[2]),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .pool_data0  (pool_data[0]),
        .pool_data1  (pool_data[1]),
        .pool_data2  (pool_data[2]),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done)
    );

    always #5 clk = ~clk;

    // ========== Stimulus / Golden ==========
    logic [15:0] img[N_CH][LANES][IF_H][IF_W];
    logic [15:0] exp_out[N_CH][LANES][OF_H][OF_W];

    function automatic logic [15:0] max2(input logic [15:0] a, input logic [15:0] b);
        return (a > b) ? a : b;
    endfunction

    task automatic gen_data();
        for (int ch = 0; ch < N_CH; ch++)
            for (int l = 0; l < LANES; l++) begin
                for (int r = 0; r < IF_H; r++)
                    for (int c = 0; c < IF_W; c++)
                        // mix of full-range and small values (ties, zeros)
                        img[ch][l][r][c] = ($urandom_range(3) == 0) ? 16'($urandom_range(3))
                                                                    : 16'($urandom);
                for (int r = 0; r < OF_H; r++)
                    for (int c = 0; c < OF_W; c++)
                        exp_out[ch][l][r][c] = max2(max2(img[ch][l][2*r][2*c],   img[ch][l][2*r][2*c+1]),
                                                    max2(img[ch][l][2*r+1][2*c], img[ch][l][2*r+1][2*c+1]));
            end
    endtask

    // ========== Upstream Driver (conv layer side) ==========
    initial begin
        process::self().srandom(SEED);
        gen_data();
        foreach (out_data[l]) out_data[l] = 'x;

        repeat (3) @(posedge clk);
        rst_n <= 1;
        repeat (2) @(posedge clk);

        for (int ch = 0; ch < N_CH; ch++)
            for (int r = 0; r < IF_H; r++)
                for (int c = 0; c < IF_W; c++) begin
                    while ($urandom_range(99) >= VALID_PCT) @(posedge clk);  // idle gap

                    out_valid   <= 1;
                    out_ch_done <= (r == IF_H - 1) && (c == IF_W - 1);
                    for (int l = 0; l < LANES; l++) out_data[l] <= img[ch][l][r][c];

                    @(posedge clk);
                    while (!out_ready) @(posedge clk);  // hold until accepted

                    out_valid   <= 0;
                    out_ch_done <= 0;
                    for (int l = 0; l < LANES; l++) out_data[l] <= 'x;
                end
    end

    // ========== Downstream Ready (next layer side) ==========
    initial begin
        process::self().srandom(SEED + 1);
        forever begin
            @(posedge clk);
            pool_ready <= ($urandom_range(99) < READY_PCT);
        end
    end

    // ========== Monitor / Checker ==========
    int in_ch = 0, in_r = 0, in_c = 0;  // position of the pixel on out_data
    int in_cnt = 0, out_cnt = 0, err_cnt = 0, stall_cnt = 0;

    logic [15:0] got_out[N_CH][LANES][OF_H][OF_W];  // RTL output captured on pool handshake
    bit          got_vld[N_CH][OF_H][OF_W];
    time         got_time[N_CH][OF_H][OF_W];

    int tfd;  // trace file

    initial begin
        tfd = $fopen(TRACE_FILE, "w");
        $fdisplay(tfd, "pool_l1 handshake trace  (time = rising clk edge where the handshake happens,");
        $fdisplay(tfd, "                          data = value on the bus during the cycle just before it)");
        $fdisplay(tfd, "  IN    : out_valid & out_ready   (pixel accepted, data = out_data0..2)");
        $fdisplay(tfd, "  OUT   : pool_valid & pool_ready (pooled output,  data = pool_data0..2 | expected)");
        $fdisplay(tfd, "  STALL : pool_valid & ~pool_ready (output held, out_ready = 0)");
        $fdisplay(tfd, "");
        $fdisplay(tfd, "  time(ns)  event  ch  position    lane0 lane1 lane2  | exp0  exp1  exp2");
        $fdisplay(tfd, "  --------  -----  --  ----------  ----- ----- -----  | ----- ----- -----");
    end

    task automatic error(input string msg);
        err_cnt++;
        if (err_cnt <= MAX_ERR_PRINT) $display("[FAIL] %0t : %s", $time, msg);
    endtask

    // ========== Result Report ==========
    function automatic string col_header(input int n, input int w);
        string s = "       ";
        for (int c = 0; c < n; c++) begin
            if (c % 2 == 0 && c > 0 && w == 2) s = {s, " |"};
            s = {s, $sformatf("  c%-3d ", c)};
        end
        return s;
    endfunction

    function automatic string h_line(input int n, input int w);
        string s = "      +";
        for (int c = 0; c < n; c++) begin
            if (c % 2 == 0 && c > 0 && w == 2) s = {s, "-+"};
            s = {s, "-------"};
        end
        return s;
    endfunction

    task automatic write_report();
        int fd, mis_cnt;
        string line;

        fd = $fopen(REPORT_FILE, "w");
        if (fd == 0) begin
            $display("[WARN] cannot open %s", REPORT_FILE);
            return;
        end

        $fdisplay(fd, "pool_l1 test report  (2x2 max pooling, stride 2, values in hex)");
        $fdisplay(fd, "  input  : %0d x %0d  x %0d ch x %0d lanes", IF_H, IF_W, N_CH, LANES);
        $fdisplay(fd, "  output : %0d x %0d", OF_H, OF_W);
        $fdisplay(fd, "  VALID_PCT=%0d READY_PCT=%0d SEED=%0d", VALID_PCT, READY_PCT, SEED);
        $fdisplay(fd, "  input %0d / %0d, output %0d / %0d, stall %0d cycles", in_cnt, N_IN, out_cnt,
                  N_OUT, stall_cnt);
        if (err_cnt == 0) $fdisplay(fd, "  RESULT : PASS");
        else $fdisplay(fd, "  RESULT : FAIL (%0d errors)", err_cnt);
        $fdisplay(fd, "");
        $fdisplay(fd, "  INPUT  : '|' and '-' split 2x2 windows, [xxxx] = max of the window");
        $fdisplay(fd, "  OUTPUT : RTL value, '*' = mismatch with expected, ---- = no output");
        $fdisplay(fd, "  time of every input / output handshake : %s", TRACE_FILE);

        for (int ch = 0; ch < N_CH; ch++) begin
            for (int l = 0; l < LANES; l++) begin
                $fdisplay(fd, "");
                $fdisplay(fd, "==================================================================");
                $fdisplay(fd, " CH %0d / LANE %0d", ch, l);
                $fdisplay(fd, "==================================================================");

                // ----- input grid -----
                $fdisplay(fd, "[INPUT %0dx%0d]", IF_H, IF_W);
                $fdisplay(fd, "%s", col_header(IF_W, 2));
                for (int r = 0; r < IF_H; r++) begin
                    if (r % 2 == 0 && r > 0) $fdisplay(fd, "%s", h_line(IF_W, 2));
                    line = $sformatf(" r%-3d |", r);
                    for (int c = 0; c < IF_W; c++) begin
                        automatic bit is_max = 0;
                        if (c % 2 == 0 && c > 0) line = {line, " |"};
                        // first pixel (raster order) that equals the window max
                        if (r / 2 < OF_H && c / 2 < OF_W) begin
                            automatic int wr = r / 2 * 2, wc = c / 2 * 2;
                            automatic logic [15:0] m = exp_out[ch][l][r/2][c/2];
                            for (int i = 0; i < 4; i++)
                                if (img[ch][l][wr+i/2][wc+i%2] == m) begin
                                    is_max = (wr + i / 2 == r) && (wc + i % 2 == c);
                                    break;
                                end
                        end
                        if (is_max) line = {line, $sformatf(" [%h]", img[ch][l][r][c])};
                        else line = {line, $sformatf("  %h ", img[ch][l][r][c])};
                    end
                    $fdisplay(fd, "%s", line);
                end

                // ----- output grid -----
                mis_cnt = 0;
                $fdisplay(fd, "");
                $fdisplay(fd, "[OUTPUT %0dx%0d]  (RTL)", OF_H, OF_W);
                $fdisplay(fd, "%s", col_header(OF_W, 1));
                for (int r = 0; r < OF_H; r++) begin
                    line = $sformatf(" r%-3d |", r);
                    for (int c = 0; c < OF_W; c++) begin
                        if (!got_vld[ch][r][c]) begin
                            line = {line, "  ---- "};
                            mis_cnt++;
                        end else if (got_out[ch][l][r][c] !== exp_out[ch][l][r][c]) begin
                            line = {line, $sformatf("  %h*", got_out[ch][l][r][c])};
                            mis_cnt++;
                        end else begin
                            line = {line, $sformatf("  %h ", got_out[ch][l][r][c])};
                        end
                    end
                    $fdisplay(fd, "%s", line);
                end

                // ----- mismatch list -----
                if (mis_cnt == 0) begin
                    $fdisplay(fd, "  -> all %0d outputs match", OF_H * OF_W);
                end else begin
                    $fdisplay(fd, "  -> %0d mismatch(es)", mis_cnt);
                    for (int r = 0; r < OF_H; r++)
                        for (int c = 0; c < OF_W; c++)
                            if (!got_vld[ch][r][c])
                                $fdisplay(fd, "     out(%0d,%0d) : got ----  exp %h", r, c,
                                          exp_out[ch][l][r][c]);
                            else if (got_out[ch][l][r][c] !== exp_out[ch][l][r][c])
                                $fdisplay(fd, "     out(%0d,%0d) : got %h  exp %h  @ %0d ns", r, c,
                                          got_out[ch][l][r][c], exp_out[ch][l][r][c],
                                          got_time[ch][r][c]);
                end
            end
        end

        $fclose(fd);
        $display(" report : %s", REPORT_FILE);
    endtask

    always @(posedge clk) begin
        if (rst_n) begin
            automatic bit win      = (in_r % 2 == 1) && (in_c % 2 == 1);
            automatic bit win_last = win && (in_r == OF_H * 2 - 1) && (in_c == OF_W * 2 - 1);

            // ----- handshake check -----
            if (out_ready !== (pool_ready | ~win))
                error($sformatf("out_ready=%b exp=%b (ch%0d r%0d c%0d pool_ready=%b)",
                                out_ready, pool_ready | ~win, in_ch, in_r, in_c, pool_ready));
            if (pool_valid !== (out_valid & win))
                error($sformatf("pool_valid=%b exp=%b (ch%0d r%0d c%0d)",
                                pool_valid, out_valid & win, in_ch, in_r, in_c));
            if (pool_ch_done !== (out_valid & win_last))
                error($sformatf("pool_ch_done=%b exp=%b (ch%0d r%0d c%0d)",
                                pool_ch_done, out_valid & win_last, in_ch, in_r, in_c));

            if (pool_valid === 1'b1 && pool_ready !== 1'b1) begin
                stall_cnt++;
                $fdisplay(tfd, "  %8d  STALL  %2d  out(%2d,%2d)  %h  %h  %h   (held, out_ready=0)",
                          $time, in_ch, in_r / 2, in_c / 2, pool_data[0], pool_data[1], pool_data[2]);
            end

            // ----- trace : input accepted -----
            if (out_valid === 1'b1 && out_ready === 1'b1)
                $fdisplay(tfd, "  %8d  IN     %2d  in (%2d,%2d)  %h  %h  %h", $time, in_ch, in_r, in_c,
                          out_data[0], out_data[1], out_data[2]);

            // ----- output data check -----
            if (pool_valid === 1'b1 && pool_ready === 1'b1) begin
                if (in_ch < N_CH) begin
                    for (int l = 0; l < LANES; l++) got_out[in_ch][l][in_r/2][in_c/2] = pool_data[l];
                    got_vld[in_ch][in_r/2][in_c/2]  = 1;
                    got_time[in_ch][in_r/2][in_c/2] = $time;
                end
                $fwrite(tfd, "  %8d  OUT    %2d  out(%2d,%2d)  %h  %h  %h   | %h  %h  %h", $time, in_ch,
                        in_r / 2, in_c / 2, pool_data[0], pool_data[1], pool_data[2],
                        exp_out[in_ch][0][in_r/2][in_c/2], exp_out[in_ch][1][in_r/2][in_c/2],
                        exp_out[in_ch][2][in_r/2][in_c/2]);
                if ({pool_data[0], pool_data[1], pool_data[2]} ===
                    {exp_out[in_ch][0][in_r/2][in_c/2], exp_out[in_ch][1][in_r/2][in_c/2],
                     exp_out[in_ch][2][in_r/2][in_c/2]})
                    $fdisplay(tfd, "  OK");
                else $fdisplay(tfd, "  MISMATCH");
                for (int l = 0; l < LANES; l++)
                    if (pool_data[l] !== exp_out[in_ch][l][in_r/2][in_c/2])
                        error($sformatf("lane%0d ch%0d out(%0d,%0d) : got %h exp %h",
                                        l, in_ch, in_r / 2, in_c / 2, pool_data[l],
                                        exp_out[in_ch][l][in_r/2][in_c/2]));
                out_cnt++;
            end

            // ----- advance input position -----
            if (out_valid === 1'b1 && out_ready === 1'b1) begin
                in_cnt++;
                if (in_c == IF_W - 1) begin
                    in_c = 0;
                    if (in_r == IF_H - 1) begin
                        in_r = 0;
                        in_ch++;
                    end else begin
                        in_r++;
                    end
                end else begin
                    in_c++;
                end
            end
        end
    end

    // ========== Finish ==========
    initial begin
        fork
            wait (in_cnt == N_IN && out_cnt == N_OUT);
            begin
                repeat (TIMEOUT) @(posedge clk);
                error($sformatf("timeout : in %0d/%0d, out %0d/%0d", in_cnt, N_IN, out_cnt, N_OUT));
            end
        join_any
        disable fork;
        repeat (20) @(posedge clk);  // catch extra outputs

        if (in_cnt != N_IN) error($sformatf("input count %0d != %0d", in_cnt, N_IN));
        if (out_cnt != N_OUT) error($sformatf("output count %0d != %0d", out_cnt, N_OUT));

        $display("----------------------------------------");
        $display(" pool_l1 : %0dx%0d x %0d ch x %0d lanes", IF_H, IF_W, N_CH, LANES);
        $display(" VALID_PCT=%0d READY_PCT=%0d SEED=%0d", VALID_PCT, READY_PCT, SEED);
        $display(" input %0d, output %0d, stall %0d cycles", in_cnt, out_cnt, stall_cnt);
        if (err_cnt == 0) $display(" RESULT : PASS");
        else $display(" RESULT : FAIL (%0d errors)", err_cnt);
        write_report();
        $fclose(tfd);
        $display(" trace  : %s", TRACE_FILE);
        $display("----------------------------------------");
        $finish;
    end
endmodule
