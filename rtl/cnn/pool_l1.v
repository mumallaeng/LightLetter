`timescale 1ns / 1ps

module pool_l1 #(
    parameter IF_H   = 26,
    parameter IF_W   = 26,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input         clk,
    input         rst_n,
    // post conv layer
    input  [15:0] out_data0,
    input  [15:0] out_data1,
    input  [15:0] out_data2,
    input         out_valid,
    output        out_ready,
    input         out_ch_done,
    // pre conv layer
    output [15:0] pool_data0,
    output [15:0] pool_data1,
    output [15:0] pool_data2,
    output        pool_valid,
    input         pool_ready,
    output        pool_ch_done
);
    // ========== Inner Wire ==========
    wire prev_we, pool_we;
    wire [ADDR_W-1:0] pool_addr;

    // ========== Controller ==========
    pool_ctrl_l1 #(
        .IF_H(IF_H),
        .IF_W(IF_W)
    ) U_POOL_CTRL_L1 (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .prev_we     (prev_we),
        .pool_we     (pool_we),
        .pool_addr   (pool_addr),
        .pool_valid  (pool_valid),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done)
    );

    // ========== Datapath ==========
    pool_datapath_l1 #(
        .IF_W(IF_W)
    ) U_POOL_DATAPATH_L1 (
        .clk       (clk),
        .rst_n     (rst_n),
        .prev_we   (prev_we),
        .pool_in0  (out_data0),
        .pool_in1  (out_data1),
        .pool_in2  (out_data2),
        .pool_addr (pool_addr),
        .pool_we   (pool_we),
        .pool_data0(pool_data0),
        .pool_data1(pool_data1),
        .pool_data2(pool_data2)
    );
endmodule
