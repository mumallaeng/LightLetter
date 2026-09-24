`timescale 1ns / 1ps

module pool_datapath_l1 #(
    parameter IF_W   = 26,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input               clk,
    input               rst_n,
    input               prev_we,
    input  [      15:0] pool_in0,
    input  [      15:0] pool_in1,
    input  [      15:0] pool_in2,
    input  [ADDR_W-1:0] pool_addr,
    input               pool_we,
    output [      15:0] pool_data0,
    output [      15:0] pool_data1,
    output [      15:0] pool_data2
);
    // MAX Logic x3
    max_logic #(
        .IF_W(IF_W)
    ) U_MAX_LOGIC_0 (
        .clk      (clk),
        .rst_n    (rst_n),
        .prev_we  (prev_we),
        .pool_in  (pool_in0),
        .pool_addr(pool_addr),
        .pool_we  (pool_we),
        .pool_data(pool_data0)
    );

    max_logic #(
        .IF_W(IF_W)
    ) U_MAX_LOGIC_1 (
        .clk      (clk),
        .rst_n    (rst_n),
        .prev_we  (prev_we),
        .pool_in  (pool_in1),
        .pool_addr(pool_addr),
        .pool_we  (pool_we),
        .pool_data(pool_data1)
    );

    max_logic #(
        .IF_W(IF_W)
    ) U_MAX_LOGIC_2 (
        .clk      (clk),
        .rst_n    (rst_n),
        .prev_we  (prev_we),
        .pool_in  (pool_in2),
        .pool_addr(pool_addr),
        .pool_we  (pool_we),
        .pool_data(pool_data2)
    );
endmodule
