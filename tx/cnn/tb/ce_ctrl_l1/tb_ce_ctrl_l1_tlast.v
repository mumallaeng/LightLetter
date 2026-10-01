`timescale 1ns / 1ps

// ce_ctrl_l1 : 행마다 오는 s_axis_tlast 로 프레임 끝 (ch_count) 을 찾는지 conv_l1 단위로 확인한다.
//
// 실행: 이 폴더에서  ./run_sim.sh   (옵션은 run_sim.sh 참고)
//
// 입력 (AXI4-Stream video 규칙)
//   28x28 raster, tuser = 프레임 첫 pixel (r0 c0), tlast = 행 마지막 pixel (c27) -> 프레임마다 tlast 28 번
//   valid 가 올라간 beat 는 받을 때까지 tdata / tuser / tlast 를 그대로 유지한다.
//   IDLE_JUNK = 1 이면 tvalid = 0 인 클럭의 tdata / tuser / tlast 는 랜덤 (AXIS 에서 의미 없는 값).
//   frame f 의 stimulus = tx/cnn/rtl/rtl_ref/ce1_stim.mem 의 frame (f % 2), 기대 출력 = ce1_out.mem 의 같은 frame.
//   out_reorder 는 한 프레임만 담으므로 다음 프레임은 앞 프레임 출력 1352 개를 다 받은 뒤 넣는다.
//
// 확인하는 것
//   [TL]  TB 가 보낸 tlast (handshake 된 것) 가 프레임마다 28 번, 모두 c27 인지 (stimulus 자체 검사)
//   [CH]  ce_ctrl_l1.ch_count 가 기준 모델과 매 클럭 같은지
//           기준: 프레임 마지막 pixel (r27 c27, 28 번째 tlast) 이 handshake 되는 클럭에 set -> 다음 클럭 1,
//                 c_state == STOP 에서 clear. 그 외에는 바뀌지 않는다.
//   [WIN] 프레임마다 mac_start 676 번 (26x26 window)
//   [PC]  phase_clear 가 프레임마다 한 번, 그 프레임 676 번째 window 의 mac_done 뒤에 온다
//   [OUT] conv_l1 출력 (out_valid & out_ready) == ce1_out.mem  {ch_done, d2, d1, d0}
//   [TO]  프레임이 FRAME_TIMEOUT cycle 안에 끝나지 않으면 (phase_clear 가 안 오면) FAIL
//
// 결과 파일: REPORT_FILE (요약 + 프레임별 표), TRACE_FILE (tlast / ch_count / phase_clear 이벤트의 cycle)

module tb_ce_ctrl_l1_tlast;

    parameter REPORT_FILE   = "tb_ce_ctrl_l1_tlast_report.txt";
    parameter TRACE_FILE    = "tb_ce_ctrl_l1_tlast_trace.txt";
    parameter FRAMES        = 3;
    parameter VALID_PCT     = 100;  // s_axis_tvalid 을 올릴 확률
    parameter READY_PCT     = 100;  // out_ready 를 올릴 확률
    parameter IDLE_JUNK     = 1;    // tvalid = 0 일 때 tlast / tuser / tdata 를 랜덤으로
    parameter SEED          = 1;
    parameter FRAME_TIMEOUT = 40000;
    parameter MAX_REPORT    = 10;

    localparam IMG_W   = 28;
    localparam N_PIX   = IMG_W * IMG_W;          // 784
    localparam N_WIN   = (IMG_W - 2) * (IMG_W - 2);  // 676
    localparam N_C1    = N_WIN * 6 / 3;          // 1352 entry / frame
    localparam [1:0] STOP = 2'd3;

    // ---------------- DUT ----------------
    reg         clk, rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;
    wire        out_valid, out_ch_done;
    reg         out_ready;
    wire [15:0] out_data0, out_data1, out_data2;

    conv_l1 dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .out_valid    (out_valid),
        .out_ready    (out_ready),
        .out_ch_done  (out_ch_done),
        .out_data0    (out_data0),
        .out_data1    (out_data1),
        .out_data2    (out_data2)
    );

    always #5 clk = ~clk;

    wire       pixel_valid = dut.pixel_valid;
    wire       phase_clear = dut.phase_clear;
    wire       mac_start   = dut.mac_start;
    wire       mac_done    = dut.mac_done;
    wire [1:0] c_state     = dut.U_CE_CONTROLLER_L1.c_state;
    wire       ch_count    = dut.U_CE_CONTROLLER_L1.ch_count;

    // ---------------- vectors ----------------
    reg [15:0] stim [0:2*N_PIX-1];
    reg [48:0] gold [0:2*N_C1-1];

    // ---------------- state ----------------
    integer cyc, seed, fd_trace, i;
    integer pix;                         // 지금 버스에 올린 (또는 다음에 올릴) beat 번호
    integer n_out, img_ok;
    integer err_tl, err_ch, err_win, err_pc, err_out, err_to;
    integer shown_ch, shown_out;
    integer tl_cnt   [0:FRAMES-1];       // handshake 된 tlast 수
    integer tl_bad   [0:FRAMES-1];       // c27 이 아닌 곳의 tlast
    integer win_cnt  [0:FRAMES-1];       // mac_start 수
    integer pc_cnt   [0:FRAMES-1];       // phase_clear 수
    integer out_bad  [0:FRAMES-1];
    integer last_cyc [0:FRAMES-1];       // 프레임 마지막 pixel handshake cycle
    integer ch_cyc   [0:FRAMES-1];       // ch_count 가 1 이 된 cycle
    integer pc_cyc   [0:FRAMES-1];       // phase_clear cycle
    integer f_in, f_win, f_start;        // 입력 중인 프레임 / window 를 세는 프레임 / 그 프레임 시작 cycle
    reg     running, ch_ref;
    reg [31:0] roll;

    // ---------------- AXIS source ----------------
    // pix = 버스에 올려 둔 beat. handshake 되면 다음 beat 로 넘어간다.
    integer nxt, nf, nr, nc;
    reg     go;
    wire    fire = s_axis_tvalid & s_axis_tready;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pix           <= 0;
            s_axis_tvalid <= 1'b0;
            s_axis_tdata  <= 16'd0;
            s_axis_tuser  <= 1'b0;
            s_axis_tlast  <= 1'b0;
        end else begin
            nxt = pix + (fire ? 1 : 0);
            pix <= nxt;
            if (!s_axis_tvalid || s_axis_tready) begin
                roll = $unsigned($random(seed)) % 100;
                go   = running && (nxt < FRAMES * N_PIX) && (nxt / N_PIX <= img_ok) && (roll < VALID_PCT);
                nf = nxt / N_PIX; nr = (nxt % N_PIX) / IMG_W; nc = nxt % IMG_W;
                s_axis_tvalid <= go;
                if (go) begin
                    s_axis_tdata <= stim[(nf % 2) * N_PIX + nr * IMG_W + nc];
                    s_axis_tuser <= (nr == 0) && (nc == 0);
                    s_axis_tlast <= (nc == IMG_W - 1);
                end else if (IDLE_JUNK) begin
                    s_axis_tdata <= $random(seed);
                    s_axis_tuser <= $random(seed);
                    s_axis_tlast <= $random(seed);
                end else begin
                    s_axis_tdata <= 16'd0;
                    s_axis_tuser <= 1'b0;
                    s_axis_tlast <= 1'b0;
                end
            end
        end
    end

    // 출력 소비자 : out_ready 를 확률로 흔든다
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) out_ready <= 1'b0;
        else        out_ready <= running && (($unsigned($random(seed)) % 100) < READY_PCT);
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // 다음 프레임은 앞 프레임 출력을 다 받은 뒤
    always @(posedge clk) img_ok <= n_out / N_C1;

    // ---------------- [TL] / [CH] ----------------
    // 기준 ch_count : 프레임 마지막 pixel handshake 에서 set, STOP 에서 clear (RTL 과 같은 우선순위)
    integer pf, pr, pc;
    always @(posedge clk) begin
        if (rst_n) begin
            // posedge 직전 값으로 비교 (RTL 레지스터와 같은 시점)
            if (ch_count !== ch_ref) begin
                err_ch = err_ch + 1;
                if (shown_ch < MAX_REPORT) begin
                    shown_ch = shown_ch + 1;
                    $display("[FAIL][CH] cycle %0d : ch_count %b, expected %b (state %0d, input frame %0d)",
                             cyc, ch_count, ch_ref, c_state, f_in);
                end
            end
            if (ch_count === 1'b1 && ch_cyc[f_win < FRAMES ? f_win : FRAMES-1] < 0)
                ch_cyc[f_win < FRAMES ? f_win : FRAMES-1] = cyc;

            if (c_state == STOP)
                ch_ref <= 1'b0;
            else if (pixel_valid && s_axis_tvalid && pix % N_PIX == N_PIX - 1)
                ch_ref <= 1'b1;

            if (pixel_valid) begin
                pf = pix / N_PIX; pr = (pix % N_PIX) / IMG_W; pc = pix % IMG_W;
                f_in = pf;
                if (pr == 0 && pc == 0) $fdisplay(fd_trace, "%8d  f%0d  first pixel (tuser)", cyc, pf);
                if (s_axis_tlast) begin
                    if (pf < FRAMES) tl_cnt[pf] = tl_cnt[pf] + 1;
                    if (pc != IMG_W - 1 && pf < FRAMES) tl_bad[pf] = tl_bad[pf] + 1;
                    $fdisplay(fd_trace, "%8d  f%0d  tlast r%2d c%2d  (#%0d)", cyc, pf, pr, pc,
                              pf < FRAMES ? tl_cnt[pf] : -1);
                end
                if (pr == IMG_W - 1 && pc == IMG_W - 1 && pf < FRAMES) begin
                    last_cyc[pf] = cyc;
                    $fdisplay(fd_trace, "%8d  f%0d  last pixel accepted", cyc, pf);
                end
            end
        end
    end

    always @(posedge clk) begin
        if (rst_n && ch_count !== dut.U_CE_CONTROLLER_L1.ch_count_next)
            $fdisplay(fd_trace, "%8d        ch_count %b -> %b  (state %0d)", cyc, ch_count,
                      dut.U_CE_CONTROLLER_L1.ch_count_next, c_state);
    end

    // ---------------- [WIN] / [PC] / [TO] ----------------
    always @(posedge clk) begin
        if (rst_n && running && f_win < FRAMES) begin
            if (mac_start) win_cnt[f_win] = win_cnt[f_win] + 1;
            if (phase_clear) begin
                pc_cnt[f_win] = pc_cnt[f_win] + 1;
                pc_cyc[f_win] = cyc;
                $fdisplay(fd_trace, "%8d  f%0d  phase_clear  (windows %0d)", cyc, f_win, win_cnt[f_win]);
                f_win   = f_win + 1;
                f_start = cyc;
            end else if (cyc - f_start > FRAME_TIMEOUT) begin
                err_to = err_to + 1;
                $display("[FAIL][TO] frame %0d : no phase_clear in %0d cycles (windows %0d, ch_count %b, state %0d, pixels in %0d)",
                         f_win, FRAME_TIMEOUT, win_cnt[f_win], ch_count, c_state, pix - f_win * N_PIX);
                running = 0;
            end
        end
    end

    // ---------------- [OUT] ----------------
    wire [48:0] out_word = {out_ch_done, out_data2, out_data1, out_data0};
    integer of, oe;
    always @(posedge clk) begin
        if (rst_n && out_valid && out_ready) begin
            of = n_out / N_C1; oe = (of % 2) * N_C1 + n_out % N_C1;
            if (of >= FRAMES || out_word !== gold[oe] || ^out_word === 1'bx) begin
                err_out = err_out + 1;
                if (of < FRAMES) out_bad[of] = out_bad[of] + 1;
                if (shown_out < MAX_REPORT) begin
                    shown_out = shown_out + 1;
                    $display("[FAIL][OUT] entry %0d (frame %0d) : got %0d %0d %0d done=%b / exp %0d %0d %0d done=%b",
                             n_out, of, $signed(out_word[15:0]), $signed(out_word[31:16]), $signed(out_word[47:32]),
                             out_word[48], $signed(gold[oe][15:0]), $signed(gold[oe][31:16]),
                             $signed(gold[oe][47:32]), gold[oe][48]);
                end
            end
            n_out = n_out + 1;
        end
    end

    // ---------------- report ----------------
    task write_report;
        integer fd, f;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "ce_ctrl_l1 row-tlast test  (FRAMES %0d, VALID_PCT %0d, READY_PCT %0d, IDLE_JUNK %0d, SEED %0d)",
                      FRAMES, VALID_PCT, READY_PCT, IDLE_JUNK, SEED);
            $fdisplay(fd, "");
            $fdisplay(fd, "  frame  tlast(bad)  windows  phase_clear  last_pixel_cyc  ch_count=1_cyc  phase_clear_cyc  out_mismatch");
            for (f = 0; f < FRAMES; f = f + 1)
                $fdisplay(fd, "  %5d  %5d(%0d)  %7d  %11d  %14d  %14d  %15d  %12d", f, tl_cnt[f], tl_bad[f],
                          win_cnt[f], pc_cnt[f], last_cyc[f], ch_cyc[f], pc_cyc[f], out_bad[f]);
            $fdisplay(fd, "  (expected: tlast 28(0), windows %0d, phase_clear 1, ch_count=1 one cycle after last pixel)", N_WIN);
            $fdisplay(fd, "");
            $fdisplay(fd, "  [TL] %0d  [CH] %0d  [WIN] %0d  [PC] %0d  [OUT] %0d (%0d / %0d entries)  [TO] %0d",
                      err_tl, err_ch, err_win, err_pc, err_out, n_out, FRAMES * N_C1, err_to);
            $fclose(fd);
        end
    endtask

    // ---------------- main ----------------
    integer f;
    initial begin
        clk = 0; rst_n = 0; running = 0; ch_ref = 0;
        cyc = 0; seed = SEED; n_out = 0; img_ok = 0;
        err_tl = 0; err_ch = 0; err_win = 0; err_pc = 0; err_out = 0; err_to = 0;
        shown_ch = 0; shown_out = 0;
        f_in = 0; f_win = 0; f_start = 0;
        for (f = 0; f < FRAMES; f = f + 1) begin
            tl_cnt[f] = 0; tl_bad[f] = 0; win_cnt[f] = 0; pc_cnt[f] = 0; out_bad[f] = 0;
            last_cyc[f] = -1; ch_cyc[f] = -1; pc_cyc[f] = -1;
        end

        $readmemh("ce1_stim.mem", stim);
        $readmemh("ce1_out.mem", gold);
        if (^stim[0] === 1'bx || ^gold[0] === 1'bx) begin
            $display("[FAIL] ce1_stim.mem / ce1_out.mem not loaded (tx/cnn/rtl/rtl_ref) - run via run_sim.sh");
            $finish;
        end
        fd_trace = $fopen(TRACE_FILE, "w");
        $fdisplay(fd_trace, "   cycle  event");

        repeat (5) @(posedge clk);
        rst_n = 1;
        @(posedge clk);
        running = 1;

        while (running && (f_win < FRAMES || n_out < FRAMES * N_C1)) @(posedge clk);
        repeat (50) @(posedge clk);
        running = 0;

        // 프레임별 판정
        for (f = 0; f < FRAMES; f = f + 1) begin
            if (tl_cnt[f] != IMG_W || tl_bad[f] != 0) begin
                err_tl = err_tl + 1;
                $display("[FAIL][TL] frame %0d : %0d tlast handshakes (%0d not at c27), expected 28", f, tl_cnt[f], tl_bad[f]);
            end
            if (win_cnt[f] != N_WIN) begin
                err_win = err_win + 1;
                $display("[FAIL][WIN] frame %0d : %0d windows, expected %0d", f, win_cnt[f], N_WIN);
            end
            if (pc_cnt[f] != 1) begin
                err_pc = err_pc + 1;
                $display("[FAIL][PC] frame %0d : phase_clear %0d times, expected 1", f, pc_cnt[f]);
            end
        end
        if (n_out != FRAMES * N_C1) err_out = err_out + 1;

        write_report;
        $fclose(fd_trace);

        $display("");
        $display("ce_ctrl_l1 row-tlast : FRAMES %0d, VALID_PCT %0d, READY_PCT %0d, IDLE_JUNK %0d", FRAMES, VALID_PCT, READY_PCT, IDLE_JUNK);
        for (f = 0; f < FRAMES; f = f + 1)
            $display("  frame %0d : tlast %0d, windows %0d, phase_clear %0d, last pixel @%0d, ch_count=1 @%0d, phase_clear @%0d",
                     f, tl_cnt[f], win_cnt[f], pc_cnt[f], last_cyc[f], ch_cyc[f], pc_cyc[f]);
        $display("  output entries %0d / %0d, mismatch %0d", n_out, FRAMES * N_C1, err_out);
        if (err_tl + err_ch + err_win + err_pc + err_out + err_to == 0)
            $display("[PASS] ce_ctrl_l1 row-tlast");
        else
            $display("[FAIL] ce_ctrl_l1 row-tlast : TL %0d CH %0d WIN %0d PC %0d OUT %0d TO %0d",
                     err_tl, err_ch, err_win, err_pc, err_out, err_to);
        $finish;
    end

endmodule
