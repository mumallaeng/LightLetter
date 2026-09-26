`timescale 1ns / 1ps
// FC3 output stage: signed quantizer (no ReLU) plus the same reorder buffer as relu_quant.
// out_reorder runs with N=1 and GROUPS=N_OUT, so it only absorbs backpressure here -
// FC has no pixel axis, so the read order matches the write order.
// out_data0 keeps relu_quant's name so fc_layer wires FC1/FC2 and FC3 the same way.

module fc_quant_signed #(
    parameter ACC_W     = 38,
    parameter N_OUT     = 36,
    parameter SCALE_EXP = 13
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire signed [ACC_W-1:0] sum_data,
    input  wire                    sum_valid,
    input  wire                    out_ready,
    output wire signed [     15:0] out_data0,
    output wire                    out_valid
);

    wire signed [15:0] q_value;
    wire        [15:0] rb_dout;
    wire               rb_avail;

    assign out_data0 = $signed(rb_dout);
    assign out_valid = rb_avail;

    quantizer_signed #(
        .ACC_W    (ACC_W),
        .SCALE_EXP(SCALE_EXP)
    ) u_quantizer_signed (
        .x_in (sum_data),
        .y_out(q_value)
    );

    out_reorder #(
        .WIDTH (16),
        .N     (1),
        .GROUPS(N_OUT)
    ) u_out_reorder (
        .clk       (clk),
        .rst_n     (rst_n),
        .push      (sum_valid),
        .din       (q_value),
        .rd_en     (out_ready),
        .dout      (rb_dout),
        .avail     (rb_avail),
        .last_pixel()
    );

endmodule
