`timescale 1ns / 1ps
// Fully Connected top, step 2: one shared engine (P multipliers) runs FC1 -> FC2 -> FC3.
// Ports are those of the step 1 fc_top: the MaxPooling stream in, the logit stream to Argmax.
// act_in holds the frame's 400 inputs, act_a / act_b the FC1 / FC2 outputs; one weight ROM,
// one bias ROM, the MAC lanes and the drain serve all three layers in turn (fc_ctrl).
// Group-outer / input-inner order: a group of P neurons is held while every input streams past;
// the group's first input starts the accumulators from the bias (DSP opmode C + M).

module fc_top #(
    parameter P           = 20,
    parameter ACC_W       = 40,
    // ROM contents; the testbench overrides these with paths relative to tb/cnn
    parameter WEIGHT_FILE = "fc_weight.mem",
    parameter BIAS_FILE   = "fc_bias.mem"
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [15:0] fc_in_data,   // <- MaxPooling, PyTorch flatten order (c*25+y*5+x)
    input  wire        fc_in_valid,
    input  wire        logit_ready,  // <- Argmax
    output wire        fc_in_ready,
    output wire [15:0] logit_data,   // signed
    output wire        logit_valid
);

    // layer constants (export_fc_vectors.py prints the same ROM bases and shifts)
    localparam N_IN1 = 400, N_OUT1 = 120, GROUPS1 = 6, ROM_BASE1 = 0,    BIAS_BASE1 = 0,  SCALE_EXP1 = 16;
    localparam N_IN2 = 120, N_OUT2 = 84,  GROUPS2 = 5, ROM_BASE2 = 2400, BIAS_BASE2 = 6,  SCALE_EXP2 = 15;
    localparam N_IN3 = 84,  N_OUT3 = 26,  GROUPS3 = 2, ROM_BASE3 = 3000, BIAS_BASE3 = 11, SCALE_EXP3 = 14;
    localparam ROM_ROWS = 3168, BIAS_ROWS = 13;
    localparam ROM_AW = $clog2(ROM_ROWS), BIAS_AW = $clog2(BIAS_ROWS), IN_AW = $clog2(N_IN1);

    wire              ctl_in_we, ctl_first, ctl_mac_en, ctl_last;
    wire [ IN_AW-1:0] ctl_in_waddr, ctl_x_raddr;
    wire [       1:0] ctl_layer;
    wire [       2:0] ctl_group;
    wire [ROM_AW-1:0] ctl_rom_addr;
    wire [BIAS_AW-1:0] ctl_bias_addr;

    wire [15:0] x_in, x_a, x_b, x;
    wire [16*P-1:0]    rom_w;
    wire [32*P-1:0]    rom_b;
    wire               mac_sum_valid, mac_busy;
    wire [ACC_W*P-1:0] mac_sum;
    wire [        1:0] mac_layer;
    wire [        2:0] mac_group;
    wire               drn_hold_free, drn_act_we;
    wire [        1:0] drn_act_layer;
    wire [        6:0] drn_act_waddr;
    wire [       15:0] drn_act_wdata;
    wire signed [15:0] drn_logit;

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
        .hold_free  (drn_hold_free),
        .fc_in_ready(fc_in_ready),
        .in_we      (ctl_in_we),
        .in_waddr   (ctl_in_waddr),
        .layer      (ctl_layer),
        .group      (ctl_group),
        .x_raddr    (ctl_x_raddr),
        .rom_addr   (ctl_rom_addr),
        .bias_addr  (ctl_bias_addr),
        .first      (ctl_first),
        .mac_en     (ctl_mac_en),
        .last       (ctl_last)
    );

    // ========== activation buffers ==========
    fc_act_buf #(.DEPTH(N_IN1)) u_act_in (
        .clk(clk), .we(ctl_in_we), .waddr(ctl_in_waddr), .wdata(fc_in_data), .raddr(ctl_x_raddr), .rdata(x_in));
    fc_act_buf #(.DEPTH(N_OUT1)) u_act_a (
        .clk(clk), .we(drn_act_we & (drn_act_layer == 2'd1)), .waddr(drn_act_waddr), .wdata(drn_act_wdata),
        .raddr(ctl_x_raddr[6:0]), .rdata(x_a));
    fc_act_buf #(.DEPTH(N_OUT2)) u_act_b (
        .clk(clk), .we(drn_act_we & (drn_act_layer == 2'd2)), .waddr(drn_act_waddr), .wdata(drn_act_wdata),
        .raddr(ctl_x_raddr[6:0]), .rdata(x_b));

    assign x = (ctl_layer == 2'd1) ? x_in : (ctl_layer == 2'd2) ? x_a : x_b;

    // ========== ROMs ==========
    fc_weight_rom #(.P(P), .ROWS(ROM_ROWS), .ROM_FILE(WEIGHT_FILE), .AW(ROM_AW)) u_wrom (
        .clk(clk), .addr(ctl_rom_addr), .w_out(rom_w));
    fc_bias_rom #(.P(P), .ROWS(BIAS_ROWS), .ROM_FILE(BIAS_FILE), .AW(BIAS_AW)) u_brom (
        .addr(ctl_bias_addr), .b_out(rom_b));

    // ========== multiply-accumulate ==========
    fc_mac_acc #(.P(P), .ACC_W(ACC_W)) u_mac (
        .clk      (clk),
        .rst_n    (rst_n),
        .x        (x),
        .w        (rom_w),
        .b        (rom_b),
        .first    (ctl_first),
        .mac_en   (ctl_mac_en),
        .last     (ctl_last),
        .layer    (ctl_layer),
        .group    (ctl_group),
        .sum_valid(mac_sum_valid),
        .sum      (mac_sum),
        .sum_layer(mac_layer),
        .sum_group(mac_group),
        .busy     (mac_busy)
    );

    // ========== quantize and drain ==========
    fc_drain #(
        .P(P), .ACC_W(ACC_W), .N_OUT1(N_OUT1), .N_OUT2(N_OUT2), .N_OUT3(N_OUT3),
        .SCALE_EXP1(SCALE_EXP1), .SCALE_EXP2(SCALE_EXP2), .SCALE_EXP3(SCALE_EXP3)
    ) u_drain (
        .clk        (clk),
        .rst_n      (rst_n),
        .sum_valid  (mac_sum_valid),
        .sum        (mac_sum),
        .sum_layer  (mac_layer),
        .sum_group  (mac_group),
        .logit_ready(logit_ready),
        .hold_free  (drn_hold_free),
        .act_we     (drn_act_we),
        .act_layer  (drn_act_layer),
        .act_waddr  (drn_act_waddr),
        .act_wdata  (drn_act_wdata),
        .logit_data (drn_logit),
        .logit_valid(logit_valid)
    );

    assign logit_data = drn_logit;

endmodule
