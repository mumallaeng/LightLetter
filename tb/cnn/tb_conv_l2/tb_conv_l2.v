`timescale 1ns / 1ps

// Full-path testbench for conv_l2 (13x13x6 -> 11x11x16), checked against the C golden model.
//
// 실행 (Windows, tb/cnn/tb_conv_l2 에서):
//   run_sim.bat          batch 실행 -> work/ 에 report / trace txt + 파형(wdb)
//   run_sim.bat gui      XSim GUI 로 실행 (wave.tcl 의 신호 그룹이 뜬 상태로 run all)
//   run_sim.bat wave     batch 실행 후 저장된 파형(work/tb_conv_l2_sim.wdb)을 GUI 로 연다
//   options: set XELAB_OPTS=-generic_top "VALID_PCT=70" -generic_top "READY_PCT=60"
//
// 입력 / 기대값 (rtl_ref/, run_sim.bat 이 work/ 로 복사)
//   pool1_out.mem  : 49bit {ch_done, d2, d1, d0} x 676  (frame 당 pass0 = in_ch0~2 169개 -> pass1 = in_ch3~5 169개)
//   ce2_out.mem    : 17bit {ch_done, data}       x 3872 (frame 당 och0 11x11 raster -> ... -> och15)
//   conv2_weight.mem  : 432bit x 32 ([och][is_ch35]) - weight ROM 기대값
//   conv2_bias_ce.mem : 32bit x 16
//
// 우회 하나 (RTL 이 고쳐지면 지우면 된다)
//   (W-bias) conv_out_stage 가 bias_rom 에 "conv2_bias.mem" 을 박아 넣는데,
//            지금 weight 와 짝이 맞는 bias 는 rtl_ref/conv2_bias_ce.mem 이다. -> TB 가 bias ROM 을 덮어쓴다.
//
// 확인하는 것
//   [A] weight ROM 타이밍 - cal_valid 인 매 사이클 weight_out == conv2_weight[out_ch_sel*2 + is_ch35]
//   [B] 최종 출력 - {out_ch_done, out_data} 를 ce2_out.mem 과 transfer 단위로 비교
//   [C] reorder overrun - out_reorder 가 꽉 찬 상태에서 push 가 들어와 버려진 횟수
//
// 입력 방식: 버튼을 누를 때마다 이미지 한 장 (실제 사용과 같음)
//   다음 이미지는 앞 이미지의 결과가 다 나온 뒤에 넣는다. 이미지 사이에 리셋은 하지 않는다.
//
// 결과 파일 (xsim 실행 디렉터리 = work/)
//   REPORT_FILE : frame / 출력 채널별 11x11 grid. 값은 TB 가 handshake 때 잡은 RTL 출력 그대로이고
//                 콘솔 [B] 판정도 같은 배열에서 나온다. '*' = 골든과 불일치 (아래에 골든 값 목록)
//   TRACE_FILE  : 모든 입력 / 출력 handshake 의 sim time 과 값. time 은 파형의 posedge 시각과 같다.

module tb_conv_l2;

    // ---------------- parameters ----------------
    parameter STIM_FILE   = "pool1_out.mem";
    parameter GOLD_FILE   = "ce2_out.mem";
    parameter WEIGHT_FILE = "conv2_weight.mem";
    parameter BIAS_FILE   = "conv2_bias_ce.mem";
    parameter REPORT_FILE = "tb_conv_l2_report.txt";
    parameter TRACE_FILE  = "tb_conv_l2_trace.txt";

    parameter OCH        = 16;
    parameter ICH        = 6;
    parameter IN_W       = 13;
    parameter OUT_W      = 11;
    parameter FRAMES     = 2;
    parameter VALID_PCT  = 100;  // pool_valid 을 올릴 확률
    parameter READY_PCT  = 100;  // out_ready 를 올릴 확률
    parameter SEED       = 1;
    parameter MAX_CYCLES = 200000;
    parameter MAX_REPORT = 10;   // 콘솔에 찍을 불일치 최대 개수

    localparam IN_PASS   = IN_W * IN_W;           // 169
    localparam IN_FRAME  = IN_PASS * 2;           // 338
    localparam N_IN      = IN_FRAME * FRAMES;     // 676
    localparam OUT_CH    = OUT_W * OUT_W;         // 121
    localparam OUT_FRAME = OUT_CH * OCH;          // 1936
    localparam N_OUT     = OUT_FRAME * FRAMES;    // 3872

    // ---------------- dut ports ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] pool_data0, pool_data1, pool_data2;
    reg         pool_valid;
    wire        pool_ready;
    reg         pool_ch_done;
    wire        out_valid;
    reg         out_ready;
    wire        out_ch_done;
    wire [15:0] out_data;

    conv_l2 #(
        .OCH(OCH)
    ) dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .pool_data0  (pool_data0),
        .pool_data1  (pool_data1),
        .pool_data2  (pool_data2),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .out_data    (out_data)
    );

    always #5 clk = ~clk;

    // ---------------- data ----------------
    reg [ 48:0] stim[0:N_IN-1];
    reg [ 16:0] gold[0:N_OUT-1];
    reg [431:0] wref[0:OCH*2-1];
    reg [ 16:0] got [0:N_OUT-1];  // RTL 출력 - report 도 콘솔 판정도 이 배열을 쓴다

    integer in_idx;  // 보낸 입력 entry 수
    integer o;       // 받은 출력 entry 수
    integer cyc;
    integer running;
    integer seed;
    integer out_errs, out_shown;
    integer rom_checks, rom_errs, rom_shown;
    integer overruns;
    integer img_done;  // 결과가 다 나온 이미지 수
    integer fd_trace;
    integer i;

    // ---------------- 입력 source ----------------
    // valid 는 한 번 올리면 받을 때까지 유지 (AXIS 규칙). 데이터는 in_idx 에서 조합으로 뽑는다.
    reg [31:0] roll_v, roll_r;
    wire in_fire = pool_valid && pool_ready;

    always @(*) begin
        pool_data0   = (in_idx < N_IN) ? stim[in_idx][15:0]  : 16'd0;
        pool_data1   = (in_idx < N_IN) ? stim[in_idx][31:16] : 16'd0;
        pool_data2   = (in_idx < N_IN) ? stim[in_idx][47:32] : 16'd0;
        pool_ch_done = (in_idx < N_IN) ? stim[in_idx][48]    : 1'b0;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            in_idx     <= 0;
            pool_valid <= 1'b0;
            out_ready  <= 1'b0;
        end else if (running) begin
            roll_v = $unsigned($random(seed)) % 100;
            roll_r = $unsigned($random(seed)) % 100;
            if (in_fire) in_idx <= in_idx + 1;
            if (!pool_valid || pool_ready)
                pool_valid <= ((in_idx + in_fire) < N_IN) &&
                              ((in_idx + in_fire) / IN_FRAME <= img_done) && (roll_v < VALID_PCT);
            out_ready <= (roll_r < READY_PCT);
        end
    end

    // ---------------- [A] weight ROM 타이밍 ----------------
    wire [431:0] rom_expect = wref[dut.out_ch_sel*2 + dut.rom_is_ch35];

    always @(posedge clk) begin
        if (rst_n && dut.cal_valid) begin
            rom_checks = rom_checks + 1;
            if (dut.weight_out !== rom_expect) begin
                rom_errs = rom_errs + 1;
                if (rom_shown < MAX_REPORT) begin
                    rom_shown = rom_shown + 1;
                    $display("[FAIL][rom] %t cyc %0d: out_ch_sel=%0d is_ch35=%b, weight_out.tap0=%04x exp %04x",
                             $realtime, cyc, dut.out_ch_sel, dut.rom_is_ch35,
                             dut.weight_out[15:0], rom_expect[15:0]);
                end
            end
        end
    end

    // ---------------- [B] 출력 capture / 비교 + trace ----------------
    wire [16:0] out_word = {out_ch_done, out_data};

    always @(posedge clk) begin
        if (rst_n && in_fire)
            $fdisplay(fd_trace, "%10t  %7d   IN  %4d  f%0d pass%0d r%2d c%2d   %6d %6d %6d  done=%b",
                      $realtime, cyc, in_idx, in_idx / IN_FRAME, (in_idx % IN_FRAME) / IN_PASS,
                      (in_idx % IN_PASS) / IN_W, in_idx % IN_W,
                      $signed(pool_data0), $signed(pool_data1), $signed(pool_data2), pool_ch_done);

        if (rst_n && out_valid && out_ready) begin
            if (o < N_OUT) begin
                got[o] = out_word;
                $fdisplay(fd_trace, "%10t  %7d  OUT  %4d  f%0d och%2d r%2d c%2d   %6d (%04x) done=%b  exp %6d (%04x) done=%b  %0s",
                          $realtime, cyc, o, o / OUT_FRAME, (o % OUT_FRAME) / OUT_CH,
                          (o % OUT_CH) / OUT_W, o % OUT_W,
                          $signed(out_data), out_data, out_ch_done,
                          $signed(gold[o][15:0]), gold[o][15:0], gold[o][16],
                          (out_word === gold[o]) ? "OK" : "MISMATCH");
                if (out_word !== gold[o]) begin
                    out_errs = out_errs + 1;
                    if (out_shown < MAX_REPORT) begin
                        out_shown = out_shown + 1;
                        $display("[FAIL][out] entry %0d (frame %0d och %0d r%0d c%0d) @ %t: got %0d done=%b / exp %0d done=%b",
                                 o, o / OUT_FRAME, (o % OUT_FRAME) / OUT_CH,
                                 (o % OUT_CH) / OUT_W, o % OUT_W, $realtime,
                                 $signed(out_data), out_ch_done, $signed(gold[o][15:0]), gold[o][16]);
                    end
                end
            end else begin
                $fdisplay(fd_trace, "%10t  %7d  OUT  %4d  EXTRA  %6d (%04x) done=%b", $realtime, cyc, o,
                          $signed(out_data), out_data, out_ch_done);
            end
            o = o + 1;
        end
        img_done <= o / OUT_FRAME;
    end

    // ---------------- [C] reorder overrun ----------------
    wire rb_push = dut.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.push;
    wire rb_full = dut.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.frame_full;

    always @(posedge clk) begin
        if (rst_n && rb_push && rb_full) begin
            if (overruns < MAX_REPORT)
                $display("[FAIL][overrun] %t cyc %0d: out_reorder full, push dropped", $realtime, cyc);
            overruns = overruns + 1;
        end
    end

    // ---------------- cycle counter ----------------
    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // ---------------- report ----------------
    task write_report;
        integer fd, f, ch, r, c, k, n_bad, ch_bad;
        reg [15:0] px;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "conv_l2 test report  (13x13x6 -> 11x11x16, values in signed decimal)");
            $fdisplay(fd, "  input  : %s  (%0d entries, 3 in_ch per entry)", STIM_FILE, N_IN);
            $fdisplay(fd, "  golden : %s  (%0d entries, {ch_done, data})", GOLD_FILE, N_OUT);
            $fdisplay(fd, "  VALID_PCT=%0d READY_PCT=%0d SEED=%0d", VALID_PCT, READY_PCT, SEED);
            $fdisplay(fd, "  input %0d / %0d, output %0d / %0d, %0d cycles", in_idx, N_IN, o, N_OUT, cyc);
            $fdisplay(fd, "  [A] weight ROM : %0d / %0d cal_valid cycles wrong", rom_errs, rom_checks);
            $fdisplay(fd, "  [B] output     : %0d / %0d entries wrong", out_errs, N_OUT);
            $fdisplay(fd, "  [C] overrun    : %0d dropped push", overruns);
            $fdisplay(fd, "  RESULT : %0s",
                      (rom_errs == 0 && out_errs == 0 && overruns == 0 && o == N_OUT) ? "PASS" : "FAIL");
            $fdisplay(fd, "");
            $fdisplay(fd, "  값 = RTL 출력 out_data (TB 가 out_valid & out_ready 에서 잡은 값)");
            $fdisplay(fd, "  '*' = 골든과 불일치 (값 또는 ch_done), '!' = ch_done 위치 오류, ---- = 출력 안 나옴");
            $fdisplay(fd, "  값은 INT16 을 signed 10진수로 찍었다. hex 는 %s 에 같이 있다", TRACE_FILE);
            $fdisplay(fd, "  각 출력의 sim time 은 %s 참고", TRACE_FILE);

            for (f = 0; f < FRAMES; f = f + 1) begin
                // ---- 입력 ----
                $fdisplay(fd, "\n##################################################################");
                $fdisplay(fd, " FRAME %0d", f);
                $fdisplay(fd, "##################################################################");
                for (ch = 0; ch < ICH; ch = ch + 1) begin
                    $fdisplay(fd, "\n[INPUT in_ch %0d, 13x13]", ch);
                    $fwrite(fd, "       ");
                    for (c = 0; c < IN_W; c = c + 1) $fwrite(fd, "    c%2d", c);
                    $fwrite(fd, "\n");
                    for (r = 0; r < IN_W; r = r + 1) begin
                        $fwrite(fd, " r%2d | ", r);
                        for (c = 0; c < IN_W; c = c + 1) begin
                            k = f * IN_FRAME + (ch / 3) * IN_PASS + r * IN_W + c;
                            px = stim[k] >> (16 * (ch % 3));
                            $fwrite(fd, " %6d", $signed(px));
                        end
                        $fwrite(fd, "\n");
                    end
                end

                // ---- 출력 ----
                for (ch = 0; ch < OCH; ch = ch + 1) begin
                    ch_bad = 0;
                    for (k = 0; k < OUT_CH; k = k + 1)
                        if (f * OUT_FRAME + ch * OUT_CH + k >= o ||
                            got[f*OUT_FRAME+ch*OUT_CH+k] !== gold[f*OUT_FRAME+ch*OUT_CH+k])
                            ch_bad = ch_bad + 1;
                    $fdisplay(fd, "\n[OUTPUT frame %0d / out_ch %0d, 11x11]  %0s", f, ch,
                              (ch_bad == 0) ? "OK" : "MISMATCH");
                    $fwrite(fd, "       ");
                    for (c = 0; c < OUT_W; c = c + 1) $fwrite(fd, "     c%2d", c);
                    $fwrite(fd, "\n");
                    for (r = 0; r < OUT_W; r = r + 1) begin
                        $fwrite(fd, " r%2d | ", r);
                        for (c = 0; c < OUT_W; c = c + 1) begin
                            k = f * OUT_FRAME + ch * OUT_CH + r * OUT_W + c;
                            if (k >= o) $fwrite(fd, "    ----");
                            else if (got[k][15:0] !== gold[k][15:0]) $fwrite(fd, " *%6d", $signed(got[k][15:0]));
                            else if (got[k][16] !== gold[k][16]) $fwrite(fd, " !%6d", $signed(got[k][15:0]));
                            else $fwrite(fd, "  %6d", $signed(got[k][15:0]));
                        end
                        $fwrite(fd, "\n");
                    end
                    if (ch_bad != 0) begin
                        n_bad = 0;
                        $fdisplay(fd, "   mismatch (최대 20개):");
                        for (k = 0; k < OUT_CH; k = k + 1) begin
                            i = f * OUT_FRAME + ch * OUT_CH + k;
                            if (n_bad < 20 && (i >= o || got[i] !== gold[i])) begin
                                n_bad = n_bad + 1;
                                if (i >= o)
                                    $fdisplay(fd, "     r%2d c%2d : rtl   ----          exp %6d done=%b",
                                              k / OUT_W, k % OUT_W, $signed(gold[i][15:0]), gold[i][16]);
                                else
                                    $fdisplay(fd, "     r%2d c%2d : rtl %6d done=%b  exp %6d done=%b",
                                              k / OUT_W, k % OUT_W, $signed(got[i][15:0]), got[i][16],
                                              $signed(gold[i][15:0]), gold[i][16]);
                            end
                        end
                    end
                end
            end
            $fclose(fd);
        end
    endtask

    // ---------------- main ----------------
    initial begin
        $timeformat(-9, 0, " ns", 10);
        if ($test$plusargs("vcd")) begin
            $dumpfile("tb_conv_l2.vcd");
            $dumpvars(0, tb_conv_l2);
        end

        clk        = 1'b0;
        rst_n      = 1'b0;
        running    = 0;
        seed       = SEED;
        o          = 0;
        cyc        = 0;
        out_errs   = 0;
        out_shown  = 0;
        rom_checks = 0;
        rom_errs   = 0;
        rom_shown  = 0;
        overruns   = 0;
        img_done   = 0;
        for (i = 0; i < N_OUT; i = i + 1) got[i] = 17'bx;

        $readmemh(STIM_FILE, stim);
        $readmemh(GOLD_FILE, gold);
        $readmemh(WEIGHT_FILE, wref);
        if (^stim[0] === 1'bx || ^gold[0] === 1'bx || ^wref[0] === 1'bx) begin
            $display("[FAIL] golden file not loaded - check that %s / %s / %s are in the run directory",
                     STIM_FILE, GOLD_FILE, WEIGHT_FILE);
            $finish;
        end

        fd_trace = $fopen(TRACE_FILE, "w");
        $fdisplay(fd_trace, "conv_l2 handshake trace  (time = posedge 시각, 파형과 동일)");
        $fdisplay(fd_trace, "      time    cycle  dir   idx  position          value");

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;

        // (W-bias) RTL 이 conv2_bias.mem 을 박아 넣어서 TB 가 덮어쓴다
        $readmemh(BIAS_FILE, dut.U_OUTPUT_STAGE_L1.GEN_CONV2.u_output_buffer.u_bias_rom.mem);
        $display("conv_l2 TB: %0d input entries -> %0d output entries (%0d frame), VALID_PCT=%0d READY_PCT=%0d",
                 N_IN, N_OUT, FRAMES, VALID_PCT, READY_PCT);

        @(negedge clk);
        running = 1;

        while (o < N_OUT && cyc < MAX_CYCLES) @(posedge clk);
        repeat (50) @(posedge clk);  // 초과 출력이 나오는지 조금 더 본다
        running = 0;

        // ---------------- summary ----------------
        $display("\n[A] weight ROM timing:");
        if (rom_errs == 0)
            $display("    -> weight_out == conv2_weight[out_ch_sel*2 + is_ch35] on all %0d cal_valid cycles", rom_checks);
        else
            $display("    -> %0d of %0d cal_valid cycles wrong", rom_errs, rom_checks);

        $display("\n[B] output vs C golden model (%s):", GOLD_FILE);
        if (o < N_OUT)
            $display("    -> got %0d entries, expected %0d%0s", o, N_OUT,
                     (cyc >= MAX_CYCLES) ? "  (TIMEOUT)" : "");
        else if (o > N_OUT)
            $display("    -> got %0d entries, %0d more than expected", o, o - N_OUT);
        if (out_errs == 0 && o >= N_OUT) $display("    -> all %0d entries match", N_OUT);
        else if (out_errs != 0) $display("    -> %0d entries differ", out_errs);

        $display("\n[C] out_reorder overrun:");
        if (overruns == 0) $display("    -> none");
        else $display("    -> %0d push dropped while the reorder buffer was full", overruns);

        write_report;
        $fclose(fd_trace);
        $display("\nwrote %s, %s", REPORT_FILE, TRACE_FILE);

        if (rom_errs == 0 && out_errs == 0 && overruns == 0 && o == N_OUT)
            $display("\n[PASS] conv_l2: %0d cycles.", cyc);
        else
            $display("\n[FAIL] conv_l2: rom %0d, output mismatch %0d, overrun %0d, entries %0d/%0d, %0d cycles.",
                     rom_errs, out_errs, overruns, o, N_OUT, cyc);
        $finish;
    end

endmodule
