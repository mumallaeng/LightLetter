`timescale 1ns / 1ps

module pool_l2 #(
    parameter IF_H   = 11,
    parameter IF_W   = 11,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input             clk,
    input             rst_n,
    // post conv layer
    input      [15:0] out_data,
    input             out_valid,
    output            out_ready,
    input             out_ch_done,
    // pre conv layer
    output reg [15:0] pool_data,
    output reg        pool_valid,
    input             pool_ready,
    output reg        pool_ch_done
);
    // ========== Inner Wire ==========
    wire prev_we, pool_we;
    wire [ADDR_W-1:0] pool_addr;

    // to avoid setup violation
    wire [15:0] pool_data_next;
    wire pool_valid_next, pool_ch_done_next;

    // ========== Controller ==========
    pool_ctrl_l2 #(
        .IF_H(IF_H),
        .IF_W(IF_W)
    ) U_POOL_CTRL_L2 (
        .clk         (clk),
        .rst_n       (rst_n),
        .out_valid   (out_valid),
        .out_ready   (out_ready),
        .out_ch_done (out_ch_done),
        .prev_we     (prev_we),
        .pool_we     (pool_we),
        .pool_addr   (pool_addr),
        .pool_valid  (pool_valid_next),
        .pool_ready  (pool_ready),
        .pool_ch_done(pool_ch_done_next)
    );

    // ========== Datapath ==========
    pool_datapath_l2 #(
        .IF_W(IF_W)
    ) U_POOL_DATAPATH_L2 (
        .clk      (clk),
        .rst_n    (rst_n),
        .prev_we  (prev_we),
        .pool_in  (out_data),
        .pool_addr(pool_addr),
        .pool_we  (pool_we),
        .pool_data(pool_data_next)
    );

    // ========== Output stage register - to avoid setup violation ==========
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            pool_data    <= 0;
            pool_valid   <= 1'b0;
            pool_ch_done <= 1'b0;
        end else begin
            pool_data    <= pool_data_next;
            pool_valid   <= pool_valid_next;
            pool_ch_done <= pool_ch_done_next;
        end
    end
endmodule
