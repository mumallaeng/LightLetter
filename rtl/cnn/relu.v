`timescale 1ns / 1ps
// ReLU: y = max(x, 0) (combinational).

module relu #(
    parameter ACC_W = 40
) (
    input  wire signed [ACC_W-1:0] x_in,
    output wire        [ACC_W-1:0] y_out
);

    assign y_out = x_in[ACC_W-1] ? {ACC_W{1'b0}} : $unsigned(x_in);

endmodule
