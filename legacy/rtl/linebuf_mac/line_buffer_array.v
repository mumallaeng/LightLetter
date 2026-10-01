`timescale 1ns / 1ps

module line_buffer_array #(
    parameter IMG_WIDTH = 28
)(
    input  wire         clk,
    input  wire         rst_n,
    input  wire [15:0]  pixel_in0,   // 채널0 전용 픽셀 입력
    input  wire [15:0]  pixel_in1,   // 채널1 전용 픽셀 입력
    input  wire [15:0]  pixel_in2,   // 채널2 전용 픽셀 입력
    input  wire         pixel_valid, // 3개 채널 픽셀이 동시에 유효한지 
    input  wire         phase_clear,
    output wire [431:0] win_out,
    output wire [2:0]   win_valid
);

wire wr_en = pixel_valid;

line_buffer #(.IMG_WIDTH(IMG_WIDTH)) U_LINE_BUFFER0(
    .clk         (clk),
    .rst_n       (rst_n),
    .pixel_in    (pixel_in0),
    .wr_en       (wr_en),
    .phase_clear (phase_clear),
    .win_out     (win_out[143:0]),
    .win_valid   (win_valid[0])
);

line_buffer #(.IMG_WIDTH(IMG_WIDTH)) U_LINE_BUFFER1(
    .clk         (clk),
    .rst_n       (rst_n),
    .pixel_in    (pixel_in1),
    .wr_en       (wr_en),
    .phase_clear (phase_clear),
    .win_out     (win_out[287:144]),
    .win_valid   (win_valid[1])
);

line_buffer #(.IMG_WIDTH(IMG_WIDTH)) U_LINE_BUFFER2(
    .clk         (clk),
    .rst_n       (rst_n),
    .pixel_in    (pixel_in2),
    .wr_en       (wr_en),
    .phase_clear (phase_clear),
    .win_out     (win_out[431:288]),
    .win_valid   (win_valid[2])
);

endmodule