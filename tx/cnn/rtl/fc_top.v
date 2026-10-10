`timescale 1ns / 1ps
// Fully Connected top: one shared engine (P multipliers) runs FC1 -> FC2 -> FC3 in groups of P neurons.
// Zero gating / skipping: each feature buffer has a zero buffer written with it; fc_ctrl skips with it,
// the weight ROM and fc_zero_gating stop on a zero input.
// The weight ROM read register and fc_zero_gating's feature register are fc_mac's stage 1.
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

    localparam N_IN1 = 400, N_OUT1 = 120, GROUPS1 = 6, SCALE_EXP1 = 16;
    localparam N_IN2 = 120, N_OUT2 = 84,  GROUPS2 = 5, SCALE_EXP2 = 14;
    localparam N_IN3 = 84,  N_OUT3 = 26,  GROUPS3 = 2, SCALE_EXP3 = 14;
    localparam ROM_ROWS = N_IN1 * GROUPS1 + N_IN2 * GROUPS2 + N_IN3 * GROUPS3, BIAS_ROWS = GROUPS1 + GROUPS2 + GROUPS3;
    localparam ROM_AW = $clog2(ROM_ROWS), BIAS_AW = $clog2(BIAS_ROWS), IN_AW = $clog2(N_IN1), FEAT_AW = $clog2(N_OUT1);

    wire              ctrl_fc1_in_we, ctrl_first, ctrl_mac_en, ctrl_last, ctrl_weight_en;
    wire [ IN_AW-1:0] ctrl_fc1_in_waddr, ctrl_feature_raddr;
    wire [       1:0] ctrl_layer;
    wire [       2:0] ctrl_group;
    wire [ROM_AW-1:0] ctrl_weight_addr;
    wire [BIAS_AW-1:0] ctrl_bias_addr;

    wire [15:0] x_fc1, x_fc2, x_fc3;
    reg  [15:0] feature;
    wire [16*P-1:0]    rom_weight;
    wire [32*P-1:0]    rom_bias;
    wire               mac_sum_valid, mac_busy, mac_last_inflight;
    wire [ACC_W*P-1:0] mac_sum;
    wire [        1:0] mac_layer;
    wire [        2:0] mac_group;
    wire               qout_hold_free, qout_feature_we;
    wire [        1:0] qout_feature_layer;
    wire [FEAT_AW-1:0] qout_feature_waddr;
    wire [       15:0] qout_feature_wdata;
    wire signed [15:0] qout_logit;

    wire               fc2_in_we = qout_feature_we & (qout_feature_layer == 2'd1);
    wire               fc3_in_we = qout_feature_we & (qout_feature_layer == 2'd2);
    wire               z1_zero, z2_zero, z3_zero;
    wire [  IN_AW-1:0] z1_nxt, z1_last;
    wire [FEAT_AW-1:0] z2_nxt, z2_last, z3_nxt, z3_last;
    reg                is_zero;
    reg  [  IN_AW-1:0] skip_nxt, skip_last;
    wire [       15:0] zg_feature;
    wire               zg_zero;

    // ========== control ==========
    fc_ctrl #(
        .N_IN1(N_IN1), .N_IN2(N_IN2), .N_IN3(N_IN3),
        .GROUPS1(GROUPS1), .GROUPS2(GROUPS2), .GROUPS3(GROUPS3),
        .ROM_AW(ROM_AW), .BIAS_AW(BIAS_AW), .IN_AW(IN_AW)
    ) u_ctrl (
        .clk        (clk),
        .rst_n      (rst_n),
        .fc_in_valid(fc_in_valid),
        .mac_busy   (mac_busy),
        .last_inflight (mac_last_inflight),
        .hold_free  (qout_hold_free),
        .skip_nxt   (skip_nxt),
        .skip_last  (skip_last),
        .fc_in_ready(fc_in_ready),
        .fc1_in_we    (ctrl_fc1_in_we),
        .fc1_in_waddr (ctrl_fc1_in_waddr),
        .layer      (ctrl_layer),
        .group      (ctrl_group),
        .feature_raddr (ctrl_feature_raddr),
        .weight_addr  (ctrl_weight_addr),
        .weight_en    (ctrl_weight_en),
        .bias_addr    (ctrl_bias_addr),
        .first      (ctrl_first),
        .mac_en     (ctrl_mac_en),
        .last       (ctrl_last)
    );

    // ========== feature buffers ==========
    fc_feature_buf #(.DEPTH(N_IN1)) u_fc1_in (
        .clk(clk), .we(ctrl_fc1_in_we), .waddr(ctrl_fc1_in_waddr), .wdata(fc_in_data), .raddr(ctrl_feature_raddr), .rdata(x_fc1));
    fc_feature_buf #(.DEPTH(N_OUT1), .AW(FEAT_AW)) u_fc2_in (
        .clk(clk), .we(fc2_in_we), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[FEAT_AW-1:0]), .rdata(x_fc2));
    fc_feature_buf #(.DEPTH(N_OUT2), .AW(FEAT_AW)) u_fc3_in (
        .clk(clk), .we(fc3_in_we), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[FEAT_AW-1:0]), .rdata(x_fc3));

    // ========== zero buffers: same writes as the feature buffers ==========
    fc_zero_buf #(.DEPTH(N_IN1), .AW(IN_AW)) u_fc1_zero (
        .clk(clk), .rst_n(rst_n), .we(ctrl_fc1_in_we), .waddr(ctrl_fc1_in_waddr), .wdata(fc_in_data),
        .raddr(ctrl_feature_raddr), .zero(z1_zero), .nxt(z1_nxt), .last(z1_last));
    fc_zero_buf #(.DEPTH(N_OUT1), .AW(FEAT_AW)) u_fc2_zero (
        .clk(clk), .rst_n(rst_n), .we(fc2_in_we), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[FEAT_AW-1:0]), .zero(z2_zero), .nxt(z2_nxt), .last(z2_last));
    fc_zero_buf #(.DEPTH(N_OUT2), .AW(FEAT_AW)) u_fc3_zero (
        .clk(clk), .rst_n(rst_n), .we(fc3_in_we), .waddr(qout_feature_waddr), .wdata(qout_feature_wdata),
        .raddr(ctrl_feature_raddr[FEAT_AW-1:0]), .zero(z3_zero), .nxt(z3_nxt), .last(z3_last));

    always @(*) begin
        case (ctrl_layer)
            2'd1:    feature = x_fc1;
            2'd2:    feature = x_fc2;
            default: feature = x_fc3;
        endcase
    end

    localparam FPAD = IN_AW - FEAT_AW;

    always @(*) begin
        case (ctrl_layer)
            2'd1: begin
                is_zero   = z1_zero;
                skip_nxt  = z1_nxt;
                skip_last = z1_last;
            end
            2'd2: begin
                is_zero   = z2_zero;
                skip_nxt  = {{FPAD{1'b0}}, z2_nxt};
                skip_last = {{FPAD{1'b0}}, z2_last};
            end
            default: begin
                is_zero   = z3_zero;
                skip_nxt  = {{FPAD{1'b0}}, z3_nxt};
                skip_last = {{FPAD{1'b0}}, z3_last};
            end
        endcase
    end

    // weight ROM gating: a zero input does not read its row, w_out keeps the previous one
    wire rom_en = ctrl_weight_en & ~is_zero;

    // ========== ROMs ==========
    fc_weight_rom #(.P(P), .ROWS(ROM_ROWS), .ROM_FILE(WEIGHT_FILE), .AW(ROM_AW)) u_wrom (
        .clk(clk), .en(rom_en), .addr(ctrl_weight_addr), .w_out(rom_weight));
    fc_bias_rom #(.P(P), .ROWS(BIAS_ROWS), .ROM_FILE(BIAS_FILE), .AW(BIAS_AW)) u_brom (
        .addr(ctrl_bias_addr), .b_out(rom_bias));

    // ========== zero gating array: stage 1 feature register beside w_out, held on a zero input ==========
    fc_zero_gating u_zero_gating (
        .clk      (clk),
        .rst_n    (rst_n),
        .valid    (ctrl_mac_en),
        .is_zero  (is_zero),
        .feature  (feature),
        .feature_q(zg_feature),
        .zero_q   (zg_zero)
    );

    // ========== multiply-accumulate ==========
    fc_mac #(.P(P), .ACC_W(ACC_W)) u_mac (
        .clk      (clk),
        .rst_n    (rst_n),
        .feature  (zg_feature),
        .weight   (rom_weight),
        .zero     (zg_zero),
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
        .busy     (mac_busy),
        .last_inflight(mac_last_inflight)
    );

    // ========== quantize and output ==========
    fc_quant_out #(
        .P(P), .ACC_W(ACC_W), .N_OUT1(N_OUT1), .N_OUT2(N_OUT2), .N_OUT3(N_OUT3), .WADDR_AW(FEAT_AW),
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
