`timescale 1ns / 1ps
// Synthesis / timing check only - not part of the design.
// Registers every input and output of conv_out_stage so that all paths, including the
// port-to-port ones (ch_result -> sum_data -> quantizer -> FIFO), become register-to-register
// and show up in the timing report. The input registers stand in for mac_array's output
// registers, the output registers for MaxPooling's input registers.

module conv_out_stage_synth #(
    parameter LAYER     = 2,
    parameter SCALE_EXP = 16,
    parameter CH_W      = 36
) (
    input  wire                   clk,
    input  wire                   rst_n,
    input  wire signed [CH_W-1:0] ch_result0,
    input  wire signed [CH_W-1:0] ch_result1,
    input  wire signed [CH_W-1:0] ch_result2,
    input  wire                   mac_valid,
    input  wire                   out_ready,
    output reg                    ch3_5_en,
    output reg  [15:0]            out_data0,
    output reg  [15:0]            out_data1,
    output reg  [15:0]            out_data2,
    output reg                    out_ch_done,
    output reg                    out_valid
);

    reg signed [CH_W-1:0] ch_result0_r, ch_result1_r, ch_result2_r;
    reg                   mac_valid_r, out_ready_r, rst_n_r;

    wire        ch3_5_en_w, out_ch_done_w, out_valid_w;
    wire [15:0] out_data0_w, out_data1_w, out_data2_w;

    always @(posedge clk) begin
        rst_n_r      <= rst_n;
        ch_result0_r <= ch_result0;
        ch_result1_r <= ch_result1;
        ch_result2_r <= ch_result2;
        mac_valid_r  <= mac_valid;
        out_ready_r  <= out_ready;

        ch3_5_en     <= ch3_5_en_w;
        out_data0    <= out_data0_w;
        out_data1    <= out_data1_w;
        out_data2    <= out_data2_w;
        out_ch_done  <= out_ch_done_w;
        out_valid    <= out_valid_w;
    end

    conv_out_stage #(
        .LAYER    (LAYER),
        .SCALE_EXP(SCALE_EXP),
        .CH_W     (CH_W)
    ) u_conv_out_stage (
        .clk        (clk),
        .rst_n      (rst_n_r),
        .ch_result0 (ch_result0_r),
        .ch_result1 (ch_result1_r),
        .ch_result2 (ch_result2_r),
        .mac_valid  (mac_valid_r),
        .out_ready  (out_ready_r),
        .ch3_5_en   (ch3_5_en_w),
        .out_data0  (out_data0_w),
        .out_data1  (out_data1_w),
        .out_data2  (out_data2_w),
        .out_ch_done(out_ch_done_w),
        .out_valid  (out_valid_w)
    );

endmodule
