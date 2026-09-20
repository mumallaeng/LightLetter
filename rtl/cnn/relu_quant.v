`timescale 1ns / 1ps
// ReLU & Quantization (top):
// Output Buffer -> ReLU -> Quantizer -> Lane Packer -> Output FIFO -> MaxPooling

module relu_quant #(
    parameter ACC_W      = 40,
    parameter PACK       = 3,          // values per output entry: conv1 = 3, conv2 = 1
    parameter SCALE_EXP  = 16,         // quantizer right shift (0..31)
    parameter FIFO_DEPTH = 2048
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire signed [ACC_W-1:0] sum_data,
    input  wire                    sum_valid,
    input  wire                    ch_done,
    input  wire                    out_ready,    // <- MaxPooling
    output wire [15:0]             out_data0,
    output wire [15:0]             out_data1,    // 0 if PACK < 2
    output wire [15:0]             out_data2,    // 0 if PACK < 3
    output wire                    out_ch_done,
    output wire                    out_valid
);

    localparam FIFO_W = 16 * PACK + 1;

    wire [ACC_W-1:0]  relu_y;
    wire [15:0]       quant_y;
    wire [FIFO_W-1:0] pack_data;
    wire              pack_valid;
    wire [FIFO_W-1:0] fifo_dout;
    wire              fifo_empty;

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
        .q_done    (ch_done),
        .pack_data (pack_data),
        .pack_valid(pack_valid)
    );

    // ========== Output FIFO ==========
    output_fifo #(
        .WIDTH(FIFO_W),
        .DEPTH(FIFO_DEPTH)
    ) u_output_fifo (
        .clk  (clk),
        .rst_n(rst_n),
        .push (pack_valid),
        .din  (pack_data),
        .rd_en(out_ready),
        .dout (fifo_dout),
        .empty(fifo_empty)
    );

    // ========== Output Logic ==========
    assign out_data0   = fifo_dout[15:0];
    assign out_ch_done = fifo_dout[16*PACK];
    assign out_valid   = ~fifo_empty;

    generate
        if (PACK > 1) begin : GEN_LANE1
            assign out_data1 = fifo_dout[31:16];
        end else begin : GEN_NO_LANE1
            assign out_data1 = 16'd0;
        end

        if (PACK > 2) begin : GEN_LANE2
            assign out_data2 = fifo_dout[47:32];
        end else begin : GEN_NO_LANE2
            assign out_data2 = 16'd0;
        end
    endgenerate

endmodule
