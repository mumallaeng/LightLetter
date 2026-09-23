`timescale 1ns / 1ps

module conv_l1 #(
    parameter OCH = 6
) (
    input         clk,
    input         rst_n,
    // axis - img preprocess
    input  [15:0] s_axis_tdata,
    input         s_axis_tvalid,
    output        s_axis_tready,
    input         s_axis_tuser,
    input         s_axis_tlast,
    // pooling layer
    output        out_valid,
    input         out_ready,
    output        out_ch_done,
    output [15:0] out_data0,
    output [15:0] out_data1,
    output [15:0] out_data2
);
    // CE controller
    // ----------------------------------
    wire win_valid, pixel_valid, phase_clear, mac_start, mac_done;

    ce_ctrl_l1 U_CE_CONTROLLER_L1 (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .win_valid    (win_valid),
        .pixel_valid  (pixel_valid),
        .phase_clear  (phase_clear),
        .mac_start    (mac_start),
        .mac_done     (mac_done)
    );

    // Weight Address controller
    // ----------------------------------
    wire [$clog2(OCH)-1:0] out_ch_sel;
    wire cal_valid;

    weight_addr_ctrl_l1 U_WEIGHT_ADDR_CONTROLLER_L1 (
        .clk       (clk),
        .rst_n     (rst_n),
        .mac_start (mac_start),
        .mac_done  (mac_done),
        .out_ch_sel(out_ch_sel),
        .cal_valid (cal_valid)
    );

    // Weight ROM
    // ----------------------------------
    wire [143:0] weight_out;

    weight_rom_l1 U_WEIGHT_ROM_L1 (
        .clk       (clk),
        .out_ch_sel(out_ch_sel),
        .weight_out(weight_out)
    );

    // Line buffer
    // ----------------------------------
    wire [143:0] win_out;

    line_buffer #(
        .IMG_WIDTH(28)
    ) U_LINEBUF_L1 (
        .clk        (clk),
        .rst_n      (rst_n),
        .pixel_in   (s_axis_tdata),
        .wr_en      (pixel_valid),
        .phase_clear(phase_clear),
        .win_out    (win_out),
        .win_valid  (win_valid)
    );

    // MAC Array
    // ----------------------------------
    wire [35:0] ch_result;
    wire mac_valid;

    mac_array_l1 U_MAC_ARRAY_L1 (
        .clk       (clk),
        .rst_n     (rst_n),
        .win_in    (win_out),
        .win_valid (cal_valid),
        .weight_in (weight_out),
        .ch_result0(ch_result),
        .mac_valid (mac_valid)
    );

    // Output Stage
    // ----------------------------------
    conv_out_stage #(
        .LAYER    (1),
        .SCALE_EXP(16),
        .CH_W     (36),
        .ACC_W    (40)
    ) U_OUTPUT_STAGE_L1 (
        .clk        (clk),
        .rst_n      (rst_n),
        .ch_result0 (ch_result),
        .ch_result1 (36'd0),
        .ch_result2 (36'd0),
        .ch3_5_en   (),
        .mac_valid  (mac_valid),
        .out_valid  (out_valid),
        .out_ready  (out_ready),
        .out_ch_done(out_ch_done),
        .out_data0  (out_data0),
        .out_data1  (out_data1),
        .out_data2  (out_data2)
    );
endmodule
