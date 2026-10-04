`timescale 1ns / 1ps
// Quantizer: y = clip(round_half_even(x / 2^SCALE_EXP), 0, 32767), x >= 0 (combinational).

module quantizer #(
    parameter ACC_W     = 40,
    parameter SCALE_EXP = 16          // 0..31
) (
    input  wire [ACC_W-1:0] x_in,
    output wire [15:0]      y_out
);

    localparam [ACC_W-1:0] ONE = {{(ACC_W - 1) {1'b0}}, 1'b1};
    localparam [ACC_W-1:0] REM_MASK = (SCALE_EXP == 0) ? {ACC_W{1'b0}} : ((ONE << SCALE_EXP) - ONE);
    localparam [ACC_W-1:0] HALF = (SCALE_EXP == 0) ? {ACC_W{1'b0}} : (ONE << (SCALE_EXP - 1));
    localparam [ACC_W-1:0] OUT_MAX = 32767;

    wire [ACC_W-1:0] q   = x_in >> SCALE_EXP;
    wire [ACC_W-1:0] rem = x_in & REM_MASK;

    // round half to even: up if above half, or exactly half with an odd quotient
    wire round_up = (SCALE_EXP != 0) & ((rem > HALF) | ((rem == HALF) & q[0]));

    wire [ACC_W-1:0] q_round = q + {{(ACC_W-1){1'b0}}, round_up};

    assign y_out = (q_round > OUT_MAX) ? OUT_MAX[15:0] : q_round[15:0];

endmodule
