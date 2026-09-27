`timescale 1ns / 1ps
// Signed quantizer: y = clip(round_half_even(x_in / 2^SCALE_EXP), -32768, 32767).
// Same rounding as quantizer, but with no ReLU in front,
// so the shift is arithmetic and the result saturates on both ends.

module quantizer_signed #(
    parameter ACC_W     = 38,
    parameter SCALE_EXP = 13
) (
    input  wire signed [ACC_W-1:0] x_in,
    output wire signed [     15:0] y_out
);
    localparam signed [ACC_W:0] OUT_MAX = 32767;
    localparam signed [ACC_W:0] OUT_MIN = -32768;
    localparam [SCALE_EXP-1:0] HALF = {1'b1, {(SCALE_EXP - 1) {1'b0}}};

    wire signed [ACC_W-1:0] q = x_in >>> SCALE_EXP;  // arithmetic shift: floor
    wire [SCALE_EXP-1:0] rem = x_in[SCALE_EXP-1:0];  // two's complement low bits, always >= 0

    // round half to even: up if above half, or exactly half with an odd quotient
    wire round_up = (rem > HALF) | ((rem == HALF) & q[0]);

    wire signed [ACC_W:0] q_round = $signed({q[ACC_W-1], q}) + $signed({{ACC_W{1'b0}}, round_up});

    assign y_out = $signed((q_round > OUT_MAX) ? 16'sh7FFF : (q_round < OUT_MIN) ? 16'sh8000 : q_round[15:0]);

endmodule
