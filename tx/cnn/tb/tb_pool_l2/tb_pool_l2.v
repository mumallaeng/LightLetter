`timescale 1ns / 1ps

// Testbench for pool_l2 (2x2 max pooling, stride 2: 11x11x16 -> 5x5x16), checked against the C golden model.
//
// 실행: Vivado 프로젝트 sim_1 에 이 파일을 넣고 top 을 tb_pool_l2 로 두고 Run Behavioral Simulation.
//       아래 .mem 이 sim 소스로 들어가 있어야 한다 (bare 파일명으로 읽는다).
//   options (xelab): -generic_top "VALID_PCT=70" -generic_top "READY_PCT=40" -generic_top "SEED=5"
//
// 입력 / 기대값 (rtl/cnn/rtl_ref/, FRAMES = 2)
//   ce2_out.mem   : 17bit {ch_done, data} x 3872  conv_l2 출력 = pool_l2 입력, frame 당 och0 11x11 raster -> ... -> och15
//   pool2_out.mem : 17bit {ch_done, data} x  800  pool_l2 출력, frame 당 och0 5x5 raster -> ... -> och15, 채널마다 (4,4) 에 ch_done
//   11 은 홀수라 마지막 행 / 열 (r10, c10) 은 버린다.
//
// 확인하는 것
//   [A] 출력 vs pool2_out.mem   - {pool_ch_done, pool_data} 를 handshake 순서대로 골든 파일과 비교
//   [B] 출력 vs 입력 2x2 max    - 같은 출력을 TB 가 입력에서 직접 구한 max 와도 비교 (골든 파일과 독립)
//   [C] 출력 handshake         - pool_valid 가 뜬 뒤 pool_ready 전에 내려가거나 값이 바뀌면 위반
//
// 결과 파일 (시뮬레이션 실행 디렉터리)
//   REPORT_FILE : frame / 채널별 입력 11x11 grid (2x2 window 의 max 는 [ ], 버리는 r10 / c10 은 ~)
//                 -> RTL 출력 5x5 grid. signed 10진수. 값은 TB 가 handshake 때 잡은 RTL 출력 그대로이고
//                 콘솔 판정도 같은 배열에서 나온다. '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름
//   TRACE_FILE  : 모든 입력 / 출력 handshake 의 sim time 과 값 (10진수, 괄호 안 hex). time 은 파형의 posedge 시각.

module tb_pool_l2;

    // ---------------- parameters ----------------
    parameter STIM_FILE   = "ce2_out.mem";
    parameter GOLD_FILE   = "pool2_out.mem";
    parameter REPORT_FILE = "tb_pool_l2_report.txt";
    parameter TRACE_FILE  = "tb_pool_l2_trace.txt";

    parameter FRAMES     = 2;
    parameter VALID_PCT  = 100;  // out_valid 을 올릴 확률
    parameter READY_PCT  = 100;  // pool_ready 를 올릴 확률
    parameter SEED       = 1;
    parameter MAX_CYCLES = 200000;
    parameter MAX_REPORT = 10;   // 콘솔에 찍을 불일치 최대 개수

    localparam CH       = 16;
    localparam IN_W     = 11;
    localparam OUT_W    = IN_W / 2;             // 5
    localparam IN_CH    = IN_W * IN_W;          // 121
    localparam OUT_CH   = OUT_W * OUT_W;        // 25
    localparam IN_FRAME = IN_CH * CH;           // 1936
    localparam OUT_FRAME = OUT_CH * CH;         // 400
    localparam N_IN     = IN_FRAME * FRAMES;    // 3872
    localparam N_OUT    = OUT_FRAME * FRAMES;   // 800

    // ---------------- dut ports ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] out_data;
    reg         out_valid;
    wire        out_ready;
    reg         out_ch_done;
    wire [15:0] pool_data;
    wire        pool_valid;
    reg         pool_ready;
    wire        pool_ch_done;

    pool_l2 #(
        .IF_H(IN_W),
        .IF_W(IN_W)
    ) dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_data    (out_data),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .pool_data   (pool_data),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done)
    );

    always #5 clk = ~clk;

    // ---------------- data ----------------
    reg [16:0] stim[0:N_IN-1];
    reg [16:0] gold[0:N_OUT-1];
    reg [16:0] got [0:N_OUT-1];  // RTL 출력 - report 도 콘솔 판정도 이 배열을 쓴다

    integer in_idx;  // 보낸 입력 수
    integer o;       // 받은 출력 수
    integer cyc, running, seed, fd_trace, i;
    integer err_a, err_b, err_c, shown_a, shown_b, shown_c;
    integer stall_cyc;  // pool_valid & ~pool_ready 사이클 수
    reg [31:0] roll_v, roll_r;

    // ---------------- helpers ----------------
    // 출력 entry e 의 기대값을 입력에서 직접 계산: 11x11 입력의 2x2 window max (unsigned, ReLU 뒤라 음수 없음)
    function [15:0] ref_max;
        input integer e;
        integer f, ch, pr, pc, base;
        reg [15:0] a, b, c, d, m;
        begin
            f    = e / OUT_FRAME;
            ch   = (e % OUT_FRAME) / OUT_CH;
            pr   = (e % OUT_CH) / OUT_W;
            pc   = e % OUT_W;
            base = f * IN_FRAME + ch * IN_CH + (2 * pr) * IN_W + 2 * pc;
            a = stim[base][15:0];
            b = stim[base+1][15:0];
            c = stim[base+IN_W][15:0];
            d = stim[base+IN_W+1][15:0];
            m = a;
            if (b > m) m = b;
            if (c > m) m = c;
            if (d > m) m = d;
            ref_max = m;
        end
    endfunction

    // 입력 (f, ch, r, c) 가 어느 window 의 max 인지: 1 = max (같은 값이면 raster 순 첫 번째), 0 = 아님, -1 = 버리는 행/열
    function integer in_mark;
        input integer f, ch, r, c;
        integer base, k, best;
        reg [15:0] v, mv;
        begin
            if (r >= OUT_W * 2 || c >= OUT_W * 2) in_mark = -1;
            else begin
                base = f * IN_FRAME + ch * IN_CH + (r / 2 * 2) * IN_W + (c / 2 * 2);
                best = 0;
                mv   = stim[base][15:0];
                for (k = 1; k < 4; k = k + 1) begin
                    v = stim[base + (k / 2) * IN_W + (k % 2)][15:0];
                    if (v > mv) begin
                        mv   = v;
                        best = k;
                    end
                end
                in_mark = ((r % 2) * 2 + (c % 2) == best) ? 1 : 0;
            end
        end
    endfunction

    // ---------------- 입력 source ----------------
    // valid 는 한 번 올리면 받을 때까지 유지 (AXIS 규칙). 데이터는 in_idx 에서 조합으로 뽑는다.
    wire in_fire = out_valid && out_ready;

    always @(*) begin
        out_data    = (in_idx < N_IN) ? stim[in_idx][15:0] : 16'd0;
        out_ch_done = (in_idx < N_IN) ? stim[in_idx][16] : 1'b0;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            in_idx     <= 0;
            out_valid  <= 1'b0;
            pool_ready <= 1'b0;
        end else if (running) begin
            roll_v = $unsigned($random(seed)) % 100;
            roll_r = $unsigned($random(seed)) % 100;
            if (in_fire) in_idx <= in_idx + 1;
            if (!out_valid || out_ready)
                out_valid <= ((in_idx + in_fire) < N_IN) && (roll_v < VALID_PCT);
            pool_ready <= (roll_r < READY_PCT);
        end
    end

    // ---------------- 출력 capture / 비교 + trace ----------------
    wire [16:0] out_word = {pool_ch_done, pool_data};
    reg  [16:0] hold_word;
    reg         hold_v;
    reg  [15:0] exp_b;

    always @(posedge clk) begin
        if (rst_n && in_fire)
            $fdisplay(fd_trace, "%10t  %7d   IN  %4d  f%0d ch%2d r%2d c%2d   %6d (%04x) done=%b%0s",
                      $realtime, cyc, in_idx, in_idx / IN_FRAME, (in_idx % IN_FRAME) / IN_CH,
                      (in_idx % IN_CH) / IN_W, in_idx % IN_W, $signed(out_data), out_data, out_ch_done,
                      ((in_idx % IN_CH) / IN_W == IN_W - 1 || in_idx % IN_W == IN_W - 1) ? "  (dropped)" : "");

        // [C] handshake: 이전 사이클에 valid & ~ready 였으면 이번에도 같은 값으로 valid 여야 한다
        if (rst_n && hold_v && !(pool_valid && out_word === hold_word)) begin
            err_c = err_c + 1;
            if (shown_c < MAX_REPORT) begin
                shown_c = shown_c + 1;
                $display("[FAIL][C] %t cyc %0d: pool_valid=%b value %0d done=%b, but %0d done=%b was not taken yet",
                         $realtime, cyc, pool_valid, pool_data, pool_ch_done, hold_word[15:0], hold_word[16]);
            end
        end
        hold_v    = rst_n && pool_valid && !pool_ready;
        hold_word = out_word;
        if (rst_n && pool_valid && !pool_ready) stall_cyc = stall_cyc + 1;

        if (rst_n && pool_valid && pool_ready) begin
            if (o < N_OUT) begin
                got[o] = out_word;
                exp_b  = ref_max(o);
                $fdisplay(fd_trace, "%10t  %7d  OUT  %4d  f%0d ch%2d r%2d c%2d   %6d (%04x) done=%b  exp %6d (%04x) done=%b  %0s",
                          $realtime, cyc, o, o / OUT_FRAME, (o % OUT_FRAME) / OUT_CH,
                          (o % OUT_CH) / OUT_W, o % OUT_W, $signed(pool_data), pool_data, pool_ch_done,
                          $signed(gold[o][15:0]), gold[o][15:0], gold[o][16],
                          (out_word === gold[o]) ? "OK" : "MISMATCH");
                if (out_word !== gold[o]) begin
                    err_a = err_a + 1;
                    if (shown_a < MAX_REPORT) begin
                        shown_a = shown_a + 1;
                        $display("[FAIL][A] entry %0d (frame %0d ch %0d r%0d c%0d) @ %t: got %0d done=%b / exp %0d done=%b",
                                 o, o / OUT_FRAME, (o % OUT_FRAME) / OUT_CH, (o % OUT_CH) / OUT_W, o % OUT_W,
                                 $realtime, pool_data, pool_ch_done, gold[o][15:0], gold[o][16]);
                    end
                end
                if (pool_data !== exp_b) begin
                    err_b = err_b + 1;
                    if (shown_b < MAX_REPORT) begin
                        shown_b = shown_b + 1;
                        $display("[FAIL][B] entry %0d (frame %0d ch %0d r%0d c%0d): got %0d / max of the 2x2 input window %0d",
                                 o, o / OUT_FRAME, (o % OUT_FRAME) / OUT_CH, (o % OUT_CH) / OUT_W, o % OUT_W,
                                 pool_data, exp_b);
                    end
                end
            end else begin
                err_a = err_a + 1;
                $fdisplay(fd_trace, "%10t  %7d  OUT  %4d  EXTRA  %6d done=%b", $realtime, cyc, o,
                          $signed(pool_data), pool_ch_done);
            end
            o = o + 1;
        end
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // ---------------- report ----------------
    task write_report;
        integer fd, f, ch, r, c, k, e, bad, listed, mk;
        reg [15:0] v;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "pool_l2 test report  (2x2 max pooling, stride 2: 11x11x16 -> 5x5x16, values in signed decimal)");
            $fdisplay(fd, "  input  : %s  (%0d entries)", STIM_FILE, N_IN);
            $fdisplay(fd, "  golden : %s  (%0d entries, {ch_done, data})", GOLD_FILE, N_OUT);
            $fdisplay(fd, "  VALID_PCT=%0d READY_PCT=%0d SEED=%0d", VALID_PCT, READY_PCT, SEED);
            $fdisplay(fd, "  input %0d / %0d, output %0d / %0d, %0d cycles, output stall %0d cycles",
                      in_idx, N_IN, o, N_OUT, cyc, stall_cyc);
            $fdisplay(fd, "  [A] vs %s    : %0d wrong", GOLD_FILE, err_a);
            $fdisplay(fd, "  [B] vs 2x2 input max : %0d wrong", err_b);
            $fdisplay(fd, "  [C] handshake        : %0d violations", err_c);
            $fdisplay(fd, "  RESULT : %0s", pass_all(0) ? "PASS" : "FAIL");
            $fdisplay(fd, "");
            $fdisplay(fd, "  INPUT  : [ v] = 2x2 window 의 max, ~v = 버리는 행 / 열 (r10, c10), '|' '-' = window 경계");
            $fdisplay(fd, "  OUTPUT : TB 가 pool_valid & pool_ready 에서 잡은 RTL 출력");
            $fdisplay(fd, "           '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름, ---- = 출력 안 나옴");
            $fdisplay(fd, "  각 값의 sim time / hex 는 %s 참고", TRACE_FILE);

            for (f = 0; f < FRAMES; f = f + 1) begin
                $fdisplay(fd, "\n##################################################################");
                $fdisplay(fd, " FRAME %0d", f);
                $fdisplay(fd, "##################################################################");
                for (ch = 0; ch < CH; ch = ch + 1) begin
                    // ---- 입력 ----
                    $fdisplay(fd, "\n[INPUT frame %0d / ch %0d, 11x11]", f, ch);
                    $fwrite(fd, "       ");
                    for (c = 0; c < IN_W; c = c + 1) $fwrite(fd, "%0s    c%2d", (c % 2 == 0 && c > 0) ? "  " : "", c);
                    $fwrite(fd, "\n");
                    for (r = 0; r < IN_W; r = r + 1) begin
                        if (r % 2 == 0 && r > 0) begin
                            $fwrite(fd, "      +");
                            for (c = 0; c < IN_W; c = c + 1) $fwrite(fd, "%0s-------", (c % 2 == 0 && c > 0) ? "-+" : "");
                            $fwrite(fd, "\n");
                        end
                        $fwrite(fd, " r%2d | ", r);
                        for (c = 0; c < IN_W; c = c + 1) begin
                            v  = stim[f*IN_FRAME+ch*IN_CH+r*IN_W+c][15:0];
                            mk = in_mark(f, ch, r, c);
                            if (c % 2 == 0 && c > 0) $fwrite(fd, " |");
                            if (mk == 1) $fwrite(fd, "[%6d]", $signed(v));
                            else if (mk == -1) $fwrite(fd, " ~%5d ", $signed(v));
                            else $fwrite(fd, " %6d ", $signed(v));
                        end
                        $fwrite(fd, "\n");
                    end

                    // ---- 출력 ----
                    bad = 0;
                    for (k = 0; k < OUT_CH; k = k + 1) begin
                        e = f * OUT_FRAME + ch * OUT_CH + k;
                        if (e >= o || got[e] !== gold[e]) bad = bad + 1;
                    end
                    $fdisplay(fd, "\n[OUTPUT frame %0d / ch %0d, 5x5]  %0s", f, ch, (bad == 0) ? "OK" : "MISMATCH");
                    $fwrite(fd, "       ");
                    for (c = 0; c < OUT_W; c = c + 1) $fwrite(fd, "    c%2d", c);
                    $fwrite(fd, "\n");
                    for (r = 0; r < OUT_W; r = r + 1) begin
                        $fwrite(fd, " r%2d | ", r);
                        for (c = 0; c < OUT_W; c = c + 1) begin
                            e = f * OUT_FRAME + ch * OUT_CH + r * OUT_W + c;
                            if (e >= o) $fwrite(fd, "   ----");
                            else if (got[e][15:0] !== gold[e][15:0]) $fwrite(fd, "*%6d", $signed(got[e][15:0]));
                            else if (got[e][16] !== gold[e][16]) $fwrite(fd, "!%6d", $signed(got[e][15:0]));
                            else $fwrite(fd, " %6d", $signed(got[e][15:0]));
                        end
                        $fwrite(fd, "\n");
                    end
                    if (bad != 0) begin
                        listed = 0;
                        $fdisplay(fd, "   mismatch %0d 개:", bad);
                        for (k = 0; k < OUT_CH; k = k + 1) begin
                            e = f * OUT_FRAME + ch * OUT_CH + k;
                            if (e >= o)
                                $fdisplay(fd, "     r%0d c%0d : rtl   ----          exp %6d done=%b", k / OUT_W, k % OUT_W,
                                          $signed(gold[e][15:0]), gold[e][16]);
                            else if (got[e] !== gold[e])
                                $fdisplay(fd, "     r%0d c%0d : rtl %6d done=%b  exp %6d done=%b", k / OUT_W, k % OUT_W,
                                          $signed(got[e][15:0]), got[e][16], $signed(gold[e][15:0]), gold[e][16]);
                        end
                    end
                end
            end
            $fclose(fd);
        end
    endtask

    function pass_all;
        input dummy;
        pass_all = (err_a == 0 && err_b == 0 && err_c == 0 && o == N_OUT);
    endfunction

    // ---------------- main ----------------
    initial begin
        $timeformat(-9, 0, " ns", 10);
        clk       = 1'b0;
        rst_n     = 1'b0;
        running   = 0;
        seed      = SEED;
        o         = 0;
        cyc       = 0;
        err_a = 0; err_b = 0; err_c = 0;
        shown_a = 0; shown_b = 0; shown_c = 0;
        stall_cyc = 0;
        hold_v    = 1'b0;
        hold_word = 17'd0;
        for (i = 0; i < N_OUT; i = i + 1) got[i] = 17'bx;

        $readmemh(STIM_FILE, stim);
        $readmemh(GOLD_FILE, gold);
        if (^stim[0] === 1'bx || ^gold[0] === 1'bx) begin
            $display("[FAIL] golden file not loaded - add %s / %s to the sim sources", STIM_FILE, GOLD_FILE);
            $finish;
        end

        fd_trace = $fopen(TRACE_FILE, "w");
        $fdisplay(fd_trace, "pool_l2 handshake trace  (time = posedge 시각, 파형과 동일, 값은 signed 10진수)");
        $fdisplay(fd_trace, "      time    cycle  dir   idx  position           value");

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;
        $display("pool_l2 TB: %0d input entries -> %0d output entries (%0d frame), VALID_PCT=%0d READY_PCT=%0d",
                 N_IN, N_OUT, FRAMES, VALID_PCT, READY_PCT);

        @(negedge clk);
        running = 1;

        while (o < N_OUT && cyc < MAX_CYCLES) @(posedge clk);
        repeat (50) @(posedge clk);  // 초과 출력이 나오는지 조금 더 본다
        running = 0;

        // ---------------- summary ----------------
        $display("\nsummary (%0d cycles, output stall %0d cycles):", cyc, stall_cyc);
        if (o < N_OUT)
            $display("  output : got %0d / %0d entries%0s", o, N_OUT, (cyc >= MAX_CYCLES) ? " (TIMEOUT)" : "");
        else if (o > N_OUT)
            $display("  output : got %0d entries, %0d more than expected", o, o - N_OUT);
        $display("  [A] vs %s    : %0s", GOLD_FILE, (err_a == 0) ? "all match" : "MISMATCH");
        if (err_a != 0) $display("        %0d entries wrong", err_a);
        $display("  [B] vs 2x2 input max : %0s", (err_b == 0) ? "all match" : "MISMATCH");
        if (err_b != 0) $display("        %0d entries wrong", err_b);
        $display("  [C] handshake        : %0d violations", err_c);

        write_report;
        $fclose(fd_trace);
        $display("\nwrote %s, %s", REPORT_FILE, TRACE_FILE);

        if (pass_all(0)) $display("\n[PASS] pool_l2: %0d cycles.", cyc);
        else $display("\n[FAIL] pool_l2 - see %s", REPORT_FILE);
        $finish;
    end

endmodule
