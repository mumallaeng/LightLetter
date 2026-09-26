`timescale 1ns / 1ps

module cnn_top #(
    parameter NUM_CLASS = 36
) (
    input                          clk,
    input                          rst_n,
    // axis - img preprocess
    input  [                 15:0] s_axis_tdata,
    input                          s_axis_tvalid,
    output                         s_axis_tready,
    input                          s_axis_tuser,
    input                          s_axis_tlast,
    // output
    output [$clog2(NUM_CLASS)-1:0] cnn_result,
    output                         cnn_done
);

    // Convolution Layer 1
    // ===============================
    wire l1_out_valid, l1_out_ready, l1_out_ch_done;
    wire [15:0] l1_out_data0, l1_out_data1, l1_out_data2;

    conv_l1 #(
        .OCH(6)
    ) U_CONV_L1 (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .out_valid    (l1_out_valid),
        .out_ready    (l1_out_ready),
        .out_ch_done  (l1_out_ch_done),
        .out_data0    (l1_out_data0),
        .out_data1    (l1_out_data1),
        .out_data2    (l1_out_data2)
    );

    // Pooling Layer 1
    // ===============================
    wire l1_pool_valid, l1_pool_ready, l1_pool_ch_done;
    wire [15:0] l1_pool_data0, l1_pool_data1, l1_pool_data2;

    pool_l1 #(
        .IF_H(26),
        .IF_W(26)
    ) U_POOL_L1 (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_data0   (l1_out_data0),
        .out_data1   (l1_out_data1),
        .out_data2   (l1_out_data2),
        .out_valid   (l1_out_valid),
        .out_ready   (l1_out_ready),
        .out_ch_done (l1_out_ch_done),
        .pool_data0  (l1_pool_data0),
        .pool_data1  (l1_pool_data1),
        .pool_data2  (l1_pool_data2),
        .pool_valid  (l1_pool_valid),
        .pool_ready  (l1_pool_ready),
        .pool_ch_done(l1_pool_ch_done)
    );

    // Convolution Layer 2
    // ===============================
    wire l2_out_valid, l2_out_ready, l2_out_ch_done;
    wire [15:0] l2_out_data;

    conv_l2 #(
        .OCH(16)
    ) U_CONV_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .pool_data0  (l1_pool_data0),
        .pool_data1  (l1_pool_data1),
        .pool_data2  (l1_pool_data2),
        .pool_valid  (l1_pool_valid),
        .pool_ready  (l1_pool_ready),
        .pool_ch_done(l1_pool_ch_done),
        .out_valid   (l2_out_valid),
        .out_ready   (l2_out_ready),
        .out_ch_done (l2_out_ch_done),
        .out_data    (l2_out_data)
    );

    // Pooling Layer 2 - Temporal
    // ===============================
    wire l2_pool_valid, l2_pool_ready, l2_pool_ch_done;
    wire [15:0] l2_pool_data;

    pool_l2 #(
        .IF_H(11),
        .IF_W(11)
    ) U_POOL_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_data    (l2_out_data),
        .out_valid   (l2_out_valid),
        .out_ready   (l2_out_ready),
        .out_ch_done (l2_out_ch_done),
        .pool_data   (l2_pool_data),
        .pool_valid  (l2_pool_valid),
        .pool_ready  (l2_pool_ready),
        .pool_ch_done(l2_pool_ch_done)
    );

    // Fully Connected Layer
    // ===============================
    wire logit_valid, logit_ready;
    wire signed [15:0] logit_data;

    fc_top U_FC (
        .clk        (clk),
        .rst_n      (rst_n),
        .fc_in_data (l2_pool_data),
        .fc_in_valid(l2_pool_valid),
        .fc_in_ready(l2_pool_ready),
        .logit_data (logit_data),
        .logit_valid(logit_valid),
        .logit_ready(logit_ready)
    );

    // Argmax
    // ===============================
    argmax #(
        .NUM_CLASS(NUM_CLASS)
    ) U_ARGMAX (
        .clk        (clk),
        .rst_n      (rst_n),
        .logit_data (logit_data),
        .logit_valid(logit_valid),
        .logit_ready(logit_ready),
        .cnn_result (cnn_result),
        .cnn_done   (cnn_done)
    );
endmodule
