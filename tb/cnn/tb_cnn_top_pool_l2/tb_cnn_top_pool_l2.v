`timescale 1ns / 1ps

// cnn_top (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2) checked against the C golden model (rtl/cnn/rtl_ref/).
//
//   28x28x1 -> conv_l1 -> 26x26x6 -> pool_l1 -> 13x13x6 -> conv_l2 -> 11x11x16 -> pool_l2 -> 5x5x16
//
// 실행: Vivado 프로젝트 sim_1 에 이 파일을 넣고 top 을 tb_cnn_top_pool_l2 로 두고 Run Behavioral Simulation.
//       아래 .mem 들이 sim 소스로 들어가 있어야 한다 (bare 파일명으로 읽는다).
//
// 입력 / 기대값 (rtl_ref/, FRAMES = 2 : frame 0 = 실제 이미지, frame 1 = 좌우 반전)
//   ce1_stim.mem      16bit x 1568  conv_l1 pixel 입력, frame 당 28x28 raster
//   ce1_out.mem       49bit x 2704  conv_l1 출력 {ch_done, d2, d1, d0}, frame 당 pass0(och0~2) -> pass1(och3~5)
//   pool1_out.mem     49bit x  676  pool_l1 출력 {ch_done, d2, d1, d0}, 같은 순서
//   ce2_out.mem       17bit x 3872  conv_l2 출력 {ch_done, data}, frame 당 och0 11x11 -> ... -> och15
//   pool2_out.mem     17bit x  800  pool_l2 출력 {ch_done, data}, frame 당 och0 5x5 -> ... -> och15, 채널마다 (4,4) 에 ch_done
//   conv2_weight.mem 432bit x   32  conv_l2 weight ROM 기대값 ([och][is_ch35])
//   conv1_bias_ce.mem / conv2_bias_ce.mem  INT32 bias
//
// 확인하는 것 (단계 경계마다 handshake 순서대로 골든 스트림과 비교)
//   [C1]  conv_l1 출력 (l1_out_valid & l1_out_ready)     vs ce1_out.mem
//   [P1]  pool_l1 출력 (l1_pool_valid & l1_pool_ready)   vs pool1_out.mem
//   [ROM] conv_l2 weight ROM - cal_valid 마다 weight_out == conv2_weight[out_ch_sel*2 + is_ch35]
//   [C2]  conv_l2 출력 (l2_out_valid & l2_out_ready)     vs ce2_out.mem
//   [P2]  pool_l2 출력 = cnn_top 출력 (l2_pool_valid & l2_pool_ready) vs pool2_out.mem
//   [HS]  cnn_top 출력 handshake - l2_pool_valid 가 뜬 뒤 l2_pool_ready 전에 내려가거나 값이 바뀌면 위반
//   [OVR] conv_l1 / conv_l2 out_reorder 가 꽉 찬 상태에서 버려진 push 횟수
//
// 입력 방식: 버튼을 누를 때마다 이미지 한 장 (실제 사용과 같음)
//   다음 이미지는 앞 이미지의 결과가 다 나온 뒤에 넣는다. 이미지 사이에 리셋은 하지 않는다.
//
// 결과 파일 (시뮬레이션 실행 디렉터리)
//   REPORT_FILE : 요약 + frame 별 단계 격자 (입력 28x28, C1 26x26x6, P1 13x13x6, C2 11x11x16, P2 5x5x16),
//                 signed 10진수.
//                 값은 TB 가 handshake 때 잡은 RTL 출력 그대로이고 콘솔 판정도 같은 배열에서 나온다.
//                 '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름, ---- = 출력 안 나옴
//   TRACE_FILE  : 모든 단계 handshake 의 sim time 과 값 (10진수, 괄호 안 hex). time 은 파형의 posedge 시각.
//
// TB 우회 (RTL 은 건드리지 않음)
//   (W-bias)  conv_out_stage 가 "conv1_bias.mem" / "conv2_bias.mem" 을 박아 넣는데 지금 weight 와
//             짝이 맞는 bias 는 rtl_ref/conv{1,2}_bias_ce.mem 이다. TB 가 두 bias ROM 을 덮어쓴다.

module tb_cnn_top_pool_l2;

    // ---------------- parameters ----------------
    parameter REPORT_FILE = "tb_cnn_top_pool_l2_report.txt";
    parameter TRACE_FILE  = "tb_cnn_top_pool_l2_trace.txt";

    parameter FRAMES     = 2;
    parameter VALID_PCT  = 100;  // s_axis_tvalid 을 올릴 확률
    parameter READY_PCT  = 100;  // l2_pool_ready 를 올릴 확률
    parameter SEED       = 1;
    parameter MAX_CYCLES = 400000;
    parameter MAX_REPORT = 10;   // 단계마다 콘솔에 찍을 불일치 최대 개수

    localparam IMG_W  = 28;
    localparam C1_W   = 26;
    localparam P1_W   = 13;
    localparam C2_W   = 11;
    localparam P2_W   = 5;
    localparam OCH1   = 6;
    localparam OCH2   = 16;

    localparam N_PIX  = IMG_W * IMG_W;       // 784  pixel / frame
    localparam C1_PIX = C1_W * C1_W;         // 676
    localparam P1_PIX = P1_W * P1_W;         // 169
    localparam C2_PIX = C2_W * C2_W;         // 121
    localparam N_C1   = C1_PIX * OCH1 / 3;   // 1352 entry / frame (3 lane)
    localparam N_P1   = P1_PIX * OCH1 / 3;   // 338
    localparam P2_PIX = P2_W * P2_W;         // 25
    localparam N_C2   = C2_PIX * OCH2;       // 1936 (1 lane)
    localparam N_P2   = P2_PIX * OCH2;       // 400  (1 lane)

    // ---------------- DUT ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;
    wire [15:0] l2_pool_data;
    wire        l2_pool_valid;
    reg         l2_pool_ready;
    wire        l2_pool_ch_done;

    cnn_top dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .s_axis_tdata  (s_axis_tdata),
        .s_axis_tvalid (s_axis_tvalid),
        .s_axis_tready (s_axis_tready),
        .s_axis_tuser  (s_axis_tuser),
        .s_axis_tlast  (s_axis_tlast),
        .l2_pool_data   (l2_pool_data),
        .l2_pool_valid  (l2_pool_valid),
        .l2_pool_ready  (l2_pool_ready),
        .l2_pool_ch_done(l2_pool_ch_done)
    );

    always #5 clk = ~clk;

    // ---------------- golden data ----------------
    reg [ 15:0] stim   [0:FRAMES*N_PIX-1];
    reg [ 48:0] gold_c1[0:FRAMES*N_C1-1];
    reg [ 48:0] gold_p1[0:FRAMES*N_P1-1];
    reg [ 16:0] gold_c2[0:FRAMES*N_C2-1];
    reg [ 16:0] gold_p2[0:FRAMES*N_P2-1];
    reg [431:0] wref   [0:OCH2*2-1];

    // captured RTL streams - report 도 콘솔 판정도 이 배열을 쓴다
    reg [ 48:0] got_c1 [0:FRAMES*N_C1-1];
    reg [ 48:0] got_p1 [0:FRAMES*N_P1-1];
    reg [ 16:0] got_c2 [0:FRAMES*N_C2-1];
    reg [ 16:0] got_p2 [0:FRAMES*N_P2-1];

    // ---------------- counters ----------------
    integer pix;                   // 보낸 pixel 수
    integer n_c1, n_p1, n_c2, n_p2;      // 단계별 받은 entry 수 (blocking)
    integer err_c1, err_p1, err_c2, err_p2, err_hs;
    integer shown_c1, shown_p1, shown_c2, shown_p2, shown_hs;
    integer stall_p2;                    // l2_pool_valid & ~l2_pool_ready 사이클 수
    integer img_done;                    // 결과가 다 나온 이미지 수
    integer rom_checks, rom_errs, rom_shown;
    integer ovr_l1, ovr_l2;
    integer cyc, running, seed, fd_trace, i;
    integer t_c1_last, t_c2_last, t_p2_last;
    reg [31:0] roll_v, roll_r;

    // ---------------- AXIS source ----------------
    // valid 는 한 번 올리면 받을 때까지 유지. tdata / tuser / tlast 는 pix 에서 조합으로 뽑는다.
    wire        pix_fire = s_axis_tvalid && s_axis_tready;

    always @(*) begin
        s_axis_tdata = (pix < FRAMES * N_PIX) ? stim[pix] : 16'd0;
        s_axis_tuser = (pix % N_PIX == 0);
        s_axis_tlast = (pix % N_PIX == N_PIX - 1);
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pix           <= 0;
            s_axis_tvalid <= 1'b0;
            l2_pool_ready <= 1'b0;
        end else if (running) begin
            roll_v = $unsigned($random(seed)) % 100;
            roll_r = $unsigned($random(seed)) % 100;
            if (pix_fire) pix <= pix + 1;
            if (!s_axis_tvalid || s_axis_tready)
                s_axis_tvalid <= ((pix + pix_fire) < FRAMES * N_PIX) &&
                                 ((pix + pix_fire) / N_PIX <= img_done) && (roll_v < VALID_PCT);
            l2_pool_ready <= (roll_r < READY_PCT);
        end
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // ---------------- [PIX] ----------------
    always @(posedge clk) begin
        if (rst_n && pix_fire)
            $fdisplay(fd_trace, "%10t %7d  PIX %5d  f%0d        r%2d c%2d   %6d%0s%0s",
                      $realtime, cyc, pix, pix / N_PIX, (pix % N_PIX) / IMG_W, pix % IMG_W,
                      $signed(s_axis_tdata), s_axis_tuser ? "  tuser" : "", s_axis_tlast ? "  tlast" : "");
    end

    // ---------------- [C1] conv_l1 출력 ----------------
    wire [48:0] c1_word = {dut.l1_out_ch_done, dut.l1_out_data2, dut.l1_out_data1, dut.l1_out_data0};

    always @(posedge clk) begin
        if (rst_n && dut.l1_out_valid && dut.l1_out_ready) begin
            if (n_c1 < FRAMES * N_C1) begin
                got_c1[n_c1] = c1_word;
                $fdisplay(fd_trace, "%10t %7d   C1 %5d  f%0d pass%0d r%2d c%2d   %6d %6d %6d done=%b  exp %6d %6d %6d done=%b  %0s",
                          $realtime, cyc, n_c1, n_c1 / N_C1, (n_c1 % N_C1) / C1_PIX,
                          (n_c1 % C1_PIX) / C1_W, n_c1 % C1_W,
                          $signed(c1_word[15:0]), $signed(c1_word[31:16]), $signed(c1_word[47:32]), c1_word[48],
                          $signed(gold_c1[n_c1][15:0]), $signed(gold_c1[n_c1][31:16]),
                          $signed(gold_c1[n_c1][47:32]), gold_c1[n_c1][48],
                          (c1_word === gold_c1[n_c1]) ? "OK" : "MISMATCH");
                if (c1_word !== gold_c1[n_c1]) begin
                    err_c1 = err_c1 + 1;
                    if (shown_c1 < MAX_REPORT) begin
                        shown_c1 = shown_c1 + 1;
                        $display("[FAIL][C1] entry %0d (frame %0d pass %0d r%0d c%0d) @ %t: got %0d %0d %0d done=%b / exp %0d %0d %0d done=%b",
                                 n_c1, n_c1 / N_C1, (n_c1 % N_C1) / C1_PIX, (n_c1 % C1_PIX) / C1_W, n_c1 % C1_W,
                                 $realtime, $signed(c1_word[15:0]), $signed(c1_word[31:16]),
                                 $signed(c1_word[47:32]), c1_word[48],
                                 $signed(gold_c1[n_c1][15:0]), $signed(gold_c1[n_c1][31:16]),
                                 $signed(gold_c1[n_c1][47:32]), gold_c1[n_c1][48]);
                    end
                end
            end else begin
                err_c1 = err_c1 + 1;
                $fdisplay(fd_trace, "%10t %7d   C1 %5d  EXTRA", $realtime, cyc, n_c1);
            end
            n_c1 = n_c1 + 1;
            t_c1_last = cyc;
        end
    end

    // ---------------- [P1] pool_l1 출력 ----------------
    wire [48:0] p1_word = {dut.l1_pool_ch_done, dut.l1_pool_data2, dut.l1_pool_data1, dut.l1_pool_data0};

    always @(posedge clk) begin
        if (rst_n && dut.l1_pool_valid && dut.l1_pool_ready) begin
            if (n_p1 < FRAMES * N_P1) begin
                got_p1[n_p1] = p1_word;
                $fdisplay(fd_trace, "%10t %7d   P1 %5d  f%0d pass%0d r%2d c%2d   %6d %6d %6d done=%b  exp %6d %6d %6d done=%b  %0s",
                          $realtime, cyc, n_p1, n_p1 / N_P1, (n_p1 % N_P1) / P1_PIX,
                          (n_p1 % P1_PIX) / P1_W, n_p1 % P1_W,
                          $signed(p1_word[15:0]), $signed(p1_word[31:16]), $signed(p1_word[47:32]), p1_word[48],
                          $signed(gold_p1[n_p1][15:0]), $signed(gold_p1[n_p1][31:16]),
                          $signed(gold_p1[n_p1][47:32]), gold_p1[n_p1][48],
                          (p1_word === gold_p1[n_p1]) ? "OK" : "MISMATCH");
                if (p1_word !== gold_p1[n_p1]) begin
                    err_p1 = err_p1 + 1;
                    if (shown_p1 < MAX_REPORT) begin
                        shown_p1 = shown_p1 + 1;
                        $display("[FAIL][P1] entry %0d (frame %0d pass %0d r%0d c%0d) @ %t: got %0d %0d %0d done=%b / exp %0d %0d %0d done=%b",
                                 n_p1, n_p1 / N_P1, (n_p1 % N_P1) / P1_PIX, (n_p1 % P1_PIX) / P1_W, n_p1 % P1_W,
                                 $realtime, $signed(p1_word[15:0]), $signed(p1_word[31:16]),
                                 $signed(p1_word[47:32]), p1_word[48],
                                 $signed(gold_p1[n_p1][15:0]), $signed(gold_p1[n_p1][31:16]),
                                 $signed(gold_p1[n_p1][47:32]), gold_p1[n_p1][48]);
                    end
                end
            end else begin
                err_p1 = err_p1 + 1;
                $fdisplay(fd_trace, "%10t %7d   P1 %5d  EXTRA", $realtime, cyc, n_p1);
            end
            n_p1 = n_p1 + 1;
        end
    end

    // ---------------- [ROM] conv_l2 weight ROM ----------------
    wire [431:0] rom_expect = wref[dut.U_CONV_L2.out_ch_sel*2 + dut.U_CONV_L2.rom_is_ch35];

    always @(posedge clk) begin
        if (rst_n && dut.U_CONV_L2.cal_valid) begin
            rom_checks = rom_checks + 1;
            if (dut.U_CONV_L2.weight_out !== rom_expect) begin
                rom_errs = rom_errs + 1;
                if (rom_shown < MAX_REPORT) begin
                    rom_shown = rom_shown + 1;
                    $display("[FAIL][ROM] %t cyc %0d: out_ch_sel=%0d is_ch35=%b, weight_out.tap0=%0d exp %0d",
                             $realtime, cyc, dut.U_CONV_L2.out_ch_sel, dut.U_CONV_L2.rom_is_ch35,
                             $signed(dut.U_CONV_L2.weight_out[15:0]), $signed(rom_expect[15:0]));
                end
            end
        end
    end

    // ---------------- [C2] conv_l2 출력 ----------------
    wire [16:0] c2_word = {dut.l2_out_ch_done, dut.l2_out_data};

    always @(posedge clk) begin
        if (rst_n && dut.l2_out_valid && dut.l2_out_ready) begin
            if (n_c2 < FRAMES * N_C2) begin
                got_c2[n_c2] = c2_word;
                $fdisplay(fd_trace, "%10t %7d   C2 %5d  f%0d och%2d r%2d c%2d   %6d (%04x) done=%b  exp %6d (%04x) done=%b  %0s",
                          $realtime, cyc, n_c2, n_c2 / N_C2, (n_c2 % N_C2) / C2_PIX,
                          (n_c2 % C2_PIX) / C2_W, n_c2 % C2_W,
                          $signed(c2_word[15:0]), c2_word[15:0], c2_word[16],
                          $signed(gold_c2[n_c2][15:0]), gold_c2[n_c2][15:0], gold_c2[n_c2][16],
                          (c2_word === gold_c2[n_c2]) ? "OK" : "MISMATCH");
                if (c2_word !== gold_c2[n_c2]) begin
                    err_c2 = err_c2 + 1;
                    if (shown_c2 < MAX_REPORT) begin
                        shown_c2 = shown_c2 + 1;
                        $display("[FAIL][C2] entry %0d (frame %0d och %0d r%0d c%0d) @ %t: got %0d done=%b / exp %0d done=%b",
                                 n_c2, n_c2 / N_C2, (n_c2 % N_C2) / C2_PIX, (n_c2 % C2_PIX) / C2_W,
                                 n_c2 % C2_W, $realtime, $signed(c2_word[15:0]), c2_word[16],
                                 $signed(gold_c2[n_c2][15:0]), gold_c2[n_c2][16]);
                    end
                end
            end else begin
                err_c2 = err_c2 + 1;
                $fdisplay(fd_trace, "%10t %7d   C2 %5d  EXTRA  %6d done=%b", $realtime, cyc, n_c2,
                          $signed(c2_word[15:0]), c2_word[16]);
            end
            n_c2 = n_c2 + 1;
            t_c2_last = cyc;
        end
    end

    // ---------------- [P2] pool_l2 출력 = cnn_top 출력 ----------------
    wire [16:0] p2_word = {l2_pool_ch_done, l2_pool_data};

    always @(posedge clk) begin
        if (rst_n && l2_pool_valid && l2_pool_ready) begin
            if (n_p2 < FRAMES * N_P2) begin
                got_p2[n_p2] = p2_word;
                $fdisplay(fd_trace, "%10t %7d   P2 %5d  f%0d och%2d r%2d c%2d   %6d (%04x) done=%b  exp %6d (%04x) done=%b  %0s",
                          $realtime, cyc, n_p2, n_p2 / N_P2, (n_p2 % N_P2) / P2_PIX,
                          (n_p2 % P2_PIX) / P2_W, n_p2 % P2_W,
                          $signed(l2_pool_data), l2_pool_data, l2_pool_ch_done,
                          $signed(gold_p2[n_p2][15:0]), gold_p2[n_p2][15:0], gold_p2[n_p2][16],
                          (p2_word === gold_p2[n_p2]) ? "OK" : "MISMATCH");
                if (p2_word !== gold_p2[n_p2]) begin
                    err_p2 = err_p2 + 1;
                    if (shown_p2 < MAX_REPORT) begin
                        shown_p2 = shown_p2 + 1;
                        $display("[FAIL][P2] entry %0d (frame %0d och %0d r%0d c%0d) @ %t: got %0d done=%b / exp %0d done=%b",
                                 n_p2, n_p2 / N_P2, (n_p2 % N_P2) / P2_PIX, (n_p2 % P2_PIX) / P2_W,
                                 n_p2 % P2_W, $realtime, $signed(l2_pool_data), l2_pool_ch_done,
                                 $signed(gold_p2[n_p2][15:0]), gold_p2[n_p2][16]);
                    end
                end
            end else begin
                err_p2 = err_p2 + 1;
                $fdisplay(fd_trace, "%10t %7d   P2 %5d  EXTRA  %6d done=%b", $realtime, cyc, n_p2,
                          $signed(l2_pool_data), l2_pool_ch_done);
            end
            n_p2 = n_p2 + 1;
            t_p2_last = cyc;
        end
        img_done <= n_p2 / N_P2;
    end

    // ---------------- [HS] cnn_top 출력 handshake ----------------
    // 이전 사이클에 valid & ~ready 였으면 이번에도 같은 값으로 valid 여야 한다
    reg [16:0] hs_word;
    reg        hs_hold;

    always @(posedge clk) begin
        if (rst_n && hs_hold && !(l2_pool_valid && p2_word === hs_word)) begin
            err_hs = err_hs + 1;
            if (shown_hs < MAX_REPORT) begin
                shown_hs = shown_hs + 1;
                $display("[FAIL][HS] %t cyc %0d: l2_pool_valid=%b value %0d done=%b, but %0d done=%b was not taken yet",
                         $realtime, cyc, l2_pool_valid, l2_pool_data, l2_pool_ch_done, hs_word[15:0], hs_word[16]);
            end
        end
        hs_hold = rst_n && l2_pool_valid && !l2_pool_ready;
        hs_word = p2_word;
        if (rst_n && l2_pool_valid && !l2_pool_ready) stall_p2 = stall_p2 + 1;
    end

    // ---------------- [OVR] reorder overrun ----------------
    wire rb1_push = dut.U_CONV_L1.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.push;
    wire rb1_full = dut.U_CONV_L1.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.frame_full;
    wire rb2_push = dut.U_CONV_L2.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.push;
    wire rb2_full = dut.U_CONV_L2.U_OUTPUT_STAGE_L1.u_relu_quant.u_out_reorder.frame_full;

    always @(posedge clk) begin
        if (rst_n && rb1_push && rb1_full) begin
            if (ovr_l1 < MAX_REPORT)
                $display("[FAIL][OVR] %t cyc %0d: conv_l1 out_reorder full, push dropped", $realtime, cyc);
            ovr_l1 = ovr_l1 + 1;
        end
        if (rst_n && rb2_push && rb2_full) begin
            if (ovr_l2 < MAX_REPORT)
                $display("[FAIL][OVR] %t cyc %0d: conv_l2 out_reorder full, push dropped", $realtime, cyc);
            ovr_l2 = ovr_l2 + 1;
        end
    end

    // ========================================================================
    // report
    // ========================================================================
    // stage 1 = C1, 2 = P1, 3 = C2, 4 = P2. 채널 ch 의 pixel p 가 스트림에서 몇 번째 entry / 몇 번 lane 인지.
    function integer ent_idx;
        input integer stage, f, ch, p;
        case (stage)
            1: ent_idx = f * N_C1 + (ch / 3) * C1_PIX + p;
            2: ent_idx = f * N_P1 + (ch / 3) * P1_PIX + p;
            3: ent_idx = f * N_C2 + ch * C2_PIX + p;
            default: ent_idx = f * N_P2 + ch * P2_PIX + p;
        endcase
    endfunction

    function [16:0] ent_val;  // {ch_done, lane value}
        input integer stage, src, e, ch;  // src 0 = RTL, 1 = golden
        reg [48:0] w;
        begin
            case (stage)
                1: w = src ? gold_c1[e] : got_c1[e];
                2: w = src ? gold_p1[e] : got_p1[e];
                3: w = src ? {32'd0, gold_c2[e]} : {32'd0, got_c2[e]};
                default: w = src ? {32'd0, gold_p2[e]} : {32'd0, got_p2[e]};
            endcase
            if (stage >= 3) ent_val = w[16:0];
            else ent_val = {w[48], w[16*(ch%3)+:16]};
        end
    endfunction

    function integer ent_count;
        input integer stage;
        case (stage)
            1: ent_count = n_c1;
            2: ent_count = n_p1;
            3: ent_count = n_c2;
            default: ent_count = n_p2;
        endcase
    endfunction

    task print_grid;
        input integer fd, stage, f, ch;
        integer W, r, c, e, bad, listed;
        reg [16:0] g, x;
        begin
            W = (stage == 1) ? C1_W : (stage == 2) ? P1_W : (stage == 3) ? C2_W : P2_W;
            bad = 0;
            for (e = 0; e < W * W; e = e + 1) begin
                i = ent_idx(stage, f, ch, e);
                if (i >= ent_count(stage) || ent_val(stage, 0, i, ch) !== ent_val(stage, 1, i, ch)) bad = bad + 1;
            end
            $fdisplay(fd, "\n[%0s frame %0d / %0s %0d, %0dx%0d]  %0s",
                      (stage == 1) ? "C1 conv_l1" : (stage == 2) ? "P1 pool_l1" : (stage == 3) ? "C2 conv_l2" : "P2 pool_l2",
                      f, (stage >= 3) ? "out_ch" : "ch", ch, W, W, (bad == 0) ? "OK" : "MISMATCH");
            $fwrite(fd, "       ");
            for (c = 0; c < W; c = c + 1) $fwrite(fd, "    c%2d", c);
            $fwrite(fd, "\n");
            for (r = 0; r < W; r = r + 1) begin
                $fwrite(fd, " r%2d | ", r);
                for (c = 0; c < W; c = c + 1) begin
                    i = ent_idx(stage, f, ch, r * W + c);
                    x = ent_val(stage, 0, i, ch);
                    g = ent_val(stage, 1, i, ch);
                    if (i >= ent_count(stage)) $fwrite(fd, "   ----");
                    else if (x[15:0] !== g[15:0]) $fwrite(fd, "*%6d", $signed(x[15:0]));
                    else if (x[16] !== g[16]) $fwrite(fd, "!%6d", $signed(x[15:0]));
                    else $fwrite(fd, " %6d", $signed(x[15:0]));
                end
                $fwrite(fd, "\n");
            end
            if (bad != 0) begin
                listed = 0;
                $fdisplay(fd, "   mismatch %0d 개 (최대 20개 표시):", bad);
                for (e = 0; e < W * W && listed < 20; e = e + 1) begin
                    i = ent_idx(stage, f, ch, e);
                    x = ent_val(stage, 0, i, ch);
                    g = ent_val(stage, 1, i, ch);
                    if (i >= ent_count(stage)) begin
                        listed = listed + 1;
                        $fdisplay(fd, "     r%2d c%2d : rtl   ----          exp %6d done=%b", e / W, e % W,
                                  $signed(g[15:0]), g[16]);
                    end else if (x !== g) begin
                        listed = listed + 1;
                        $fdisplay(fd, "     r%2d c%2d : rtl %6d done=%b  exp %6d done=%b", e / W, e % W,
                                  $signed(x[15:0]), x[16], $signed(g[15:0]), g[16]);
                    end
                end
            end
        end
    endtask

    task write_report;
        integer fd, f, ch, r, c;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "cnn_top (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2) test report  (values in signed decimal)");
            $fdisplay(fd, "  VALID_PCT=%0d READY_PCT=%0d SEED=%0d FRAMES=%0d",
                      VALID_PCT, READY_PCT, SEED, FRAMES);
            $fdisplay(fd, "  %0d cycles, pixel %0d / %0d, output stall %0d cycles", cyc, pix, FRAMES * N_PIX, stall_p2);
            $fdisplay(fd, "  [C1]  conv_l1 out : %4d / %4d entries, %0d wrong   (ce1_out.mem)", n_c1, FRAMES * N_C1, err_c1);
            $fdisplay(fd, "  [P1]  pool_l1 out : %4d / %4d entries, %0d wrong   (pool1_out.mem)", n_p1, FRAMES * N_P1, err_p1);
            $fdisplay(fd, "  [ROM] conv_l2 rom : %0d / %0d cal_valid cycles wrong", rom_errs, rom_checks);
            $fdisplay(fd, "  [C2]  conv_l2 out : %4d / %4d entries, %0d wrong   (ce2_out.mem)", n_c2, FRAMES * N_C2, err_c2);
            $fdisplay(fd, "  [P2]  pool_l2 out : %4d / %4d entries, %0d wrong   (pool2_out.mem)", n_p2, FRAMES * N_P2, err_p2);
            $fdisplay(fd, "  [HS]  handshake   : %0d violations on l2_pool_valid / l2_pool_ready", err_hs);
            $fdisplay(fd, "  [OVR] dropped push: conv_l1 %0d, conv_l2 %0d", ovr_l1, ovr_l2);
            $fdisplay(fd, "  RESULT : %0s", pass_all(0) ? "PASS" : "FAIL");
            $fdisplay(fd, "");
            $fdisplay(fd, "  값 = TB 가 각 단계 valid & ready 에서 잡은 RTL 출력");
            $fdisplay(fd, "  '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름, ---- = 출력 안 나옴");
            $fdisplay(fd, "  각 값의 sim time / hex 는 %s 참고", TRACE_FILE);

            for (f = 0; f < FRAMES; f = f + 1) begin
                $fdisplay(fd, "\n##################################################################");
                $fdisplay(fd, " FRAME %0d", f);
                $fdisplay(fd, "##################################################################");
                $fdisplay(fd, "\n[INPUT frame %0d, 28x28]", f);
                $fwrite(fd, "       ");
                for (c = 0; c < IMG_W; c = c + 1) $fwrite(fd, "    c%2d", c);
                $fwrite(fd, "\n");
                for (r = 0; r < IMG_W; r = r + 1) begin
                    $fwrite(fd, " r%2d | ", r);
                    for (c = 0; c < IMG_W; c = c + 1)
                        $fwrite(fd, " %6d", $signed(stim[f*N_PIX+r*IMG_W+c]));
                    $fwrite(fd, "\n");
                end
                for (ch = 0; ch < OCH1; ch = ch + 1) print_grid(fd, 1, f, ch);
                for (ch = 0; ch < OCH1; ch = ch + 1) print_grid(fd, 2, f, ch);
                for (ch = 0; ch < OCH2; ch = ch + 1) print_grid(fd, 3, f, ch);
                for (ch = 0; ch < OCH2; ch = ch + 1) print_grid(fd, 4, f, ch);
            end
            $fclose(fd);
        end
    endtask

    function pass_all;
        input dummy;
        pass_all = (err_c1 == 0 && err_p1 == 0 && err_c2 == 0 && err_p2 == 0 && err_hs == 0 &&
                    rom_errs == 0 && ovr_l1 == 0 && ovr_l2 == 0 &&
                    n_c1 == FRAMES * N_C1 && n_p1 == FRAMES * N_P1 && n_c2 == FRAMES * N_C2 &&
                    n_p2 == FRAMES * N_P2);
    endfunction

    task stage_summary;
        input [8*32-1:0] name;
        input integer got, exp, errs;
        begin
            if (got < exp)
                $display("  %0s : got %0d / %0d entries%0s, %0d wrong", name, got, exp,
                         (cyc >= MAX_CYCLES) ? " (TIMEOUT)" : "", errs);
            else if (errs == 0)
                $display("  %0s : all %0d entries match", name, exp);
            else
                $display("  %0s : %0d of %0d entries wrong", name, errs, got);
        end
    endtask

    // ========================================================================
    // main
    // ========================================================================
    initial begin
        $timeformat(-9, 0, " ns", 10);
        clk        = 1'b0;
        rst_n      = 1'b0;
        running    = 0;
        seed       = SEED;
        cyc        = 0;
        n_c1 = 0; n_p1 = 0; n_c2 = 0; n_p2 = 0;
        err_c1 = 0; err_p1 = 0; err_c2 = 0; err_p2 = 0; err_hs = 0;
        shown_c1 = 0; shown_p1 = 0; shown_c2 = 0; shown_p2 = 0; shown_hs = 0;
        stall_p2 = 0; hs_hold = 1'b0; hs_word = 17'd0; img_done = 0;
        rom_checks = 0; rom_errs = 0; rom_shown = 0;
        ovr_l1 = 0; ovr_l2 = 0;
        t_c1_last = 0; t_c2_last = 0; t_p2_last = 0;
        for (i = 0; i < FRAMES * N_C1; i = i + 1) got_c1[i] = 49'bx;
        for (i = 0; i < FRAMES * N_P1; i = i + 1) got_p1[i] = 49'bx;
        for (i = 0; i < FRAMES * N_C2; i = i + 1) got_c2[i] = 17'bx;
        for (i = 0; i < FRAMES * N_P2; i = i + 1) got_p2[i] = 17'bx;

        $readmemh("ce1_stim.mem", stim);
        $readmemh("ce1_out.mem", gold_c1);
        $readmemh("pool1_out.mem", gold_p1);
        $readmemh("ce2_out.mem", gold_c2);
        $readmemh("pool2_out.mem", gold_p2);
        $readmemh("conv2_weight.mem", wref);
        if (^stim[0] === 1'bx || ^gold_c1[0] === 1'bx || ^gold_p1[0] === 1'bx ||
            ^gold_c2[0] === 1'bx || ^gold_p2[0] === 1'bx || ^wref[0] === 1'bx) begin
            $display("[FAIL] golden .mem not loaded - add ce1_stim / ce1_out / pool1_out / ce2_out / pool2_out / conv2_weight .mem to the sim sources");
            $finish;
        end

        fd_trace = $fopen(TRACE_FILE, "w");
        $fdisplay(fd_trace, "cnn_top pool_l2 handshake trace  (time = posedge 시각, 파형과 동일, 값은 signed 10진수)");
        $fdisplay(fd_trace, "      time   cycle stage  idx  position            value");

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;

        // (W-bias) 두 conv 의 bias ROM 을 골든과 짝이 맞는 값으로 덮어쓴다
        $readmemh("conv1_bias_ce.mem", dut.U_CONV_L1.U_OUTPUT_STAGE_L1.GEN_CONV1.u_output_buffer.u_bias_rom.mem);
        $readmemh("conv2_bias_ce.mem", dut.U_CONV_L2.U_OUTPUT_STAGE_L1.GEN_CONV2.u_output_buffer.u_bias_rom.mem);
        $display("cnn_top pool_l2 TB: %0d frame, VALID_PCT=%0d READY_PCT=%0d",
                 FRAMES, VALID_PCT, READY_PCT);

        @(negedge clk);
        running = 1;

        while (n_p2 < FRAMES * N_P2 && cyc < MAX_CYCLES) @(posedge clk);
        repeat (50) @(posedge clk);  // 초과 출력이 나오는지 조금 더 본다
        running = 0;

        // ---------------- summary ----------------
        $display("\nsummary (%0d cycles, last output: conv_l1 cyc %0d, conv_l2 cyc %0d, pool_l2 cyc %0d, output stall %0d cycles):",
                 cyc, t_c1_last, t_c2_last, t_p2_last, stall_p2);
        stage_summary("[C1]  conv_l1 vs ce1_out  ", n_c1, FRAMES * N_C1, err_c1);
        stage_summary("[P1]  pool_l1 vs pool1_out", n_p1, FRAMES * N_P1, err_p1);
        if (rom_errs == 0)
            $display("  [ROM] conv_l2 weight ROM   : correct on all %0d cal_valid cycles", rom_checks);
        else
            $display("  [ROM] conv_l2 weight ROM   : %0d of %0d cal_valid cycles wrong", rom_errs, rom_checks);
        stage_summary("[C2]  conv_l2 vs ce2_out  ", n_c2, FRAMES * N_C2, err_c2);
        stage_summary("[P2]  pool_l2 vs pool2_out", n_p2, FRAMES * N_P2, err_p2);
        $display("  [HS]  output handshake     : %0d violations", err_hs);
        $display("  [OVR] dropped push         : conv_l1 %0d, conv_l2 %0d", ovr_l1, ovr_l2);

        write_report;
        $fclose(fd_trace);
        $display("\nwrote %s, %s", REPORT_FILE, TRACE_FILE);

        if (pass_all(0)) $display("\n[PASS] cnn_top pool_l2: %0d cycles.", cyc);
        else $display("\n[FAIL] cnn_top pool_l2 - see %s", REPORT_FILE);
        $finish;
    end

endmodule
