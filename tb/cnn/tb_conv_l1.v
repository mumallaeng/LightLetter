`timescale 1ns / 1ps

// Full-path testbench for conv_l1 (28x28x1 -> 26x26x6), checked against the C golden model.
//
// Vivado XSim (run from tb/cnn):
//   xvlog ../../rtl/cnn/conv_l1.v ../../rtl/cnn/ce_ctrl_l1.v ../../rtl/cnn/weight_addr_ctrl_l1.v \
//         ../../rtl/cnn/weight_rom_l1.v ../../rtl/cnn/line_buffer.v ../../rtl/cnn/mac_array_l1.v \
//         ../../rtl/cnn/MAC_unit.v ../../rtl/cnn/conv_out_stage.v ../../rtl/cnn/output_buffer.v \
//         ../../rtl/cnn/partial_sum.v ../../rtl/cnn/buffer_ctrl.v ../../rtl/cnn/bias_rom.v \
//         ../../rtl/cnn/relu_quant.v ../../rtl/cnn/quantizer.v ../../rtl/cnn/relu.v \
//         ../../rtl/cnn/lane_packer.v ../../rtl/cnn/out_reorder.v tb_conv_l1.v
//   xelab tb_conv_l1 -s tb_conv_l1_sim
//   xsim tb_conv_l1_sim -runall
//
// Icarus:
//   iverilog -g2005 -o tb_conv_l1.vvp tb_conv_l1.v ../../rtl/cnn/*.v && vvp -n tb_conv_l1.vvp
//
// ---------------------------------------------------------------------------
// 이 테스트벤치는 conv_l1.v 를 고치지 않고 돌리기 위해 아래 3가지를 우회한다.
// 전부 initial 블록에 모여 있고, RTL 이 고쳐지면 그냥 지우면 된다.
//
//   (W1) conv_l1.v 의 out_ready 가 output 으로 선언돼 있어 구동원이 없다 (z).
//        -> force 로 1 을 걸어 항상 backpressure 없음으로 만든다.
//   (W2) conv_out_stage 의 ch_result1 / ch_result2 입력이 미연결이다 (z).
//        -> conv1 은 C_IN = 1 이므로 force 로 0 을 건다.
//   (W3) weight_rom_l1 이 $readmemh 하는 conv1_weight.mem 은 conv_l2 겸용 포맷
//        (12줄 x 432bit) 이라 6줄 x 144bit ROM 에 안 맞는다.
//        -> TB 가 conv1_weight_144.mem 으로 ROM 을 다시 채운다.
//        bias 도 현재 weight 와 짝이 맞는 conv1_bias_ce.mem 으로 덮어쓴다.
//
// 확인하는 것:
//   [A] weight ROM 타이밍 - cal_valid 인 매 사이클마다 weight_out 이
//       rom[out_ch_sel] 과 같은지. weight_rom_l1 이 sync read 라 한 박자 밀리면
//       여기서 잡힌다. ROM entry 별 사용 횟수도 세서 히스토그램으로 보여준다.
//       (정상이면 6채널이 676번씩 고르게, 밀리면 och0 두 배 / och5 0 번)
//   [B] 최종 출력 - out_data0..2 / out_ch_done 을 C 골든모델 ce1_out.mem 과 비교.
// ---------------------------------------------------------------------------

module tb_conv_l1;

    // ---------------- parameters ----------------
    parameter STIM_FILE   = "../../rtl/cnn/rtl_ref/ce1_stim.mem";        // 16bit  x 1568 (2 frame)
    parameter GOLD_FILE   = "../../rtl/cnn/rtl_ref/ce1_out.mem";         // 49bit  x 2704 (2 frame)
    parameter WEIGHT_FILE = "../../rtl/cnn/rtl_ref/conv1_weight_144.mem";// 144bit x 6
    parameter BIAS_FILE   = "../../rtl/cnn/rtl_ref/conv1_bias_ce.mem";   // 32bit  x 6

    parameter OCH        = 6;
    parameter N_PIX      = 784;      // 28 x 28, 1 frame
    parameter N_WIN      = 676;      // 26 x 26
    parameter N_OUT      = 1352;     // N_WIN * OCH / PACK(3)
    parameter MAX_CYCLES = 500000;
    parameter LOG_FIRST  = 10;       // 첫 MAC burst 를 몇 사이클 찍을지
    parameter MAX_REPORT = 10;       // 출력 불일치 최대 보고 개수

    // ---------------- dut ports ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;

    wire        out_valid;
    wire        out_ready;
    wire        out_ch_done;
    wire [15:0] out_data0, out_data1, out_data2;

    conv_l1 #(.OCH(OCH)) dut (
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

    // ---------------- data ----------------
    reg [15:0] stim[0:2*N_PIX-1];
    reg [48:0] gold[0:2*N_OUT-1];

    integer pix;                 // 보낸 픽셀 수
    integer o;                   // 받은 출력 entry 수
    integer out_errs, out_shown;
    integer slip_checks, slip_errs, slip_shown;
    integer rom_used[0:OCH-1];
    integer burst_logged;
    integer cyc;
    integer i;
    integer running;

    // ---------------- AXIS source ----------------
    // pix 는 레지스터, tdata / tvalid / tlast / tuser 는 거기서 조합으로 뽑는다.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            pix <= 0;
        else if (running && s_axis_tvalid && s_axis_tready && pix < N_PIX)
            pix <= pix + 1;
    end

    always @(*) begin
        s_axis_tvalid = running && (pix < N_PIX);
        s_axis_tdata  = (pix < N_PIX) ? stim[pix] : 16'd0;
        s_axis_tuser  = (pix == 0);
        s_axis_tlast  = (pix == N_PIX - 1);
    end

    // ---------------- [A] weight ROM 타이밍 ----------------
    // cal_valid 인 사이클의 weight_out 은 그 사이클의 rom[out_ch_sel] 이어야 한다.
    wire [143:0] rom_expect = dut.U_WEIGHT_ROM_L1.rom[dut.out_ch_sel];

    always @(posedge clk) begin
        if (rst_n && dut.cal_valid) begin
            slip_checks = slip_checks + 1;

            // 이번 사이클에 실제로 나온 weight 가 몇 번 ROM entry 인지 역추적
            for (i = 0; i < OCH; i = i + 1)
                if (dut.weight_out === dut.U_WEIGHT_ROM_L1.rom[i])
                    rom_used[i] = rom_used[i] + 1;

            if (burst_logged < LOG_FIRST) begin
                burst_logged = burst_logged + 1;
                $display("  [rom] cyc %0d  out_ch_sel=%0d  weight_out.tap0=%04x  rom[out_ch_sel].tap0=%04x  %0s",
                         cyc, dut.out_ch_sel, dut.weight_out[15:0], rom_expect[15:0],
                         (dut.weight_out === rom_expect) ? "" : "<-- MISMATCH");
            end

            if (dut.weight_out !== rom_expect) begin
                slip_errs = slip_errs + 1;
                if (slip_shown < MAX_REPORT) begin
                    slip_shown = slip_shown + 1;
                    $display("[FAIL][rom] cyc %0d: out_ch_sel=%0d but weight_out matches a different entry",
                             cyc, dut.out_ch_sel);
                end
            end
        end
    end

    // ---------------- [B] 출력 비교 ----------------
    wire [48:0] out_word = {out_ch_done, out_data2, out_data1, out_data0};

    always @(posedge clk) begin
        if (rst_n && out_valid && out_ready) begin
            if (o < N_OUT) begin
                if (out_word !== gold[o]) begin
                    out_errs = out_errs + 1;
                    if (out_shown < MAX_REPORT) begin
                        out_shown = out_shown + 1;
                        $display("[FAIL][out] entry %0d (pass %0d pixel %0d): got %04x_%04x_%04x done=%b / exp %04x_%04x_%04x done=%b",
                                 o, o / N_WIN, o % N_WIN,
                                 out_data2, out_data1, out_data0, out_ch_done,
                                 gold[o][47:32], gold[o][31:16], gold[o][15:0], gold[o][48]);
                    end
                end
            end
            o = o + 1;
        end
    end

    // ---------------- cycle counter ----------------
    always @(posedge clk)
        if (rst_n) cyc = cyc + 1;

    // ---------------- main ----------------
    initial begin
        $timeformat(-9, 0, " ns", 10);
        clk = 1'b0; rst_n = 1'b0; running = 0;
        pix = 0; o = 0; cyc = 0;
        out_errs = 0; out_shown = 0;
        slip_checks = 0; slip_errs = 0; slip_shown = 0; burst_logged = 0;
        for (i = 0; i < OCH; i = i + 1) rom_used[i] = 0;

        $readmemh(STIM_FILE, stim);
        $readmemh(GOLD_FILE, gold);

        // (W1)(W2) conv_l1.v 배선 우회 - RTL 고치면 삭제
        force dut.out_ready = 1'b1;
        force dut.U_OUTPUT_STAGE_L1.ch_result1 = 36'sd0;
        force dut.U_OUTPUT_STAGE_L1.ch_result2 = 36'sd0;

        repeat (4) @(posedge clk);
        @(negedge clk);
        rst_n = 1'b1;

        // (W3) ROM 내용을 TB 가 다시 채운다 - RTL 의 $readmemh 포맷이 고쳐지면 삭제
        $readmemh(WEIGHT_FILE, dut.U_WEIGHT_ROM_L1.rom);
        $readmemh(BIAS_FILE,   dut.U_OUTPUT_STAGE_L1.GEN_CONV1.u_output_buffer.u_bias_rom.mem);
        $display("conv_l1 TB: %0d pixels -> %0d entries, weight/bias ROM loaded by the testbench\n",
                 N_PIX, N_OUT);
        $display("[A] weight ROM timing - first %0d cal_valid cycles:", LOG_FIRST);

        @(negedge clk);
        running = 1;

        // 전부 나오거나 timeout 까지
        while (o < N_OUT && cyc < MAX_CYCLES) @(posedge clk);
        repeat (20) @(posedge clk);
        running = 0;

        // ---------------- summary ----------------
        $display("\n[A] weight ROM entry usage (%0d windows x %0d ch = %0d expected per entry):",
                 N_WIN, OCH, N_WIN);
        for (i = 0; i < OCH; i = i + 1)
            $display("      rom[%0d] used %0d time(s)%0s", i, rom_used[i],
                     (rom_used[i] == N_WIN) ? "" : "   <-- expected 676");
        if (slip_errs == 0)
            $display("    -> weight_out == rom[out_ch_sel] on all %0d cal_valid cycles: OK", slip_checks);
        else
            $display("    -> %0d of %0d cal_valid cycles present the WRONG ROM entry (1-cycle slip)",
                     slip_errs, slip_checks);

        $display("\n[B] output vs C golden model (%s):", GOLD_FILE);
        if (o != N_OUT)
            $display("    -> got %0d entries, expected %0d%0s", o, N_OUT,
                     (cyc >= MAX_CYCLES) ? "  (TIMEOUT)" : "");
        else if (out_errs == 0)
            $display("    -> all %0d entries match", N_OUT);
        else
            $display("    -> %0d of %0d entries differ", out_errs, N_OUT);

        if (slip_errs == 0 && out_errs == 0 && o == N_OUT)
            $display("\n[PASS] conv_l1: %0d cycles.", cyc);
        else
            $display("\n[FAIL] conv_l1: rom slip %0d, output mismatch %0d, entries %0d/%0d, %0d cycles.",
                     slip_errs, out_errs, o, N_OUT, cyc);

        $finish;
    end

endmodule
