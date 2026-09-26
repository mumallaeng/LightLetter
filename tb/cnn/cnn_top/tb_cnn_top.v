`timescale 1ns / 1ps

// cnn_top 전체 (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> fc_top -> argmax) 를 C 골든모델과 비교한다.
//
//   28x28x1 -> conv_l1 -> 26x26x6 -> pool_l1 -> 13x13x6 -> conv_l2 -> 11x11x16 -> pool_l2 -> 400
//           -> FC1 -> 120 -> FC2 -> 84 -> FC3 -> logit 36 -> argmax -> cnn_result (class 0..35), cnn_done
//
// 실행: 이 폴더에서  ./run_sim.sh  (Vivado xsim, 옵션은 run_sim.sh 참고)
//       .mem 은 모두 bare 파일명으로 읽으므로 run_sim.sh 가 build/ 에 모아 두고 거기서 돌린다.
//
// 입력 / 기대값 (FRAMES = 2 : frame 0 = 실제 이미지 (손글씨 "6"), frame 1 = 좌우 반전)
//   rtl/cnn/rtl_ref/ (C 골든모델 cnn_chain rtl-vectors)
//     ce1_stim.mem      16bit x 1568  conv_l1 pixel 입력, frame 당 28x28 raster
//     ce1_out.mem       49bit x 2704  conv_l1 출력 {ch_done, d2, d1, d0}
//     pool1_out.mem     49bit x  676  pool_l1 출력 {ch_done, d2, d1, d0}
//     ce2_out.mem       17bit x 3872  conv_l2 출력 {ch_done, data}
//     pool2_out.mem     17bit x  800  pool_l2 출력 {ch_done, data} = FC1 입력
//     conv2_weight.mem 432bit x   32  conv_l2 weight ROM 기대값 ([och][is_ch35])
//     conv1_bias_ce.mem / conv2_bias_ce.mem  골든과 짝이 맞는 INT32 bias
//   tb/cnn/cnn_top/vectors/ (gen_fc_golden.py : pool2_out.mem 에서 FC 골든 규칙으로 계산)
//     fc1_out.mem       16bit x  240  FC1 출력 (neuron 0..119)
//     fc2_out.mem       16bit x  168  FC2 출력 (neuron 0..83)
//     logit_out.mem     16bit x   72  FC3 출력 = logit (signed, class 0..35)
//     class_out.mem      8bit x    2  argmax 결과
//
// 확인하는 것 (단계 경계마다 handshake 순서대로 골든 스트림과 비교)
//   [C1]  conv_l1 출력  (l1_out_valid & l1_out_ready)          vs ce1_out.mem
//   [P1]  pool_l1 출력  (l1_pool_valid & l1_pool_ready)        vs pool1_out.mem
//   [ROM] conv_l2 weight ROM - cal_valid 마다 weight_out == conv2_weight[out_ch_sel*2 + is_ch35]
//   [C2]  conv_l2 출력  (l2_out_valid & l2_out_ready)          vs ce2_out.mem
//   [P2]  pool_l2 출력  (l2_pool_valid & l2_pool_ready)        vs pool2_out.mem
//   [F1]  FC1 출력      (U_FC.l1_out_valid & U_FC.l2_in_ready)  vs fc1_out.mem
//   [F2]  FC2 출력      (U_FC.l2_out_valid & U_FC.l3_in_ready)  vs fc2_out.mem
//   [LG]  FC3 출력      (logit_valid & logit_ready)            vs logit_out.mem
//   [AM]  argmax        cnn_done 은 36 번째 logit 을 받은 바로 다음 클럭에만 1 클럭,
//                       그때 cnn_result == class_out.mem (그리고 == RTL 이 실제로 낸 logit 의 argmax)
//   [HS]  pool_l2 -> fc_top, FC1 -> FC2, FC2 -> FC3 handshake - valid 가 뜬 뒤 ready 전에 내려가거나 값이 바뀌면 위반
//   [OVR] conv_l1 / conv_l2 out_reorder 가 꽉 찬 상태에서 버려진 push 횟수
//   값 비교는 RTL 값이나 기대값에 x 가 하나라도 있으면 불일치로 센다 (x === x 로 통과하지 않게).
//   .mem 을 못 읽으면 [FAIL] 로 멈추고, Vivado GUI 에서 계속 run 해도 결과는 FAIL 로 남는다.
//
// 입력 방식 (GATE)
//   0 : 버튼을 누를 때마다 이미지 한 장 (실제 사용과 같음). 다음 이미지는 앞 이미지의 cnn_done 뒤에 넣는다.
//   1 : 다음 이미지는 conv_l1 이 앞 프레임 출력을 다 내보낸 뒤 넣는다 (C 골든모델 test_cnn_top 과 같은 게이트,
//       프레임이 파이프라인에서 겹친다).
//   이미지 사이에 리셋은 하지 않는다. cnn_top 에는 출력 ready 가 없어 (argmax logit_ready = 1) 입력 valid 만 흔든다.
//
// 결과 파일 (시뮬레이션 실행 디렉터리)
//   REPORT_FILE : 요약 + frame 별 단계 격자 (입력, C1, P1, C2, P2) + FC1 / FC2 / logit 표 + argmax 결과, signed 10진수.
//                 '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름, ---- = 출력 안 나옴
//   TRACE_FILE  : 모든 단계 handshake 의 sim time 과 값 (10진수, 괄호 안 hex). time 은 파형의 posedge 시각.
//
// TB 우회 (RTL 은 건드리지 않음)
//   (W-bias, BIAS_FIX = 1) conv_out_stage 가 "conv1_bias.mem" / "conv2_bias.mem" (rtl/cnn/mem) 을 박아 넣는데,
//             지금 weight 와 짝이 맞는 bias 는 rtl_ref/conv{1,2}_bias_ce.mem 이다. TB 가 두 bias ROM 을 덮어쓴다.
//             BIAS_FIX = 0 이면 rtl/cnn/mem 의 bias 그대로 돌린다 (지금은 C1 부터 골든과 달라진다).

module tb_cnn_top;

    // ---------------- parameters ----------------
    parameter REPORT_FILE = "tb_cnn_top_report.txt";
    parameter TRACE_FILE  = "tb_cnn_top_trace.txt";

    parameter FRAMES     = 2;
    parameter VALID_PCT  = 100;  // s_axis_tvalid 을 올릴 확률
    parameter GATE       = 0;    // 0: 앞 이미지 cnn_done 뒤, 1: conv_l1 이 앞 프레임을 다 내보낸 뒤
    parameter BIAS_FIX   = 1;    // 1: conv bias ROM 을 *_bias_ce.mem 으로 덮어쓴다
    parameter SEED       = 1;
    parameter MAX_CYCLES = 600000;
    parameter MAX_REPORT = 10;   // 단계마다 콘솔에 찍을 불일치 최대 개수

    localparam NUM_CLASS = 36;
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
    localparam P2_PIX = P2_W * P2_W;         // 25
    localparam N_C1   = C1_PIX * OCH1 / 3;   // 1352 entry / frame (3 lane)
    localparam N_P1   = P1_PIX * OCH1 / 3;   // 338
    localparam N_C2   = C2_PIX * OCH2;       // 1936 (1 lane)
    localparam N_P2   = P2_PIX * OCH2;       // 400  = FC1 입력
    localparam N_F1   = 120;
    localparam N_F2   = 84;
    localparam N_LG   = NUM_CLASS;           // 36

    // ---------------- DUT ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;
    wire [$clog2(NUM_CLASS)-1:0] cnn_result;
    wire        cnn_done;

    cnn_top #(
        .NUM_CLASS(NUM_CLASS)
    ) dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .cnn_result   (cnn_result),
        .cnn_done     (cnn_done)
    );

    always #5 clk = ~clk;

    // ---------------- golden data ----------------
    reg [ 15:0] stim    [0:FRAMES*N_PIX-1];
    reg [ 48:0] gold_c1 [0:FRAMES*N_C1-1];
    reg [ 48:0] gold_p1 [0:FRAMES*N_P1-1];
    reg [ 16:0] gold_c2 [0:FRAMES*N_C2-1];
    reg [ 16:0] gold_p2 [0:FRAMES*N_P2-1];
    reg [ 15:0] gold_f1 [0:FRAMES*N_F1-1];
    reg [ 15:0] gold_f2 [0:FRAMES*N_F2-1];
    reg [ 15:0] gold_lg [0:FRAMES*N_LG-1];
    reg [  7:0] gold_cls[0:FRAMES-1];
    reg [431:0] wref    [0:OCH2*2-1];

    // captured RTL streams - report 도 콘솔 판정도 이 배열을 쓴다
    reg [ 48:0] got_c1 [0:FRAMES*N_C1-1];
    reg [ 48:0] got_p1 [0:FRAMES*N_P1-1];
    reg [ 16:0] got_c2 [0:FRAMES*N_C2-1];
    reg [ 16:0] got_p2 [0:FRAMES*N_P2-1];
    reg [ 15:0] got_f1 [0:FRAMES*N_F1-1];
    reg [ 15:0] got_f2 [0:FRAMES*N_F2-1];
    reg [ 15:0] got_lg [0:FRAMES*N_LG-1];
    reg [  7:0] got_cls[0:FRAMES-1];
    integer     done_cyc[0:FRAMES-1];

    // ---------------- counters ----------------
    integer pix;                                          // 보낸 pixel 수
    integer n_c1, n_p1, n_c2, n_p2, n_f1, n_f2, n_lg, n_done;
    integer err_c1, err_p1, err_c2, err_p2, err_f1, err_f2, err_lg, err_am;
    integer shown_c1, shown_p1, shown_c2, shown_p2, shown_f1, shown_f2, shown_lg, shown_am;
    integer rom_checks, rom_errs, rom_shown;
    integer ovr_l1, ovr_l2;
    integer stall_p2;                                     // pool_l2 -> fc_top : valid & ~ready 사이클 수
    integer img_ok;                                       // 이 frame 번호까지 입력을 넣어도 된다
    integer cyc, running, seed, fd_trace, i;
    integer t_c1_last, t_c2_last, t_p2_last, t_lg_last;
    reg        am_expect;                                 // 이번 클럭에 cnn_done 이 1 이어야 한다
    reg [31:0] roll_v;

    // ---------------- x-aware compare ----------------
    // 값이 다르거나, 어느 한쪽에 x / z 가 있으면 1 (x === x 가 일치로 통과하지 않게)
    function neq49;
        input [48:0] a, b;
        neq49 = (a !== b) || (^a === 1'bx) || (^b === 1'bx);
    endfunction
    function neq17;
        input [16:0] a, b;
        neq17 = (a !== b) || (^a === 1'bx) || (^b === 1'bx);
    endfunction
    function neq16;
        input [15:0] a, b;
        neq16 = (a !== b) || (^a === 1'bx) || (^b === 1'bx);
    endfunction
    function neq8;
        input [7:0] a, b;
        neq8 = (a !== b) || (^a === 1'bx) || (^b === 1'bx);
    endfunction

    reg mem_ok;                                           // 골든 .mem 을 모두 읽었는지

    // ---------------- AXIS source ----------------
    // valid 는 한 번 올리면 받을 때까지 유지. tdata / tuser / tlast 는 pix 에서 조합으로 뽑는다.
    // tuser = frame 첫 pixel, tlast = frame 마지막 pixel (ce_ctrl_l1 은 tlast 로 이미지 끝을 센다)
    wire pix_fire = s_axis_tvalid && s_axis_tready;

    always @(*) begin
        s_axis_tdata = (pix < FRAMES * N_PIX) ? stim[pix] : 16'd0;
        s_axis_tuser = (pix % N_PIX == 0);
        s_axis_tlast = (pix % N_PIX == N_PIX - 1);
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pix           <= 0;
            s_axis_tvalid <= 1'b0;
        end else if (running) begin
            roll_v = $unsigned($random(seed)) % 100;
            if (pix_fire) pix <= pix + 1;
            if (!s_axis_tvalid || s_axis_tready)
                s_axis_tvalid <= ((pix + pix_fire) < FRAMES * N_PIX) &&
                                 ((pix + pix_fire) / N_PIX <= img_ok) && (roll_v < VALID_PCT);
        end
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // 프레임 게이트: GATE 0 = cnn_done 개수, GATE 1 = conv_l1 이 다 내보낸 frame 수
    always @(posedge clk) img_ok <= (GATE == 0) ? n_done : n_c1 / N_C1;

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
                          !neq49(c1_word, gold_c1[n_c1]) ? "OK" : "MISMATCH");
                if (neq49(c1_word, gold_c1[n_c1])) begin
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
                          !neq49(p1_word, gold_p1[n_p1]) ? "OK" : "MISMATCH");
                if (neq49(p1_word, gold_p1[n_p1])) begin
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
                          !neq17(c2_word, gold_c2[n_c2]) ? "OK" : "MISMATCH");
                if (neq17(c2_word, gold_c2[n_c2])) begin
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

    // ---------------- [P2] pool_l2 출력 = FC1 입력 ----------------
    wire [16:0] p2_word = {dut.l2_pool_ch_done, dut.l2_pool_data};

    always @(posedge clk) begin
        if (rst_n && dut.l2_pool_valid && dut.l2_pool_ready) begin
            if (n_p2 < FRAMES * N_P2) begin
                got_p2[n_p2] = p2_word;
                $fdisplay(fd_trace, "%10t %7d   P2 %5d  f%0d och%2d r%2d c%2d   %6d (%04x) done=%b  exp %6d (%04x) done=%b  %0s",
                          $realtime, cyc, n_p2, n_p2 / N_P2, (n_p2 % N_P2) / P2_PIX,
                          (n_p2 % P2_PIX) / P2_W, n_p2 % P2_W,
                          $signed(p2_word[15:0]), p2_word[15:0], p2_word[16],
                          $signed(gold_p2[n_p2][15:0]), gold_p2[n_p2][15:0], gold_p2[n_p2][16],
                          !neq17(p2_word, gold_p2[n_p2]) ? "OK" : "MISMATCH");
                if (neq17(p2_word, gold_p2[n_p2])) begin
                    err_p2 = err_p2 + 1;
                    if (shown_p2 < MAX_REPORT) begin
                        shown_p2 = shown_p2 + 1;
                        $display("[FAIL][P2] entry %0d (frame %0d och %0d r%0d c%0d) @ %t: got %0d done=%b / exp %0d done=%b",
                                 n_p2, n_p2 / N_P2, (n_p2 % N_P2) / P2_PIX, (n_p2 % P2_PIX) / P2_W,
                                 n_p2 % P2_W, $realtime, $signed(p2_word[15:0]), p2_word[16],
                                 $signed(gold_p2[n_p2][15:0]), gold_p2[n_p2][16]);
                    end
                end
            end else begin
                err_p2 = err_p2 + 1;
                $fdisplay(fd_trace, "%10t %7d   P2 %5d  EXTRA  %6d done=%b", $realtime, cyc, n_p2,
                          $signed(p2_word[15:0]), p2_word[16]);
            end
            n_p2 = n_p2 + 1;
            t_p2_last = cyc;
        end
        if (rst_n && dut.l2_pool_valid && !dut.l2_pool_ready) stall_p2 = stall_p2 + 1;
    end

    // ---------------- [F1] FC1 -> FC2 ----------------
    always @(posedge clk) begin
        if (rst_n && dut.U_FC.l1_out_valid && dut.U_FC.l2_in_ready) begin
            if (n_f1 < FRAMES * N_F1) begin
                got_f1[n_f1] = dut.U_FC.l1_out_data;
                $fdisplay(fd_trace, "%10t %7d   F1 %5d  f%0d n%3d            %6d (%04x)  exp %6d  %0s",
                          $realtime, cyc, n_f1, n_f1 / N_F1, n_f1 % N_F1,
                          $signed(dut.U_FC.l1_out_data), dut.U_FC.l1_out_data, $signed(gold_f1[n_f1]),
                          !neq16(dut.U_FC.l1_out_data, gold_f1[n_f1]) ? "OK" : "MISMATCH");
                if (neq16(dut.U_FC.l1_out_data, gold_f1[n_f1])) begin
                    err_f1 = err_f1 + 1;
                    if (shown_f1 < MAX_REPORT) begin
                        shown_f1 = shown_f1 + 1;
                        $display("[FAIL][F1] frame %0d neuron %0d @ %t: got %0d / exp %0d", n_f1 / N_F1, n_f1 % N_F1,
                                 $realtime, $signed(dut.U_FC.l1_out_data), $signed(gold_f1[n_f1]));
                    end
                end
            end else begin
                err_f1 = err_f1 + 1;
                $fdisplay(fd_trace, "%10t %7d   F1 %5d  EXTRA  %6d", $realtime, cyc, n_f1, $signed(dut.U_FC.l1_out_data));
            end
            n_f1 = n_f1 + 1;
        end
    end

    // ---------------- [F2] FC2 -> FC3 ----------------
    always @(posedge clk) begin
        if (rst_n && dut.U_FC.l2_out_valid && dut.U_FC.l3_in_ready) begin
            if (n_f2 < FRAMES * N_F2) begin
                got_f2[n_f2] = dut.U_FC.l2_out_data;
                $fdisplay(fd_trace, "%10t %7d   F2 %5d  f%0d n%3d            %6d (%04x)  exp %6d  %0s",
                          $realtime, cyc, n_f2, n_f2 / N_F2, n_f2 % N_F2,
                          $signed(dut.U_FC.l2_out_data), dut.U_FC.l2_out_data, $signed(gold_f2[n_f2]),
                          !neq16(dut.U_FC.l2_out_data, gold_f2[n_f2]) ? "OK" : "MISMATCH");
                if (neq16(dut.U_FC.l2_out_data, gold_f2[n_f2])) begin
                    err_f2 = err_f2 + 1;
                    if (shown_f2 < MAX_REPORT) begin
                        shown_f2 = shown_f2 + 1;
                        $display("[FAIL][F2] frame %0d neuron %0d @ %t: got %0d / exp %0d", n_f2 / N_F2, n_f2 % N_F2,
                                 $realtime, $signed(dut.U_FC.l2_out_data), $signed(gold_f2[n_f2]));
                    end
                end
            end else begin
                err_f2 = err_f2 + 1;
                $fdisplay(fd_trace, "%10t %7d   F2 %5d  EXTRA  %6d", $realtime, cyc, n_f2, $signed(dut.U_FC.l2_out_data));
            end
            n_f2 = n_f2 + 1;
        end
    end

    // ---------------- [LG] FC3 -> argmax (logit) ----------------
    always @(posedge clk) begin
        am_expect <= 1'b0;
        if (rst_n && dut.logit_valid && dut.logit_ready) begin
            if (n_lg < FRAMES * N_LG) begin
                got_lg[n_lg] = dut.logit_data;
                $fdisplay(fd_trace, "%10t %7d   LG %5d  f%0d class%3d        %6d (%04x)  exp %6d  %0s",
                          $realtime, cyc, n_lg, n_lg / N_LG, n_lg % N_LG,
                          $signed(dut.logit_data), dut.logit_data, $signed(gold_lg[n_lg]),
                          !neq16(dut.logit_data, gold_lg[n_lg]) ? "OK" : "MISMATCH");
                if (neq16(dut.logit_data, gold_lg[n_lg])) begin
                    err_lg = err_lg + 1;
                    if (shown_lg < MAX_REPORT) begin
                        shown_lg = shown_lg + 1;
                        $display("[FAIL][LG] frame %0d logit %0d @ %t: got %0d / exp %0d", n_lg / N_LG, n_lg % N_LG,
                                 $realtime, $signed(dut.logit_data), $signed(gold_lg[n_lg]));
                    end
                end
            end else begin
                err_lg = err_lg + 1;
                $fdisplay(fd_trace, "%10t %7d   LG %5d  EXTRA  %6d", $realtime, cyc, n_lg, $signed(dut.logit_data));
            end
            if (n_lg % N_LG == N_LG - 1) am_expect <= 1'b1;   // 다음 클럭에 cnn_done
            n_lg = n_lg + 1;
            t_lg_last = cyc;
        end
    end

    // ---------------- [AM] argmax 출력 ----------------
    // posedge 에서 보는 cnn_done / am_expect 는 둘 다 직전 클럭 값이다 (레지스터끼리 같은 시점).
    function [7:0] argmax_of;       // got_lg 의 frame f 에서 첫 최댓값의 index
        input integer f;
        integer k;
        reg signed [15:0] best;
        begin
            argmax_of = 0;
            best = got_lg[f * N_LG];
            for (k = 1; k < N_LG; k = k + 1)
                if ($signed(got_lg[f * N_LG + k]) > best) begin
                    best = got_lg[f * N_LG + k];
                    argmax_of = k;
                end
        end
    endfunction

    always @(posedge clk) begin
        if (rst_n && (cnn_done !== am_expect)) begin
            err_am = err_am + 1;
            if (shown_am < MAX_REPORT) begin
                shown_am = shown_am + 1;
                $display("[FAIL][AM] %t cyc %0d: cnn_done=%b but expected %b (logits so far %0d)", $realtime, cyc,
                         cnn_done, am_expect, n_lg);
            end
        end
        if (rst_n && cnn_done === 1'b1) begin
            if (n_done < FRAMES) begin
                got_cls[n_done]  = cnn_result;
                done_cyc[n_done] = cyc;
                $fdisplay(fd_trace, "%10t %7d   AM %5d  f%0d cnn_done  cnn_result = %0d  exp %0d, argmax(rtl logit) %0d  %0s",
                          $realtime, cyc, n_done, n_done, cnn_result, gold_cls[n_done], argmax_of(n_done),
                          (!neq8(cnn_result, gold_cls[n_done]) && !neq8(cnn_result, argmax_of(n_done))) ? "OK" : "MISMATCH");
                if (neq8(cnn_result, gold_cls[n_done]) || neq8(cnn_result, argmax_of(n_done))) begin
                    err_am = err_am + 1;
                    if (shown_am < MAX_REPORT) begin
                        shown_am = shown_am + 1;
                        $display("[FAIL][AM] frame %0d @ %t: cnn_result %0d, exp %0d, argmax of RTL logits %0d",
                                 n_done, $realtime, cnn_result, gold_cls[n_done], argmax_of(n_done));
                    end
                end
            end else begin
                err_am = err_am + 1;
                $fdisplay(fd_trace, "%10t %7d   AM %5d  EXTRA cnn_done  cnn_result = %0d", $realtime, cyc, n_done, cnn_result);
            end
            n_done = n_done + 1;
        end
    end

    // ---------------- [HS] 내부 handshake ----------------
    hs_mon #(.W(17)) u_hs_p2 (.clk(clk), .rst_n(rst_n), .valid(dut.l2_pool_valid), .ready(dut.l2_pool_ready),
                             .data({dut.l2_pool_ch_done, dut.l2_pool_data}));
    hs_mon #(.W(16)) u_hs_f1 (.clk(clk), .rst_n(rst_n), .valid(dut.U_FC.l1_out_valid), .ready(dut.U_FC.l2_in_ready),
                             .data(dut.U_FC.l1_out_data));
    hs_mon #(.W(16)) u_hs_f2 (.clk(clk), .rst_n(rst_n), .valid(dut.U_FC.l2_out_valid), .ready(dut.U_FC.l3_in_ready),
                             .data(dut.U_FC.l2_out_data));

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
                if (i >= ent_count(stage) || neq17(ent_val(stage, 0, i, ch), ent_val(stage, 1, i, ch))) bad = bad + 1;
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
                    else if (neq16(x[15:0], g[15:0])) $fwrite(fd, "*%6d", $signed(x[15:0]));
                    else if (neq8(x[16], g[16])) $fwrite(fd, "!%6d", $signed(x[15:0]));
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
                    end else if (neq17(x, g)) begin
                        listed = listed + 1;
                        $fdisplay(fd, "     r%2d c%2d : rtl %6d done=%b  exp %6d done=%b", e / W, e % W,
                                  $signed(x[15:0]), x[16], $signed(g[15:0]), g[16]);
                    end
                end
            end
        end
    endtask

    // FC 벡터 표 : stage 5 = FC1, 6 = FC2, 7 = logit. 한 줄에 12 개, '*' = 골든과 다름
    task print_vec;
        input integer fd, stage, f;
        integer n, k, e, cnt, bad;
        reg [15:0] x, g;
        begin
            n   = (stage == 5) ? N_F1 : (stage == 6) ? N_F2 : N_LG;
            cnt = (stage == 5) ? n_f1 : (stage == 6) ? n_f2 : n_lg;
            bad = 0;
            for (k = 0; k < n; k = k + 1) begin
                e = f * n + k;
                x = (stage == 5) ? got_f1[e] : (stage == 6) ? got_f2[e] : got_lg[e];
                g = (stage == 5) ? gold_f1[e] : (stage == 6) ? gold_f2[e] : gold_lg[e];
                if (e >= cnt || neq16(x, g)) bad = bad + 1;
            end
            $fdisplay(fd, "\n[%0s frame %0d, %0d]  %0s", (stage == 5) ? "F1 FC1 out" : (stage == 6) ? "F2 FC2 out" : "LG logit (FC3 out)",
                      f, n, (bad == 0) ? "OK" : "MISMATCH");
            for (k = 0; k < n; k = k + 1) begin
                if (k % 12 == 0) $fwrite(fd, " %3d | ", k);
                e = f * n + k;
                x = (stage == 5) ? got_f1[e] : (stage == 6) ? got_f2[e] : got_lg[e];
                g = (stage == 5) ? gold_f1[e] : (stage == 6) ? gold_f2[e] : gold_lg[e];
                if (e >= cnt) $fwrite(fd, "   ----");
                else if (neq16(x, g)) $fwrite(fd, "*%6d", $signed(x));
                else $fwrite(fd, " %6d", $signed(x));
                if (k % 12 == 11 || k == n - 1) $fwrite(fd, "\n");
            end
            if (bad != 0) begin
                $fdisplay(fd, "   mismatch %0d 개:", bad);
                for (k = 0; k < n; k = k + 1) begin
                    e = f * n + k;
                    x = (stage == 5) ? got_f1[e] : (stage == 6) ? got_f2[e] : got_lg[e];
                    g = (stage == 5) ? gold_f1[e] : (stage == 6) ? gold_f2[e] : gold_lg[e];
                    if (e >= cnt) $fdisplay(fd, "     %3d : rtl   ----  exp %6d", k, $signed(g));
                    else if (neq16(x, g)) $fdisplay(fd, "     %3d : rtl %6d  exp %6d", k, $signed(x), $signed(g));
                end
            end
        end
    endtask

    function [7:0] class_char;      // 0..9 -> '0'..'9', 10..35 -> 'A'..'Z'
        input [7:0] k;
        class_char = (k < 10) ? (8'd48 + k) : (8'd55 + k);
    endfunction

    task write_report;
        integer fd, f, ch, r, c;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "cnn_top (conv_l1 -> pool_l1 -> conv_l2 -> pool_l2 -> fc_top -> argmax) test report  (values in signed decimal)");
            $fdisplay(fd, "  VALID_PCT=%0d GATE=%0d BIAS_FIX=%0d SEED=%0d FRAMES=%0d", VALID_PCT, GATE, BIAS_FIX, SEED, FRAMES);
            $fdisplay(fd, "  %0d cycles, pixel %0d / %0d, pool_l2 -> fc_top stall %0d cycles", cyc, pix, FRAMES * N_PIX, stall_p2);
            $fdisplay(fd, "  [C1]  conv_l1 out : %4d / %4d entries, %0d wrong   (ce1_out.mem)", n_c1, FRAMES * N_C1, err_c1);
            $fdisplay(fd, "  [P1]  pool_l1 out : %4d / %4d entries, %0d wrong   (pool1_out.mem)", n_p1, FRAMES * N_P1, err_p1);
            $fdisplay(fd, "  [ROM] conv_l2 rom : %0d / %0d cal_valid cycles wrong", rom_errs, rom_checks);
            $fdisplay(fd, "  [C2]  conv_l2 out : %4d / %4d entries, %0d wrong   (ce2_out.mem)", n_c2, FRAMES * N_C2, err_c2);
            $fdisplay(fd, "  [P2]  pool_l2 out : %4d / %4d entries, %0d wrong   (pool2_out.mem)", n_p2, FRAMES * N_P2, err_p2);
            $fdisplay(fd, "  [F1]  FC1 out     : %4d / %4d values,  %0d wrong   (fc1_out.mem)", n_f1, FRAMES * N_F1, err_f1);
            $fdisplay(fd, "  [F2]  FC2 out     : %4d / %4d values,  %0d wrong   (fc2_out.mem)", n_f2, FRAMES * N_F2, err_f2);
            $fdisplay(fd, "  [LG]  logit       : %4d / %4d values,  %0d wrong   (logit_out.mem)", n_lg, FRAMES * N_LG, err_lg);
            $fdisplay(fd, "  [AM]  argmax      : %4d / %4d cnn_done, %0d wrong  (class_out.mem)", n_done, FRAMES, err_am);
            $fdisplay(fd, "  [HS]  handshake   : pool_l2->fc %0d, FC1->FC2 %0d, FC2->FC3 %0d violations",
                      u_hs_p2.errs, u_hs_f1.errs, u_hs_f2.errs);
            $fdisplay(fd, "  [OVR] dropped push: conv_l1 %0d, conv_l2 %0d", ovr_l1, ovr_l2);
            if (!mem_ok) $fdisplay(fd, "  [MEM] golden .mem not loaded - result is FAIL");
            $fdisplay(fd, "  RESULT : %0s", pass_all(0) ? "PASS" : "FAIL");
            for (f = 0; f < FRAMES && f < n_done; f = f + 1)
                $fdisplay(fd, "  frame %0d : cnn_done at cycle %0d, cnn_result = %0d ('%c'), expected %0d ('%c')", f,
                          done_cyc[f], got_cls[f], class_char(got_cls[f]), gold_cls[f], class_char(gold_cls[f]));
            $fdisplay(fd, "");
            $fdisplay(fd, "  값 = TB 가 각 단계 valid & ready 에서 잡은 RTL 출력");
            $fdisplay(fd, "  '*' = 골든과 값이 다름, '!' = 값은 같고 ch_done 만 다름, ---- = 출력 안 나옴");
            $fdisplay(fd, "  각 값의 sim time / hex 는 %s 참고", TRACE_FILE);

            for (f = 0; f < FRAMES; f = f + 1) begin
                $fdisplay(fd, "\n##################################################################");
                $fdisplay(fd, " FRAME %0d", f);
                $fdisplay(fd, "##################################################################");
                if (f < n_done)
                    $fdisplay(fd, "\n[AM argmax frame %0d]  cnn_result = %0d ('%c'), expected %0d ('%c')  %0s", f, got_cls[f],
                              class_char(got_cls[f]), gold_cls[f], class_char(gold_cls[f]),
                              !neq8(got_cls[f], gold_cls[f]) ? "OK" : "MISMATCH");
                else
                    $fdisplay(fd, "\n[AM argmax frame %0d]  cnn_done 안 나옴", f);
                print_vec(fd, 7, f);
                $fwrite(fd, "  class  ");
                for (c = 0; c < N_LG; c = c + 1) $fwrite(fd, "%c", class_char(c));
                $fwrite(fd, "\n");
                print_vec(fd, 6, f);
                print_vec(fd, 5, f);
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
        pass_all = (mem_ok && err_c1 == 0 && err_p1 == 0 && err_c2 == 0 && err_p2 == 0 &&
                    err_f1 == 0 && err_f2 == 0 && err_lg == 0 && err_am == 0 &&
                    u_hs_p2.errs == 0 && u_hs_f1.errs == 0 && u_hs_f2.errs == 0 &&
                    rom_errs == 0 && ovr_l1 == 0 && ovr_l2 == 0 &&
                    n_c1 == FRAMES * N_C1 && n_p1 == FRAMES * N_P1 && n_c2 == FRAMES * N_C2 &&
                    n_p2 == FRAMES * N_P2 && n_f1 == FRAMES * N_F1 && n_f2 == FRAMES * N_F2 &&
                    n_lg == FRAMES * N_LG && n_done == FRAMES);
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
    integer f;

    initial begin
        $timeformat(-9, 0, " ns", 10);
        clk        = 1'b0;
        rst_n      = 1'b0;
        running    = 0;
        seed       = SEED;
        cyc        = 0;
        img_ok     = 0;
        am_expect  = 1'b0;
        n_c1 = 0; n_p1 = 0; n_c2 = 0; n_p2 = 0; n_f1 = 0; n_f2 = 0; n_lg = 0; n_done = 0;
        err_c1 = 0; err_p1 = 0; err_c2 = 0; err_p2 = 0; err_f1 = 0; err_f2 = 0; err_lg = 0; err_am = 0;
        shown_c1 = 0; shown_p1 = 0; shown_c2 = 0; shown_p2 = 0; shown_f1 = 0; shown_f2 = 0; shown_lg = 0; shown_am = 0;
        stall_p2 = 0;
        rom_checks = 0; rom_errs = 0; rom_shown = 0;
        ovr_l1 = 0; ovr_l2 = 0;
        t_c1_last = 0; t_c2_last = 0; t_p2_last = 0; t_lg_last = 0;
        for (i = 0; i < FRAMES * N_C1; i = i + 1) got_c1[i] = 49'bx;
        for (i = 0; i < FRAMES * N_P1; i = i + 1) got_p1[i] = 49'bx;
        for (i = 0; i < FRAMES * N_C2; i = i + 1) got_c2[i] = 17'bx;
        for (i = 0; i < FRAMES * N_P2; i = i + 1) got_p2[i] = 17'bx;
        for (i = 0; i < FRAMES * N_F1; i = i + 1) got_f1[i] = 16'bx;
        for (i = 0; i < FRAMES * N_F2; i = i + 1) got_f2[i] = 16'bx;
        for (i = 0; i < FRAMES * N_LG; i = i + 1) got_lg[i] = 16'bx;
        for (i = 0; i < FRAMES; i = i + 1) begin got_cls[i] = 8'bx; done_cyc[i] = 0; end

        $readmemh("ce1_stim.mem", stim);
        $readmemh("ce1_out.mem", gold_c1);
        $readmemh("pool1_out.mem", gold_p1);
        $readmemh("ce2_out.mem", gold_c2);
        $readmemh("pool2_out.mem", gold_p2);
        $readmemh("fc1_out.mem", gold_f1);
        $readmemh("fc2_out.mem", gold_f2);
        $readmemh("logit_out.mem", gold_lg);
        $readmemh("class_out.mem", gold_cls);
        $readmemh("conv2_weight.mem", wref);
        mem_ok = !(^stim[0] === 1'bx || ^gold_c1[0] === 1'bx || ^gold_p1[0] === 1'bx || ^gold_c2[0] === 1'bx ||
                   ^gold_p2[0] === 1'bx || ^gold_f1[0] === 1'bx || ^gold_f2[0] === 1'bx || ^gold_lg[0] === 1'bx ||
                   ^gold_cls[0] === 1'bx || ^wref[0] === 1'bx);
        if (!mem_ok) begin
            $display("[FAIL] golden .mem not loaded - run from build/ via run_sim.sh (or add the .mem files to the sim sources)");
            $display("       TB : ce1_stim ce1_out pool1_out ce2_out pool2_out conv2_weight (rtl/cnn/rtl_ref),");
            $display("            fc1_out fc2_out logit_out class_out (tb/cnn/cnn_top/vectors)");
            $display("       RTL: fc1..3_weight fc1..3_bias l2_weight_ch00..15 (rtl/cnn/mem), conv bias (see conv_out_stage.v)");
            $finish;
        end

        fd_trace = $fopen(TRACE_FILE, "w");
        $fdisplay(fd_trace, "cnn_top handshake trace  (time = posedge 시각, 파형과 동일, 값은 signed 10진수)");
        $fdisplay(fd_trace, "      time   cycle stage  idx  position            value");

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;

        // (W-bias) 두 conv 의 bias ROM 을 골든과 짝이 맞는 값으로 덮어쓴다
        if (BIAS_FIX) begin
            $readmemh("conv1_bias_ce.mem", dut.U_CONV_L1.U_OUTPUT_STAGE_L1.GEN_CONV1.u_output_buffer.u_bias_rom.mem);
            $readmemh("conv2_bias_ce.mem", dut.U_CONV_L2.U_OUTPUT_STAGE_L1.GEN_CONV2.u_output_buffer.u_bias_rom.mem);
        end
        $display("cnn_top TB: %0d frame, VALID_PCT=%0d GATE=%0d BIAS_FIX=%0d SEED=%0d",
                 FRAMES, VALID_PCT, GATE, BIAS_FIX, SEED);

        @(negedge clk);
        running = 1;

        while (n_done < FRAMES && cyc < MAX_CYCLES) @(posedge clk);
        repeat (200) @(posedge clk);  // 초과 출력 / 추가 cnn_done 이 나오는지 조금 더 본다
        running = 0;

        // ---------------- summary ----------------
        $display("\nsummary (%0d cycles, last output: conv_l1 cyc %0d, conv_l2 cyc %0d, pool_l2 cyc %0d, logit cyc %0d, pool_l2->fc stall %0d cycles):",
                 cyc, t_c1_last, t_c2_last, t_p2_last, t_lg_last, stall_p2);
        stage_summary("[C1]  conv_l1 vs ce1_out    ", n_c1, FRAMES * N_C1, err_c1);
        stage_summary("[P1]  pool_l1 vs pool1_out  ", n_p1, FRAMES * N_P1, err_p1);
        if (rom_errs == 0)
            $display("  [ROM] conv_l2 weight ROM     : correct on all %0d cal_valid cycles", rom_checks);
        else
            $display("  [ROM] conv_l2 weight ROM     : %0d of %0d cal_valid cycles wrong", rom_errs, rom_checks);
        stage_summary("[C2]  conv_l2 vs ce2_out    ", n_c2, FRAMES * N_C2, err_c2);
        stage_summary("[P2]  pool_l2 vs pool2_out  ", n_p2, FRAMES * N_P2, err_p2);
        stage_summary("[F1]  FC1     vs fc1_out    ", n_f1, FRAMES * N_F1, err_f1);
        stage_summary("[F2]  FC2     vs fc2_out    ", n_f2, FRAMES * N_F2, err_f2);
        stage_summary("[LG]  logit   vs logit_out  ", n_lg, FRAMES * N_LG, err_lg);
        stage_summary("[AM]  argmax  vs class_out  ", n_done, FRAMES, err_am);
        for (f = 0; f < FRAMES && f < n_done; f = f + 1)
            $display("        frame %0d : cnn_done at cycle %0d -> cnn_result = %0d ('%c'), expected %0d ('%c')", f,
                     done_cyc[f], got_cls[f], class_char(got_cls[f]), gold_cls[f], class_char(gold_cls[f]));
        $display("  [HS]  handshake violations   : pool_l2->fc %0d, FC1->FC2 %0d, FC2->FC3 %0d",
                 u_hs_p2.errs, u_hs_f1.errs, u_hs_f2.errs);
        $display("  [OVR] dropped push           : conv_l1 %0d, conv_l2 %0d", ovr_l1, ovr_l2);
        if (!mem_ok) $display("  [MEM] golden .mem not loaded - result is FAIL");

        write_report;
        $fclose(fd_trace);
        $display("\nwrote %s, %s", REPORT_FILE, TRACE_FILE);

        if (pass_all(0)) $display("\n[PASS] cnn_top: %0d frames, %0d cycles.", FRAMES, cyc);
        else $display("\n[FAIL] cnn_top - see %s", REPORT_FILE);
        $finish;
    end

endmodule

// valid 가 뜬 뒤 ready 로 받기 전까지 valid 와 data 가 그대로여야 한다 (AXI-Stream 규칙)
module hs_mon #(
    parameter W = 16
) (
    input         clk,
    input         rst_n,
    input         valid,
    input         ready,
    input [W-1:0] data
);
    integer errs = 0;
    reg         hold = 1'b0;
    reg [W-1:0] held;

    always @(posedge clk) begin
        if (rst_n && hold && !(valid && data === held)) begin
            if (errs < 10)
                $display("[FAIL][HS] %m %t: valid=%b data=%h, but %h was not taken yet", $realtime, valid, data, held);
            errs = errs + 1;
        end
        hold = rst_n && valid && !ready;
        held = data;
    end
endmodule
