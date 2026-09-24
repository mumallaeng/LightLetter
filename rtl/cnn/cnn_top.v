`timescale 1ns / 1ps

module cnn_top (
    input         clk,
    input         rst_n,
    // axis - img preprocess
    input  [15:0] s_axis_tdata,
    input         s_axis_tvalid,
    output        s_axis_tready,
    input         s_axis_tuser,
    input         s_axis_tlast,
    // connecting
    output        pool_valid,
    output        pool_ready,
    output        pool_ch_done,
    output [15:0] pool_data0,
    output [15:0] pool_data1,
    output [15:0] pool_data2
);

    // Convolution Layer 1
    // ===============================
    wire out_valid, out_ready, out_ch_done;
    wire [15:0] out_data0, out_data1, out_data2;

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
        .out_valid    (out_valid),
        .out_ready    (out_ready),
        .out_ch_done  (out_ch_done),
        .out_data0    (out_data0),
        .out_data1    (out_data1),
        .out_data2    (out_data2)
    );

    // Pooling Layer 1
    // ===============================
    // wire pool_valid, pool_ready, pool_ch_done;
    // wire [15:0] pool_data0, pool_data1, pool_data2;

    pool_l1 #(
        .IF_H(26),
        .IF_W(26)
    ) U_POOL_L1 (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_data0   (out_data0),
        .out_data1   (out_data1),
        .out_data2   (out_data2),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .pool_data0  (pool_data0),
        .pool_data1  (pool_data1),
        .pool_data2  (pool_data2),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done)
    );

endmodule
