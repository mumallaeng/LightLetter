`timescale 1ns / 1ps

module cov2_linebuf_mac #(
    parameter IMG_WIDTH = 28,
    parameter NUM_ACTIVE_CH = 3
)(
    input  wire         clk,
    input  wire         rst_n,

    // ---------------------------------------------------------
    // 3개 입력 채널
    // Phase 0 : CH0, CH1, CH2
    // Phase 1 : CH3, CH4, CH5
    // ---------------------------------------------------------
    input  wire [15:0]  pixel_in0,
    input  wire [15:0]  pixel_in1,
    input  wire [15:0]  pixel_in2,
    input  wire         pixel_valid,

    // 새로운 3채널 phase 시작 시 Line Buffer 초기화
    input  wire         phase_clear,

    // ---------------------------------------------------------
    // MAC Weight
    // CH0 : weight_in[143:0]
    // CH1 : weight_in[287:144]
    // CH2 : weight_in[431:288]
    // ---------------------------------------------------------
    input  wire [431:0] weight_in,

    // ---------------------------------------------------------
    // 채널별 Convolution 결과
    // 채널 간 합산은 Output Buffer에서 수행
    // ---------------------------------------------------------
    output wire [35:0]  ch_result0,
    output wire [35:0]  ch_result1,
    output wire [35:0]  ch_result2,

    output wire         mac_valid
);

    // =========================================================
    // Line Buffer → MAC 연결 신호
    // =========================================================

    wire [431:0] win_out;
    wire [2:0]   win_valid;


    // =========================================================
    // Line Buffer Array
    // =========================================================

    line_buffer_array #(
        .IMG_WIDTH(IMG_WIDTH)
    ) U_LINE_BUFFER_ARRAY (
        .clk         (clk),
        .rst_n       (rst_n),

        .pixel_in0   (pixel_in0),
        .pixel_in1   (pixel_in1),
        .pixel_in2   (pixel_in2),

        .pixel_valid (pixel_valid),
        .phase_clear (phase_clear),

        .win_out     (win_out),
        .win_valid   (win_valid)
    );


    // =========================================================
    // MAC Array
    // =========================================================

    MAC_array #(
        .NUM_ACTIVE_CH(NUM_ACTIVE_CH)
    ) U_MAC_ARRAY (
        .clk        (clk),
        .rst_n      (rst_n),

        .win_in     (win_out),
        .win_valid  (win_valid),
        .weight_in  (weight_in),

        .ch_result0 (ch_result0),
        .ch_result1 (ch_result1),
        .ch_result2 (ch_result2),

        .mac_valid  (mac_valid)
    );

endmodule