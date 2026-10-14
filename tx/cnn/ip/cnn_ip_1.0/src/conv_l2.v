`timescale 1ns / 1ps

module conv_l2 #(
    parameter OCH = 16
) (
    input         clk,
    input         rst_n,
    // post pooling layer
    input  [15:0] pool_data0,
    input  [15:0] pool_data1,
    input  [15:0] pool_data2,
    input         pool_valid,
    output        pool_ready,
    input         pool_ch_done,
    // pre pooling_layer
    output        out_valid,
    input         out_ready,
    output        out_ch_done,
    output [15:0] out_data
);
    // CE controller
    // ----------------------------------
    wire win_valid, pixel_valid, phase_clear, mac_start, mac_done, is_ch35;
    // temporal
    wire [2:0] raw_win_valid;
    assign win_valid = &raw_win_valid;

    ce_ctrl_l2 U_CE_CONTROLLER_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done),
        .win_valid   (win_valid),
        .pixel_valid (pixel_valid),
        .phase_clear (phase_clear),
        .is_ch35     (is_ch35),
        .mac_start   (mac_start),
        .mac_done    (mac_done)
    );

    // Weight Address controller
    // ----------------------------------
    wire [$clog2(OCH)-1:0] out_ch_sel;
    wire rom_is_ch35, cal_valid, cal_valid_rt;

    weight_addr_ctrl_l2 #(
        .OCH(OCH)
    ) U_WEIGHT_ADDR_CONTROLLER_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .mac_start   (mac_start),
        .mac_done    (mac_done),
        .is_ch35     (is_ch35),
        .rom_is_ch35 (rom_is_ch35),
        .out_ch_sel  (out_ch_sel),
        .cal_valid   (cal_valid),
        .cal_valid_rt(cal_valid_rt)
    );

    // Weight ROM
    // ----------------------------------
    wire [431:0] weight_out;
    wire [  2:0] win_zero;  // per input channel: whole 3x3 window is zero
    wire [  2:0] rom_en = {3{cal_valid}} & ~win_zero;  // zero window: that bank holds its row

    weight_rom_l2 #(
        .OCH(16)
    ) U_WEIGHT_ROM_L2 (
        .clk       (clk),
        .is_ch35   (rom_is_ch35),
        .out_ch_sel(out_ch_sel),
        .rom_en    (rom_en),
        .weight_out(weight_out)
    );


    // Line buffer Array
    // ----------------------------------
    wire [431:0] win_out;

    line_buffer_array #(
        .IMG_WIDTH(13)
    ) U_LINEBUF_ARRAY_L2 (
        .clk        (clk),
        .rst_n      (rst_n),
        .pixel_in0  (pool_data0),
        .pixel_in1  (pool_data1),
        .pixel_in2  (pool_data2),
        .pixel_valid(pixel_valid),
        .phase_clear(phase_clear),
        .win_out    (win_out),
        .win_valid  (raw_win_valid)
    );

    // Zero Gating Array
    // ----------------------------------
    // registers the window and the ROM row; cal_valid_rt (aligned with weight_out) -> cal_valid_zg
    wire [431:0] zg_win, zg_weight;
    wire [ 26:0] is_zero;
    wire         cal_valid_zg;

    zero_gating_array #(
        .N(27)
    ) U_ZERO_GATING_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .win_next    (win_out),
        .weight_next (weight_out),
        .cal_valid   (cal_valid_rt),
        .win_in      (zg_win),
        .weight_in   (zg_weight),
        .is_zero     (is_zero),
        .cal_valid_rt(cal_valid_zg)
    );

    assign win_zero[0] = &is_zero[ 8: 0];
    assign win_zero[1] = &is_zero[17: 9];
    assign win_zero[2] = &is_zero[26:18];

    // MAC Array
    // ----------------------------------
    wire [35:0] ch_result0, ch_result1, ch_result2;
    wire mac_valid;

    MAC_array #(
        .NUM_ACTIVE_CH(3)
    ) U_MAC_ARRAY_L2 (
        .clk       (clk),
        .rst_n     (rst_n),
        .win_in    (zg_win),
        .win_valid ({3{cal_valid_zg}}),
        .weight_in (zg_weight),
        .mul_en    ({27{cal_valid_zg}} & ~is_zero),
        .ch_result0(ch_result0),
        .ch_result1(ch_result1),
        .ch_result2(ch_result2),
        .mac_valid (mac_valid)
    );

    // Output Stage
    // ----------------------------------
    conv_out_stage #(
        .LAYER    (2),
        .SCALE_EXP(16),
        .CH_W     (36),
        .ACC_W    (40)
    ) U_OUTPUT_STAGE_L1 (
        .clk        (clk),
        .rst_n      (rst_n),
        .ch_result0 (ch_result0),
        .ch_result1 (ch_result1),
        .ch_result2 (ch_result2),
        .ch3_5_en   (),
        .mac_valid  (mac_valid),
        .out_valid  (out_valid),
        .out_ready  (out_ready),
        .out_ch_done(out_ch_done),
        .out_data0  (out_data),
        .out_data1  (),
        .out_data2  ()
    );

endmodule
