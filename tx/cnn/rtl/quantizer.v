`timescale 1ns / 1ps
// Quantizer: y = clip(round_half_even(x / 2^SCALE_EXP), lo, 32767), lo = 0 with RELU, -32768 without.
// RELU zeroes a negative x first, so the arithmetic shift below never sees one.

module quantizer #(
    parameter ACC_W     = 40,
    parameter SCALE_EXP = 14,         // 1..ACC_W-1
    parameter RELU      = 1
) (
    input  wire signed [ACC_W-1:0] x_in,
    output wire signed [     15:0] y_out
);
    localparam signed [ACC_W:0] OUT_MAX = 32767;
    localparam signed [ACC_W:0] OUT_MIN = -32768;
    localparam [SCALE_EXP-1:0] HALF = {1'b1, {(SCALE_EXP - 1) {1'b0}}};

    wire signed [ACC_W-1:0] x = (RELU != 0 && x_in[ACC_W-1]) ? {ACC_W{1'b0}} : x_in;

    wire signed [ACC_W-1:0] q = x >>> SCALE_EXP;  // arithmetic shift: floor
    wire [SCALE_EXP-1:0] rem = x[SCALE_EXP-1:0];  // two's complement low bits, always >= 0

    // round half to even: up if above half, or exactly half with an odd quotient
    wire round_up = (rem > HALF) | ((rem == HALF) & q[0]);

    wire signed [ACC_W:0] q_round = $signed({q[ACC_W-1], q}) + $signed({{ACC_W{1'b0}}, round_up});

    assign y_out = $signed((q_round > OUT_MAX) ? 16'sh7FFF : (q_round < OUT_MIN) ? 16'sh8000 : q_round[15:0]);

endmodule
