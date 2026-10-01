`timescale 1ns / 1ps
// Conv output stage: mac_array -> Output Buffer -> ReLU & Quantization -> MaxPooling
// LAYER selects the conv1 / conv2 configuration of both blocks.

module conv_out_stage #(
    parameter LAYER     = 2,           // 1 = conv1, 2 = conv2
    parameter SCALE_EXP = 16,
    parameter CH_W      = 36,
    parameter ACC_W     = 40
) (
    input  wire                   clk,
    input  wire                   rst_n,
    input  wire signed [CH_W-1:0] ch_result0,
    input  wire signed [CH_W-1:0] ch_result1,
    input  wire signed [CH_W-1:0] ch_result2,
    input  wire                   mac_valid,
    input  wire                   out_ready,
    output wire                   ch3_5_en,
    output wire [15:0]            out_data0,
    output wire [15:0]            out_data1,
    output wire [15:0]            out_data2,
    output wire                   out_ch_done,
    output wire                   out_valid
);

    localparam N          = (LAYER == 1) ? 676 : 121;
    localparam C_OUT      = (LAYER == 1) ? 6   : 16;
    localparam NUM_GROUPS = (LAYER == 1) ? 1   : 2;
    localparam PACK       = (LAYER == 1) ? 3   : 1;

    wire signed [ACC_W-1:0] sum_data;
    wire                    sum_valid;

    generate
        if (LAYER == 1) begin : GEN_CONV1
            output_buffer #(
                .N         (N),
                .C_OUT     (C_OUT),
                .NUM_GROUPS(NUM_GROUPS),
                .CH_W      (CH_W),
                .ACC_W     (ACC_W),
                .BIAS_FILE ("conv1_bias.mem")
            ) u_output_buffer (
                .clk       (clk),
                .rst_n     (rst_n),
                .ch_result0(ch_result0),
                .ch_result1(ch_result1),
                .ch_result2(ch_result2),
                .mac_valid (mac_valid),
                .ch3_5_en  (ch3_5_en),
                .sum_data  (sum_data),
                .sum_valid (sum_valid)
            );
        end else begin : GEN_CONV2
            output_buffer #(
                .N         (N),
                .C_OUT     (C_OUT),
                .NUM_GROUPS(NUM_GROUPS),
                .CH_W      (CH_W),
                .ACC_W     (ACC_W),
                .BIAS_FILE ("conv2_bias.mem")
            ) u_output_buffer (
                .clk       (clk),
                .rst_n     (rst_n),
                .ch_result0(ch_result0),
                .ch_result1(ch_result1),
                .ch_result2(ch_result2),
                .mac_valid (mac_valid),
                .ch3_5_en  (ch3_5_en),
                .sum_data  (sum_data),
                .sum_valid (sum_valid)
            );
        end
    endgenerate

    relu_quant #(
        .ACC_W    (ACC_W),
        .N        (N),
        .C_OUT    (C_OUT),
        .PACK     (PACK),
        .SCALE_EXP(SCALE_EXP)
    ) u_relu_quant (
        .clk        (clk),
        .rst_n      (rst_n),
        .sum_data   (sum_data),
        .sum_valid  (sum_valid),
        .out_ready  (out_ready),
        .out_data0  (out_data0),
        .out_data1  (out_data1),
        .out_data2  (out_data2),
        .out_ch_done(out_ch_done),
        .out_valid  (out_valid)
    );

endmodule
