`timescale 1ns / 1ps

// cnn_top (conv_l1 -> pool_l1) checked against the C golden vectors in rtl/cnn/rtl_ref/.
//
//   input 28x28 -> conv1 3x3 (6 och) + ReLU/quant -> 26x26x6 -> maxpool 2x2 -> 13x13x6
//
// Vectors (rtl/cnn/rtl_ref/, see README.md there) - FRAMES = 2:
//   frame 0 = the real EMNIST sample of the Python model ('6'), frame 1 = the same image flipped L/R
//   ce1_stim.mem          16 bit x 1568   conv_l1 pixel input, 28x28 raster per frame
//   ce1_out.mem           49 bit x 2704   conv_l1 output = pool_l1 input  {ch_done, d2, d1, d0}
//   pool1_out.mem         49 bit x  676   pool_l1 output                   {ch_done, d2, d1, d0}
//   conv1_weight_144.mem 144 bit x    6   weight ROM entries
//   conv1_bias_ce.mem     32 bit x    6   INT32 bias
//   entry order per frame: pass0 = {och0,1,2} raster -> pass1 = {och3,4,5} raster
//
// Run: run_sim.bat   (Vivado 2020.2 XSim, from this folder)
//
// Every stage is checked on the fly and logged to TRACE_FILE:
//   [PIX ] AXIS pixel handshake      frame / pixel (row,col) / value
//   [WIN ] line_buffer 3x3 window    window taps vs. the stim image at (row,col)
//   [MAC ] mac_array_l1 per och      ch_result vs. sum(window x weight) computed by the TB
//   [SUM ] output_buffer             sum_data  vs. MAC result + bias[och]
//   [CONV] relu_quant -> pool_l1     out_data  vs. ReLU / >>16 (round-half-even) of the sum, vs. ce1_out.mem
//   [POOL] pool_l1 output            pool_data vs. max of the 4 conv values, vs. pool1_out.mem
// The console shows the same flow for the FOCUS_* window of every frame plus the summary.
//
// Results (written in this folder):
//   rtl_conv1_out.txt / gold_conv1_out.txt   frame x och 26x26 matrices
//   rtl_pool1_out.txt / gold_pool1_out.txt   frame x och 13x13 matrices
//        '*' after an RTL value = differs from the golden value
//   compare_report.txt                       summary + every mismatch
//
// TB workarounds (RTL is not modified):
//   cnn_top declares pool_ready as an OUTPUT, but pool_l1 takes it as an input, so nothing drives it.
//   The TB forces dut.pool_ready = 1 (no backpressure from the next layer).
//   conv_out_stage hard-codes "conv1_bias.mem" (an older bias set); the TB loads conv1_bias_ce.mem
//   into the bias ROM, the same as tb_conv_l1.v does.
//   Frame gate (rtl_ref/README.md): the next frame starts only after conv_l1 has sent out the
//   previous frame completely.

module tb_cnn_top_layer1;

    // ---------------- parameters ----------------
    parameter REF_DIR     = "../../../rtl/cnn/rtl_ref/";
    parameter FRAMES      = 2;
    parameter TRACE_FILE  = "sim_trace.log";
    parameter REPORT_FILE = "compare_report.txt";

    parameter FOCUS_R    = 8;  // conv window whose whole flow is printed on the console
    parameter FOCUS_C    = 10;
    parameter MAX_CYCLES = 400000;
    parameter MAX_LIST   = 30;  // mismatches listed in the report

    localparam OCH       = 6;
    localparam IMG_W     = 28;
    localparam CONV_W    = 26;
    localparam POOL_W    = 13;
    localparam N_PIX     = IMG_W * IMG_W;  // 784  per frame
    localparam N_WIN     = CONV_W * CONV_W;  // 676
    localparam N_POOLPIX = POOL_W * POOL_W;  // 169
    localparam N_CONV    = N_WIN * OCH / 3;  // 1352 conv entries per frame (3 lanes)
    localparam N_POOL    = N_POOLPIX * OCH / 3;  // 338  pool entries per frame
    localparam N_MAC     = N_WIN * OCH;  // 4056 conv values per frame
    localparam N_PV      = N_POOLPIX * OCH;  // 1014 pool values per frame

    // ---------------- DUT ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;
    wire        pool_valid;
    wire        pool_ready;
    wire        pool_ch_done;
    wire [15:0] pool_data0, pool_data1, pool_data2;

    cnn_top dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .pool_valid   (pool_valid),
        .pool_ready   (pool_ready),
        .pool_ch_done (pool_ch_done),
        .pool_data0   (pool_data0),
        .pool_data1   (pool_data1),
        .pool_data2   (pool_data2)
    );

    // pool_ready is an undriven output of cnn_top -> drive the net from here
    initial force dut.pool_ready = 1'b1;

    always #5 clk = ~clk;

    // ---------------- golden data (rtl_ref) ----------------
    reg        [ 15:0] stim     [0:FRAMES*N_PIX-1];
    reg        [ 48:0] ce1_out  [0:FRAMES*N_CONV-1];
    reg        [ 48:0] pool1_out[0:FRAMES*N_POOL-1];
    reg        [143:0] wref     [0:OCH-1];
    reg signed [ 31:0] bias     [0:OCH-1];

    // unpacked golden / captured RTL values, index = frame*N + och*H*W + r*W + c
    reg        [ 15:0] gold_conv[0:FRAMES*N_MAC-1];
    reg        [ 15:0] gold_pool[0:FRAMES*N_PV-1];
    reg        [ 15:0] rtl_conv [0:FRAMES*N_MAC-1];
    reg        [ 15:0] rtl_pool [0:FRAMES*N_PV-1];

    // per (frame, window, och): index = frame*N_MAC + win*6 + och
    reg signed [ 35:0] mac_res  [0:FRAMES*N_MAC-1];
    reg signed [ 39:0] sum_res  [0:FRAMES*N_MAC-1];

    // MAC expectation FIFO (cal_valid -> mac_valid is a 2-stage pipeline)
    reg signed [ 35:0] mac_exp_q[0:15];
    reg        [ 15:0] mac_idx_q[0:15];
    integer q_wr, q_rd;

    // ---------------- counters ----------------
    integer fd, cyc, running, i, k, ch, r, c, f;
    integer pix, n_win, n_mac, n_sum, n_conv, n_pool;
    integer cur_win;
    integer err_win, err_wgt, err_mac, err_sum, err_quant, err_cdone, err_pdone, err_max;
    integer conv_err, pool_err;
    integer t_pix0, t_pix1, t_win0, t_win1, t_mac0, t_mac1, t_sum0, t_sum1;
    integer t_conv0, t_conv1, t_pool0, t_pool1;

    // ---------------- helpers ----------------
    function signed [15:0] tap;  // 16-bit tap k of a 144-bit window / weight word
        input [143:0] word;
        input integer k;
        tap = word[16*k+:16];
    endfunction

    function [15:0] quant;  // ReLU -> >>16 round-half-to-even -> 32767 clamp
        input signed [39:0] s;
        reg [39:0] x, q;
        begin
            x     = (s < 0) ? 40'd0 : s;
            q     = (x + 40'h7FFF + ((x >> 16) & 40'd1)) >> 16;
            quant = (q > 40'd32767) ? 16'd32767 : q[15:0];
        end
    endfunction

    function [15:0] max4;
        input [15:0] a, b, c, d;
        reg [15:0] m;
        begin
            m = a;
            if (b > m) m = b;
            if (c > m) m = c;
            if (d > m) m = d;
            max4 = m;
        end
    endfunction

    function focus_win;  // w = window index inside a frame
        input integer w;
        focus_win = (w == FOCUS_R * CONV_W + FOCUS_C);
    endfunction

    function focus_pool;
        input integer p;
        focus_pool = (p == (FOCUS_R / 2) * POOL_W + FOCUS_C / 2);
    endfunction

    // ---------------- AXIS source ----------------
    // frame gate: frame n starts after conv_l1 has sent n * N_CONV entries
    wire frame_gate = (pix % N_PIX == 0) && (pix > 0) && (n_conv < (pix / N_PIX) * N_CONV);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) pix <= 0;
        else if (s_axis_tvalid && s_axis_tready) pix <= pix + 1;
    end

    always @(*) begin
        s_axis_tvalid = running && (pix < FRAMES * N_PIX) && !frame_gate;
        s_axis_tdata  = (pix < FRAMES * N_PIX) ? stim[pix] : 16'd0;
        s_axis_tuser  = (pix % N_PIX == 0);
        s_axis_tlast  = (pix % N_PIX == N_PIX - 1);
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // ========================================================================
    // [PIX] pixel handshake
    // ========================================================================
    always @(posedge clk) begin
        if (rst_n && s_axis_tvalid && s_axis_tready) begin
            if (t_pix0 < 0) t_pix0 = cyc;
            t_pix1 = cyc;
            $fdisplay(fd, "cyc %6d [PIX ] f%0d #%3d (r%2d,c%2d) = %5d%0s%0s", cyc, pix / N_PIX,
                      pix % N_PIX, (pix % N_PIX) / IMG_W, pix % IMG_W, stim[pix],
                      s_axis_tuser ? "  tuser" : "", s_axis_tlast ? "  tlast" : "");
        end
    end

    // ========================================================================
    // [WIN] + [MAC] input side : every cal_valid cycle = one (window, och) product
    // ========================================================================
    reg signed [35:0] dot;
    reg        [143:0] win_exp;
    integer wf, ww, wr, wc, och;

    always @(posedge clk) begin
        if (rst_n && dut.U_CONV_L1.cal_valid) begin
            och = dut.U_CONV_L1.out_ch_sel;
            if (och == 0) begin
                cur_win = n_win;
                n_win   = n_win + 1;
                if (t_win0 < 0) t_win0 = cyc;
                t_win1 = cyc;
            end
            wf = cur_win / N_WIN;
            ww = cur_win % N_WIN;
            wr = ww / CONV_W;
            wc = ww % CONV_W;

            // window the line buffer should present: image[wr..wr+2][wc..wc+2] of frame wf
            for (k = 0; k < 9; k = k + 1)
            win_exp[16*k+:16] = stim[wf*N_PIX+(wr+k/3)*IMG_W+wc+k%3];

            if (och == 0) begin
                if (dut.U_CONV_L1.win_out !== win_exp) err_win = err_win + 1;
                $fdisplay(fd,
                          "cyc %6d [WIN ] f%0d win#%3d (r%2d,c%2d) taps = %5d %5d %5d | %5d %5d %5d | %5d %5d %5d  %0s",
                          cyc, wf, ww, wr, wc, tap(dut.U_CONV_L1.win_out, 0),
                          tap(dut.U_CONV_L1.win_out, 1), tap(dut.U_CONV_L1.win_out, 2),
                          tap(dut.U_CONV_L1.win_out, 3), tap(dut.U_CONV_L1.win_out, 4),
                          tap(dut.U_CONV_L1.win_out, 5), tap(dut.U_CONV_L1.win_out, 6),
                          tap(dut.U_CONV_L1.win_out, 7), tap(dut.U_CONV_L1.win_out, 8),
                          (dut.U_CONV_L1.win_out === win_exp) ? "== image" : "!= image  <-- ERR");
                if (focus_win(ww)) begin
                    $display("\n  ---------------- frame %0d ----------------", wf);
                    $display("  cyc %0d  [WIN ] line_buffer window #%0d (r%0d,c%0d) %0s", cyc, ww, wr,
                             wc, (dut.U_CONV_L1.win_out === win_exp) ? "== stim image window" :
                             "!= stim image window  <-- ERR");
                    for (k = 0; k < 3; k = k + 1)
                    $display("            | %6d %6d %6d |", tap(dut.U_CONV_L1.win_out, 3 * k),
                             tap(dut.U_CONV_L1.win_out, 3 * k + 1),
                             tap(dut.U_CONV_L1.win_out, 3 * k + 2));
                end
            end

            if (dut.U_CONV_L1.weight_out !== wref[och]) err_wgt = err_wgt + 1;

            dot = 0;
            for (k = 0; k < 9; k = k + 1)
            dot = dot + tap(dut.U_CONV_L1.win_out, k) * tap(dut.U_CONV_L1.weight_out, k);

            mac_exp_q[q_wr%16] = dot;
            mac_idx_q[q_wr%16] = cur_win * OCH + och;
            q_wr               = q_wr + 1;

            if (focus_win(ww))
                $display("  cyc %0d  [MAC ] och%0d start  w = %6d %6d %6d | %6d %6d %6d | %6d %6d %6d   TB sum(x*w) = %0d",
                         cyc, och, tap(dut.U_CONV_L1.weight_out, 0),
                         tap(dut.U_CONV_L1.weight_out, 1), tap(dut.U_CONV_L1.weight_out, 2),
                         tap(dut.U_CONV_L1.weight_out, 3), tap(dut.U_CONV_L1.weight_out, 4),
                         tap(dut.U_CONV_L1.weight_out, 5), tap(dut.U_CONV_L1.weight_out, 6),
                         tap(dut.U_CONV_L1.weight_out, 7), tap(dut.U_CONV_L1.weight_out, 8), dot);
        end
    end

    // ========================================================================
    // [MAC] output side
    // ========================================================================
    reg signed [35:0] mac_e;
    integer mi;

    always @(posedge clk) begin
        if (rst_n && dut.U_CONV_L1.mac_valid) begin
            if (t_mac0 < 0) t_mac0 = cyc;
            t_mac1 = cyc;
            if (q_rd == q_wr) begin
                err_mac = err_mac + 1;
                $fdisplay(fd, "cyc %6d [MAC ] mac_valid with no pending window  <-- ERR", cyc);
            end else begin
                mac_e = mac_exp_q[q_rd%16];
                mi    = mac_idx_q[q_rd%16];
                q_rd  = q_rd + 1;
                mac_res[mi] = dut.U_CONV_L1.ch_result;
                if (dut.U_CONV_L1.ch_result !== mac_e) err_mac = err_mac + 1;
                $fdisplay(fd, "cyc %6d [MAC ] f%0d win#%3d och%0d  ch_result = %11d   TB = %11d  %0s", cyc,
                          mi / N_MAC, (mi % N_MAC) / OCH, mi % OCH,
                          $signed(dut.U_CONV_L1.ch_result), mac_e,
                          (dut.U_CONV_L1.ch_result === mac_e) ? "ok" : "<-- ERR");
                if (focus_win((mi % N_MAC) / OCH))
                    $display("  cyc %0d  [MAC ] och%0d done   ch_result = %0d  %0s", cyc, mi % OCH,
                             $signed(dut.U_CONV_L1.ch_result),
                             (dut.U_CONV_L1.ch_result === mac_e) ? "(== TB)" : "<-- ERR");
            end
            n_mac = n_mac + 1;
        end
    end

    // ========================================================================
    // [SUM] output_buffer: sum_data = MAC + bias   (order: pixel -> och)
    // ========================================================================
    reg signed [39:0] sum_e;

    always @(posedge clk) begin
        if (rst_n && dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_valid) begin
            if (t_sum0 < 0) t_sum0 = cyc;
            t_sum1 = cyc;
            if (n_sum < FRAMES * N_MAC) begin
                sum_e = mac_res[n_sum] + bias[n_sum%OCH];
                sum_res[n_sum] = dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data;
                if (dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data !== sum_e) err_sum = err_sum + 1;
                $fdisplay(fd, "cyc %6d [SUM ] f%0d win#%3d och%0d  sum_data = %11d   mac + bias = %11d + %10d  %0s",
                          cyc, n_sum / N_MAC, (n_sum % N_MAC) / OCH, n_sum % OCH,
                          dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data, mac_res[n_sum], bias[n_sum%OCH],
                          (dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data === sum_e) ? "ok" : "<-- ERR");
                if (focus_win((n_sum % N_MAC) / OCH))
                    $display("  cyc %0d  [SUM ] och%0d  output_buffer sum_data = %0d  (= mac %0d + bias %0d) %0s",
                             cyc, n_sum % OCH, dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data,
                             mac_res[n_sum], bias[n_sum%OCH],
                             (dut.U_CONV_L1.U_OUTPUT_STAGE_L1.sum_data === sum_e) ? "ok" : "<-- ERR");
            end
            n_sum = n_sum + 1;
        end
    end

    // ========================================================================
    // [CONV] conv_l1 -> pool_l1  and  [POOL] pool_l1 output
    // (one block so the pool check sees the conv value captured on the same edge)
    // ========================================================================
    reg [15:0] cv[0:2];
    reg [15:0] pv[0:2];
    reg [48:0] g;
    reg [15:0] q_e, a0, a1, a2, a3, mx;
    integer fr, e, pass, p, ci, pr, pc, base;

    always @(posedge clk) begin
        // ---------- conv output ----------
        if (rst_n && dut.out_valid && dut.out_ready) begin
            if (t_conv0 < 0) t_conv0 = cyc;
            t_conv1 = cyc;
            if (n_conv < FRAMES * N_CONV) begin
                fr    = n_conv / N_CONV;
                e     = n_conv % N_CONV;
                pass  = e / N_WIN;
                p     = e % N_WIN;
                g     = ce1_out[n_conv];
                cv[0] = dut.out_data0;
                cv[1] = dut.out_data1;
                cv[2] = dut.out_data2;
                if (dut.out_ch_done !== g[48]) err_cdone = err_cdone + 1;
                for (k = 0; k < 3; k = k + 1) begin
                    ch = pass * 3 + k;
                    ci = fr * N_MAC + ch * N_WIN + p;
                    rtl_conv[ci] = cv[k];
                    q_e = quant(sum_res[fr*N_MAC+p*OCH+ch]);
                    if (cv[k] !== q_e) err_quant = err_quant + 1;
                    if (cv[k] !== g[16*k+:16]) conv_err = conv_err + 1;
                    $fdisplay(fd,
                              "cyc %6d [CONV] f%0d och%0d (r%2d,c%2d)  out = %5d   quant(sum %11d) = %5d  golden %5d  %0s%0s",
                              cyc, fr, ch, p / CONV_W, p % CONV_W, cv[k],
                              sum_res[fr*N_MAC+p*OCH+ch], q_e, g[16*k+:16],
                              (cv[k] === g[16*k+:16]) ? "ok" : "<-- ERR",
                              (cv[k] === q_e) ? "" : "  quant <-- ERR");
                    if (focus_win(p))
                        $display("  cyc %0d  [CONV] och%0d  ReLU/>>16 -> out_data%0d = %0d   golden %0d  %0s",
                                 cyc, ch, k, cv[k], g[16*k+:16],
                                 (cv[k] === g[16*k+:16]) ? "ok" : "<-- ERR");
                end
                if (dut.out_ch_done)
                    $fdisplay(fd, "cyc %6d [CONV] f%0d out_ch_done  (och%0d..%0d finished)  golden ch_done=%b",
                              cyc, fr, pass * 3, pass * 3 + 2, g[48]);
            end
            n_conv = n_conv + 1;
        end

        // ---------- pool output ----------
        if (rst_n && pool_valid && pool_ready) begin
            if (t_pool0 < 0) t_pool0 = cyc;
            t_pool1 = cyc;
            if (n_pool < FRAMES * N_POOL) begin
                fr    = n_pool / N_POOL;
                e     = n_pool % N_POOL;
                pass  = e / N_POOLPIX;
                p     = e % N_POOLPIX;
                pr    = p / POOL_W;
                pc    = p % POOL_W;
                g     = pool1_out[n_pool];
                pv[0] = pool_data0;
                pv[1] = pool_data1;
                pv[2] = pool_data2;
                if (pool_ch_done !== g[48]) err_pdone = err_pdone + 1;
                for (k = 0; k < 3; k = k + 1) begin
                    ch = pass * 3 + k;
                    ci = fr * N_PV + ch * N_POOLPIX + p;
                    rtl_pool[ci] = pv[k];
                    base = fr * N_MAC + ch * N_WIN;
                    a0 = rtl_conv[base+(2*pr)*CONV_W+2*pc];
                    a1 = rtl_conv[base+(2*pr)*CONV_W+2*pc+1];
                    a2 = rtl_conv[base+(2*pr+1)*CONV_W+2*pc];
                    a3 = rtl_conv[base+(2*pr+1)*CONV_W+2*pc+1];
                    mx = max4(a0, a1, a2, a3);
                    if (pv[k] !== mx) err_max = err_max + 1;
                    if (pv[k] !== g[16*k+:16]) pool_err = pool_err + 1;
                    $fdisplay(fd,
                              "cyc %6d [POOL] f%0d och%0d (r%2d,c%2d)  out = %5d   max(%5d %5d %5d %5d) = %5d  golden %5d  %0s%0s",
                              cyc, fr, ch, pr, pc, pv[k], a0, a1, a2, a3, mx, g[16*k+:16],
                              (pv[k] === g[16*k+:16]) ? "ok" : "<-- ERR",
                              (pv[k] === mx) ? "" : "  max <-- ERR");
                    if (focus_pool(p))
                        $display("  cyc %0d  [POOL] och%0d (r%0d,c%0d) pool_data%0d = %0d = max(%0d %0d %0d %0d)   golden %0d  %0s",
                                 cyc, ch, pr, pc, k, pv[k], a0, a1, a2, a3, g[16*k+:16],
                                 (pv[k] === g[16*k+:16]) ? "ok" : "<-- ERR");
                end
                if (pool_ch_done)
                    $fdisplay(fd, "cyc %6d [POOL] f%0d pool_ch_done  (och%0d..%0d finished)  golden ch_done=%b",
                              cyc, fr, pass * 3, pass * 3 + 2, g[48]);
            end
            n_pool = n_pool + 1;
        end
    end

    // ========================================================================
    // matrix writer: is_pool 0/1, is_gold 0 (RTL, '*' = differs from golden) / 1 (golden)
    // ========================================================================
    task write_matrix;
        input integer is_pool;
        input integer is_gold;
        integer fh, w, n, idx;
        reg [15:0] v, gv;
        begin
            case (is_pool * 2 + is_gold)
                0: fh = $fopen("rtl_conv1_out.txt", "w");
                1: fh = $fopen("gold_conv1_out.txt", "w");
                2: fh = $fopen("rtl_pool1_out.txt", "w");
                default: fh = $fopen("gold_pool1_out.txt", "w");
            endcase
            w = is_pool ? POOL_W : CONV_W;
            n = w * w * OCH;
            $fdisplay(fh, "%0s %0s output  %0d frame x %0d och x %0dx%0d", is_gold ? "golden" : "RTL",
                      is_pool ? "pool1" : "conv1", FRAMES, OCH, w, w);
            $fdisplay(fh, "INT16, real value = n / 2^13   frame 0 = EMNIST sample, frame 1 = flipped L/R");
            $fdisplay(fh, "source: %0s", is_gold ? (is_pool ? "rtl_ref/pool1_out.mem" : "rtl_ref/ce1_out.mem") :
                      "cnn_top simulation   ('*' = differs from golden)");
            for (f = 0; f < FRAMES; f = f + 1)
            for (ch = 0; ch < OCH; ch = ch + 1) begin
                $fdisplay(fh, "\n[frame %0d  och %0d]  %0dx%0d", f, ch, w, w);
                $fwrite(fh, "       ");
                for (c = 0; c < w; c = c + 1) $fwrite(fh, "    c%2d ", c);
                $fwrite(fh, "\n");
                for (r = 0; r < w; r = r + 1) begin
                    $fwrite(fh, " r%2d  |", r);
                    for (c = 0; c < w; c = c + 1) begin
                        idx = f * n + ch * w * w + r * w + c;
                        v   = is_pool ? rtl_pool[idx] : rtl_conv[idx];
                        gv  = is_pool ? gold_pool[idx] : gold_conv[idx];
                        if (is_gold) $fwrite(fh, " %6d ", gv);
                        else $fwrite(fh, " %6d%0s", v, (v !== gv) ? "*" : " ");
                    end
                    $fwrite(fh, "\n");
                end
            end
            $fclose(fh);
        end
    endtask

    task list_diffs;
        input integer fh;
        input integer is_pool;
        integer w, n, idx, shown;
        reg [15:0] v, gv;
        begin
            w     = is_pool ? POOL_W : CONV_W;
            n     = w * w * OCH;
            shown = 0;
            for (idx = 0; idx < FRAMES * n; idx = idx + 1) begin
                v  = is_pool ? rtl_pool[idx] : rtl_conv[idx];
                gv = is_pool ? gold_pool[idx] : gold_conv[idx];
                if (v !== gv && shown < MAX_LIST) begin
                    shown = shown + 1;
                    $fdisplay(fh, "    %0s f%0d och%0d (r%0d,c%0d): RTL %0d  golden %0d",
                              is_pool ? "pool1" : "conv1", idx / n, (idx % n) / (w * w),
                              (idx % (w * w)) / w, idx % w, v, gv);
                end
            end
        end
    endtask

    task summary;
        input integer fh;
        begin
            $fdisplay(fh, "============ cnn_top layer1 : rtl_ref golden vectors, %0d frame(s) ============", FRAMES);
            $fdisplay(fh, "signal flow (cycle = posedge count after reset)");
            $fdisplay(fh, "  stage                        count      first cyc  last cyc   check");
            $fdisplay(fh, "  [PIX ] AXIS pixel in         %4d/%4d  %8d  %8d   -", pix, FRAMES * N_PIX,
                      t_pix0, t_pix1);
            $fdisplay(fh, "  [WIN ] line_buffer window    %4d/%4d  %8d  %8d   window != stim image : %0d",
                      n_win, FRAMES * N_WIN, t_win0, t_win1, err_win);
            $fdisplay(fh, "  [MAC ] mac_array_l1          %4d/%4d  %8d  %8d   weight != rom_ref : %0d, ch_result != sum(x*w) : %0d",
                      n_mac, FRAMES * N_MAC, t_mac0, t_mac1, err_wgt, err_mac);
            $fdisplay(fh, "  [SUM ] output_buffer         %4d/%4d  %8d  %8d   sum != mac + bias : %0d", n_sum,
                      FRAMES * N_MAC, t_sum0, t_sum1, err_sum);
            $fdisplay(fh, "  [CONV] relu_quant (x3 lane)  %4d/%4d  %8d  %8d   out != quant(sum) : %0d, ch_done wrong : %0d",
                      n_conv, FRAMES * N_CONV, t_conv0, t_conv1, err_quant, err_cdone);
            $fdisplay(fh, "  [POOL] pool_l1    (x3 lane)  %4d/%4d  %8d  %8d   out != max(2x2) : %0d, ch_done wrong : %0d",
                      n_pool, FRAMES * N_POOL, t_pool0, t_pool1, err_max, err_pdone);
            $fdisplay(fh, "");
            $fdisplay(fh, "conv1 vs ce1_out.mem   (%0d values) : %0d differ", FRAMES * N_MAC, conv_err);
            $fdisplay(fh, "pool1 vs pool1_out.mem (%0d values) : %0d differ", FRAMES * N_PV, pool_err);
        end
    endtask

    // ========================================================================
    // main
    // ========================================================================
    integer ok, rf;

    initial begin
        clk       = 0;
        rst_n     = 0;
        running   = 0;
        cyc       = 0;
        n_win     = 0;
        n_mac     = 0;
        n_sum     = 0;
        n_conv    = 0;
        n_pool    = 0;
        cur_win   = 0;
        q_wr      = 0;
        q_rd      = 0;
        err_win   = 0;
        err_wgt   = 0;
        err_mac   = 0;
        err_sum   = 0;
        err_quant = 0;
        err_cdone = 0;
        err_pdone = 0;
        err_max   = 0;
        conv_err  = 0;
        pool_err  = 0;
        t_pix0 = -1; t_pix1 = -1; t_win0 = -1; t_win1 = -1; t_mac0 = -1; t_mac1 = -1;
        t_sum0 = -1; t_sum1 = -1; t_conv0 = -1; t_conv1 = -1; t_pool0 = -1; t_pool1 = -1;
        for (i = 0; i < FRAMES * N_MAC; i = i + 1) rtl_conv[i] = 16'hxxxx;
        for (i = 0; i < FRAMES * N_PV; i = i + 1) rtl_pool[i] = 16'hxxxx;

        $readmemh({"ce1_stim.mem"}, stim);
        $readmemh({"ce1_out.mem"}, ce1_out);
        $readmemh({"pool1_out.mem"}, pool1_out);
        $readmemh({"conv1_weight_144.mem"}, wref);
        $readmemh({"conv1_bias_ce.mem"}, bias);
        if (^stim[FRAMES*N_PIX-1] === 1'bx || ^pool1_out[FRAMES*N_POOL-1] === 1'bx || ^wref[0] === 1'bx) begin
            $display("[FAIL] rtl_ref files not loaded - check REF_DIR (%0s)", REF_DIR);
            $finish;
        end

        // golden entries -> matrices (same entry order as the RTL stream)
        for (i = 0; i < FRAMES * N_CONV; i = i + 1)
        for (k = 0; k < 3; k = k + 1)
        gold_conv[(i/N_CONV)*N_MAC+((i%N_CONV)/N_WIN*3+k)*N_WIN+(i%N_CONV)%N_WIN] = ce1_out[i][16*k+:16];
        for (i = 0; i < FRAMES * N_POOL; i = i + 1)
        for (k = 0; k < 3; k = k + 1)
        gold_pool[(i/N_POOL)*N_PV+((i%N_POOL)/N_POOLPIX*3+k)*N_POOLPIX+(i%N_POOL)%N_POOLPIX] = pool1_out[i][16*k+:16];

        fd = $fopen(TRACE_FILE, "w");
        $fdisplay(fd, "cnn_top layer1 signal trace  (rtl_ref golden vectors, %0d frames, INT16 / INT32 integers)",
                  FRAMES);
        $fdisplay(fd, "f<n> = frame, ok = matches golden / TB calculation, ERR = mismatch\n");

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1;
        // bias ROM <- conv1_bias_ce.mem (conv_out_stage hard-codes an older "conv1_bias.mem")
        $readmemh({"conv1_bias_ce.mem"},
                  dut.U_CONV_L1.U_OUTPUT_STAGE_L1.GEN_CONV1.u_output_buffer.u_bias_rom.mem);

        $display("==============================================================");
        $display(" cnn_top layer1  |  rtl_ref golden vectors, %0d frame(s)", FRAMES);
        $display(" frame 0 = EMNIST sample '6', frame 1 = flipped L/R");
        $display(" console: flow of conv window (r%0d,c%0d) -> pool (r%0d,c%0d) per frame", FOCUS_R,
                 FOCUS_C, FOCUS_R / 2, FOCUS_C / 2);
        $display(" every event of every pixel -> %0s", TRACE_FILE);
        $display("==============================================================");

        @(negedge clk);
        running = 1;
        while (n_pool < FRAMES * N_POOL && cyc < MAX_CYCLES) @(posedge clk);
        repeat (50) @(posedge clk);
        running = 0;

        // ---------- results ----------
        write_matrix(0, 0);
        write_matrix(0, 1);
        write_matrix(1, 0);
        write_matrix(1, 1);

        rf = $fopen(REPORT_FILE, "w");
        summary(rf);
        $fdisplay(rf, "\nmismatches (first %0d each):", MAX_LIST);
        list_diffs(rf, 0);
        list_diffs(rf, 1);

        $display("");
        summary(1);  // STDOUT
        list_diffs(1, 0);
        list_diffs(1, 1);

        ok = (n_pool == FRAMES * N_POOL) && (n_conv == FRAMES * N_CONV) &&
             (err_win + err_wgt + err_mac + err_sum + err_quant + err_cdone + err_pdone + err_max +
              conv_err + pool_err == 0);
        if (ok) begin
            $display("\n[PASS] every stage checked, RTL == rtl_ref golden bit-exact (%0d cycles)", cyc);
            $fdisplay(rf, "\n[PASS] RTL == rtl_ref golden bit-exact (%0d cycles)", cyc);
        end else begin
            $display("\n[FAIL] see %0s / %0s%0s", REPORT_FILE, TRACE_FILE,
                     (cyc >= MAX_CYCLES) ? "  (TIMEOUT)" : "");
            $fdisplay(rf, "\n[FAIL]%0s", (cyc >= MAX_CYCLES) ? "  (TIMEOUT)" : "");
        end
        $display("  matrices: rtl_conv1_out.txt / gold_conv1_out.txt, rtl_pool1_out.txt / gold_pool1_out.txt");
        $fclose(rf);
        $fclose(fd);
        $finish;
    end

endmodule
