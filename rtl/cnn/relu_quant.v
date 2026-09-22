`timescale 1ns / 1ps
// ReLU & Quantization (top):
//   Output Buffer -> ReLU -> Quantizer -> Lane Packer -> Reorder Buffer -> MaxPooling

module relu_quant #(
    parameter ACC_W     = 40,
    parameter N         = 121,         // pixels per frame
    parameter C_OUT     = 16,          // output channels
    parameter PACK      = 1,           // values per output entry: conv1 = 3, conv2 = 1
    parameter SCALE_EXP = 16           // quantizer right shift (0..31)
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire signed [ACC_W-1:0] sum_data,
    input  wire                    sum_valid,
    input  wire                    out_ready,    // <- MaxPooling
    output wire [15:0]             out_data0,
    output wire [15:0]             out_data1,    // 0 if PACK < 2
    output wire [15:0]             out_data2,    // 0 if PACK < 3
    output wire                    out_ch_done,  // entry belongs to the last pixel of its channel group
    output wire                    out_valid
);

    localparam ENTRY_W = 16 * PACK;
    localparam GROUPS  = C_OUT / PACK;

    wire [ACC_W-1:0]   relu_y;
    wire [15:0]        quant_y;
    wire [ENTRY_W-1:0] pack_data;
    wire               pack_valid;
    wire [ENTRY_W-1:0] rb_dout;
    wire               rb_avail;
    wire               rb_last_pixel;

    // ========== ReLU -> Quantizer ==========
    relu #(
        .ACC_W(ACC_W)
    ) u_relu (
        .x_in (sum_data),
        .y_out(relu_y)
    );

    quantizer #(
        .ACC_W    (ACC_W),
        .SCALE_EXP(SCALE_EXP)
    ) u_quantizer (
        .x_in (relu_y),
        .y_out(quant_y)
    );

    // ========== Lane Packer ==========
    lane_packer #(
        .PACK(PACK)
    ) u_lane_packer (
        .clk       (clk),
        .rst_n     (rst_n),
        .q_in      (quant_y),
        .q_valid   (sum_valid),
        .pack_data (pack_data),
        .pack_valid(pack_valid)
    );

    // ========== Reorder Buffer ==========
    out_reorder #(
        .WIDTH (ENTRY_W),
        .N     (N),
        .GROUPS(GROUPS)
    ) u_out_reorder (
        .clk       (clk),
        .rst_n     (rst_n),
        .push      (pack_valid),
        .din       (pack_data),
        .rd_en     (out_ready),
        .dout      (rb_dout),
        .avail     (rb_avail),
        .last_pixel(rb_last_pixel)
    );

    // ========== Output Logic ==========
    assign out_data0   = rb_dout[15:0];
    assign out_ch_done = rb_avail & rb_last_pixel;
    assign out_valid   = rb_avail;

    generate
        if (PACK > 1) begin : GEN_LANE1
            assign out_data1 = rb_dout[31:16];
        end else begin : GEN_NO_LANE1
            assign out_data1 = 16'd0;
        end

        if (PACK > 2) begin : GEN_LANE2
            assign out_data2 = rb_dout[47:32];
        end else begin : GEN_NO_LANE2
            assign out_data2 = 16'd0;
        end
    endgenerate

endmodule
