`timescale 1ns / 1ps
// Fully Connected top: MaxPooling -> FC1 -> FC2 -> FC3 -> logit stream (to Argmax).
// The layers are chained as value-at-a-time valid/ready streams,
// so each layer's out_ready comes from the next layer's in_ready.
// FC3 ends the Fully Connected scope: 36 signed logits in class order.

module fc_top #(
    // ROM contents; the testbench overrides these with paths relative to tb/cnn
    parameter FC1_WEIGHT = "fc1_weight.mem",
    parameter FC1_BIAS   = "fc1_bias.mem",
    parameter FC2_WEIGHT = "fc2_weight.mem",
    parameter FC2_BIAS   = "fc2_bias.mem",
    parameter FC3_WEIGHT = "fc3_weight.mem",
    parameter FC3_BIAS   = "fc3_bias.mem"
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

    wire [15:0] l1_out_data, l2_out_data;
    wire l1_out_valid, l2_out_valid;
    wire l2_in_ready, l3_in_ready;

    fc_layer #(
        .N_IN       (400),
        .N_OUT      (120),
        .L          (25),
        .NUM_CHUNK  (16),
        .ACC_W      (40),
        .RELU       (1),
        .SCALE_EXP  (15),
        .WEIGHT_FILE(FC1_WEIGHT),
        .BIAS_FILE  (FC1_BIAS)
    ) u_fc1 (
        .clk      (clk),
        .rst_n    (rst_n),
        .in_data  (fc_in_data),
        .in_valid (fc_in_valid),
        .out_ready(l2_in_ready),
        .in_ready (fc_in_ready),
        .out_data (l1_out_data),
        .out_valid(l1_out_valid)
    );

    fc_layer #(
        .N_IN       (120),
        .N_OUT      (84),
        .L          (10),
        .NUM_CHUNK  (12),
        .ACC_W      (38),
        .RELU       (1),
        .SCALE_EXP  (14),
        .WEIGHT_FILE(FC2_WEIGHT),
        .BIAS_FILE  (FC2_BIAS)
    ) u_fc2 (
        .clk      (clk),
        .rst_n    (rst_n),
        .in_data  (l1_out_data),
        .in_valid (l1_out_valid),
        .out_ready(l3_in_ready),
        .in_ready (l2_in_ready),
        .out_data (l2_out_data),
        .out_valid(l2_out_valid)
    );

    fc_layer #(
        .N_IN       (84),
        .N_OUT      (36),
        .L          (5),
        .NUM_CHUNK  (17),                // 84 = 16 x 5 + 4, so the last chunk is short
        .ACC_W      (38),
        .RELU       (0),                 // signed quantizer, no ReLU
        .SCALE_EXP  (13),
        .WEIGHT_FILE(FC3_WEIGHT),
        .BIAS_FILE  (FC3_BIAS)
    ) u_fc3 (
        .clk      (clk),
        .rst_n    (rst_n),
        .in_data  (l2_out_data),
        .in_valid (l2_out_valid),
        .out_ready(logit_ready),
        .in_ready (l3_in_ready),
        .out_data (logit_data),
        .out_valid(logit_valid)
    );

endmodule
