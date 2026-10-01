`timescale 1ns / 1ps

module pool_ctrl_l1 #(
    parameter IF_H   = 26,
    parameter IF_W   = 26,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input               clk,
    input               rst_n,
    // conv layer
    input               out_valid,
    output              out_ready,
    input               out_ch_done,
    // pooling datapath
    output              prev_we,
    output              pool_we,
    output [ADDR_W-1:0] pool_addr,
    // next layer
    output              pool_valid,
    input               pool_ready,
    output              pool_ch_done
);
    localparam ROW_W = $clog2(IF_H);
    localparam COL_W = $clog2(IF_W);

    localparam ROW_LAST = IF_H - 1;
    localparam COL_LAST = IF_W - 1;
    localparam WIN_ROW_LAST = (IF_H / 2) * 2 - 1;  // odd size: drop last row
    localparam WIN_COL_LAST = (IF_W / 2) * 2 - 1;  // odd size: drop last col

    // ----- register -----
    reg [ROW_W-1:0] row_cnt, row_cnt_next;
    reg [COL_W-1:0] col_cnt, col_cnt_next;

    wire row_last, col_last, win_last;
    wire row_odd, col_odd, win_valid, pixel_valid;

    assign row_last = (row_cnt == ROW_LAST);
    assign col_last = (col_cnt == COL_LAST);
    assign win_last = (row_cnt == WIN_ROW_LAST) & (col_cnt == WIN_COL_LAST);

    // ----- State Update Logic -----
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            row_cnt <= 0;
            col_cnt <= 0;
        end else begin
            row_cnt <= row_cnt_next;
            col_cnt <= col_cnt_next;
        end
    end

    // ----- Next State Logic -----
    always @(*) begin
        row_cnt_next = row_cnt;
        col_cnt_next = col_cnt;

        if (pixel_valid) begin
            if (col_last) begin
                col_cnt_next = 0;
                row_cnt_next = row_last ? 0 : row_cnt + 1;
            end else begin
                col_cnt_next = col_cnt + 1;
            end
        end
    end

    // ========== Output Logic ==========
    assign row_odd = row_cnt[0];  // LSB of cnt value
    assign col_odd = col_cnt[0];
    assign win_valid = row_odd & col_odd;

    assign out_ready = pool_ready | ~win_valid;
    assign pixel_valid = out_valid & out_ready;
    assign pool_valid = out_valid & win_valid;
    assign pool_ch_done = pool_valid & win_last;

    assign pool_addr = col_cnt[ADDR_W:1];  // col_cnt >> 1
    assign prev_we = pixel_valid & ~col_odd;
    assign pool_we = pixel_valid & col_odd & ~row_odd;
endmodule
