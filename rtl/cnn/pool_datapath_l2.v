`timescale 1ns / 1ps

module pool_datapath_l2 #(
    parameter IF_W   = 11,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input               clk,
    input               rst_n,
    input               prev_we,
    input  [      15:0] pool_in,
    input  [ADDR_W-1:0] pool_addr,
    input               pool_we,
    output [      15:0] pool_data
);
    // Single Max Logic
    max_logic #(
        .IF_W(IF_W)
    ) U_MAX_LOGIC_L2 (
        .clk      (clk),
        .rst_n    (rst_n),
        .prev_we  (prev_we),
        .pool_in  (pool_in),
        .pool_addr(pool_addr),
        .pool_we  (pool_we),
        .pool_data(pool_data)
    );
endmodule
