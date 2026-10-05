`timescale 1ns / 1ps
// Fully Connected top: one shared engine (P multipliers) runs FC1 -> FC2 -> FC3 in groups of P neurons.
module fc_top #(
    parameter P           = 20,
    parameter ACC_W       = 40,
    parameter WEIGHT_FILE = "fc_weight.mem",
    parameter BIAS_FILE   = "fc_bias.mem"
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [15:0] fc_in_data,   // PyTorch flatten order (c*25+y*5+x)
    input  wire        fc_in_valid,
    input  wire        logit_ready,
    output wire        fc_in_ready,
    output wire [15:0] logit_data,   // signed
    output wire        logit_valid
);

    localparam N_IN1 = 400, N_OUT1 = 120, GROUPS1 = 6, ROM_BASE1 = 0,    BIAS_BASE1 = 0,  SCALE_EXP1 = 16;
    localparam N_IN2 = 120, N_OUT2 = 84,  GROUPS2 = 5, ROM_BASE2 = 2400, BIAS_BASE2 = 6,  SCALE_EXP2 = 15;
    localparam N_IN3 = 84,  N_OUT3 = 26,  GROUPS3 = 2, ROM_BASE3 = 3000, BIAS_BASE3 = 11, SCALE_EXP3 = 14;
    localparam ROM_ROWS = 3168, BIAS_ROWS = 13;
    localparam ROM_AW = $clog2(ROM_ROWS), BIAS_AW = $clog2(BIAS_ROWS), IN_AW = $clog2(N_IN1);

    wire              ctrl_fc1_in_we, ctrl_first, ctrl_mac_en, ctrl_last;
    wire [ IN_AW-1:0] ctrl_fc1_in_waddr, ctrl_feature_raddr;
    wire [       1:0] ctrl_layer;
    wire [       2:0] ctrl_group;
    wire [ROM_AW-1:0] ctrl_weight_addr;
    wire [BIAS_AW-1:0] ctrl_bias_addr;

    wire [15:0] x_fc1, x_fc2, x_fc3, feature;
    wire [16*P-1:0]    rom_weight;
    wire [32*P-1:0]    rom_bias;
    wire               mac_sum_valid, mac_busy;
    wire [ACC_W*P-1:0] mac_sum;
    wire [        1:0] mac_layer;
    wire [        2:0] mac_group;
    wire               qout_hold_free, qout_feature_we;
    wire [        1:0] qout_feature_layer;
    wire [        6:0] qout_feature_waddr;
    wire [       15:0] qout_feature_wdata;
    wire signed [15:0] qout_logit;

    // ========== control ==========
    fc_ctrl #(
        .N_IN1(N_IN1), .N_IN2(N_IN2), .N_IN3(N_IN3),
        .GROUPS1(GROUPS1), .GROUPS2(GROUPS2), .GROUPS3(GROUPS3),
        .ROM_BASE1(ROM_BASE1), .ROM_BASE2(ROM_BASE2), .ROM_BASE3(ROM_BASE3),
        .BIAS_BASE1(BIAS_BASE1), .BIAS_BASE2(BIAS_BASE2), .BIAS_BASE3(BIAS_BASE3),
        .ROM_AW(ROM_AW), .BIAS_AW(BIAS_AW), .IN_AW(IN_AW)
    ) u_ctrl (
        .clk        (clk),
        .rst_n      (rst_n),
        .fc_in_valid(fc_in_valid),
        .mac_busy   (mac_busy),
        .hold_free  (qout_hold_free),
        .fc_in_ready(fc_in_ready),
        .fc1_in_we    (ctrl_fc1_in_we),
        .fc1_in_waddr (ctrl_fc1_in_waddr),
        .layer      (ctrl_layer),
        .group      (ctrl_group),
        .feature_raddr (ctrl_feature_raddr),
        .weight_addr  (ctrl_weight_addr),
        .bias_addr    (ctrl_bias_addr),
        .first      (ctrl_first),
        .mac_en     (ctrl_mac_en),
        .last       (ctrl_last)
    );

    // ========== feature buffers ==========
    fc_feature_buf #(.DEPTH(N_IN1)) u_fc1_in (
        .clk(clk), .we(ctrl_fc1_in_we), .waddr(ctrl_fc1_in_waddr), .wdata(fc_in_data), .raddr(ctrl_feature_raddr), .rdata(x_fc1));
    fc_feature_buf #(.DEPTH(N_OUT1)) u_fc2_in (
        .clk(clk), .we(qout_feature_we & (qout_feature_layer == 2'd1)), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[6:0]), .rdata(x_fc2));
    fc_feature_buf #(.DEPTH(N_OUT2)) u_fc3_in (
        .clk(clk), .we(qout_feature_we & (qout_feature_layer == 2'd2)), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[6:0]), .rdata(x_fc3));

    assign feature = (ctrl_layer == 2'd1) ? x_fc1 : (ctrl_layer == 2'd2) ? x_fc2 : x_fc3;

    // ========== ROMs ==========
    fc_weight_rom #(.P(P), .ROWS(ROM_ROWS), .ROM_FILE(WEIGHT_FILE), .AW(ROM_AW)) u_wrom (
        .clk(clk), .addr(ctrl_weight_addr), .w_out(rom_weight));
    fc_bias_rom #(.P(P), .ROWS(BIAS_ROWS), .ROM_FILE(BIAS_FILE), .AW(BIAS_AW)) u_brom (
        .addr(ctrl_bias_addr), .b_out(rom_bias));

    // ========== multiply-accumulate ==========
    fc_mac #(.P(P), .ACC_W(ACC_W)) u_mac (
        .clk      (clk),
        .rst_n    (rst_n),
        .feature  (feature),
        .weight   (rom_weight),
        .bias     (rom_bias),
        .first    (ctrl_first),
        .mac_en   (ctrl_mac_en),
        .last     (ctrl_last),
        .layer    (ctrl_layer),
        .group    (ctrl_group),
        .sum_valid(mac_sum_valid),
        .sum      (mac_sum),
        .sum_layer(mac_layer),
        .sum_group(mac_group),
        .busy     (mac_busy)
    );

    // ========== quantize and output ==========
    fc_quant_out #(
        .P(P), .ACC_W(ACC_W), .N_OUT1(N_OUT1), .N_OUT2(N_OUT2), .N_OUT3(N_OUT3),
        .SCALE_EXP1(SCALE_EXP1), .SCALE_EXP2(SCALE_EXP2), .SCALE_EXP3(SCALE_EXP3)
    ) u_quant_out (
        .clk        (clk),
        .rst_n      (rst_n),
        .sum_valid  (mac_sum_valid),
        .sum        (mac_sum),
        .sum_layer  (mac_layer),
        .sum_group  (mac_group),
        .logit_ready(logit_ready),
        .hold_free  (qout_hold_free),
        .feature_we     (qout_feature_we),
        .feature_layer  (qout_feature_layer),
        .feature_waddr  (qout_feature_waddr),
        .feature_wdata  (qout_feature_wdata),
        .logit_data (qout_logit),
        .logit_valid(logit_valid)
    );

    assign logit_data = qout_logit;

endmodule
