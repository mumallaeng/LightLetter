`timescale 1ns / 1ps

module mem_read_path #(
    parameter FFT_W = 40
) (
    input  wire               clk,
    input  wire               rst,
    input  wire               i_rdata_load_en,
    input  wire               i_bf_mem_sel,

    input  wire [FFT_W - 1:0] i_mem0_adata,
    input  wire [FFT_W - 1:0] i_mem1_adata,
    input  wire [FFT_W - 1:0] i_mem0_bdata,
    input  wire [FFT_W - 1:0] i_mem1_bdata,

    output reg  [FFT_W - 1:0] o_bf_adata,
    output reg  [FFT_W - 1:0] o_bf_bdata,
    output reg                o_rdata_valid,

    input  wire               i_core_out_en,
    input  wire               i_core_out_sel,
    output reg  [FFT_W - 1:0] o_fft_core_data,
    output reg                o_fft_core_valid
);

    wire [FFT_W - 1:0] w_bf_adata, w_bf_bdata, w_fft_core_data;

    mux_2x1 #(
        .WIDTH(FFT_W)
    ) U_A_MUX (
        .i_data0(i_mem0_adata),
        .i_data1(i_mem1_adata),
        .sel    (i_bf_mem_sel),
        .o_data (w_bf_adata)
    );

    mux_2x1 #(
        .WIDTH(FFT_W)
    ) U_B_MUX (
        .i_data0(i_mem0_bdata),
        .i_data1(i_mem1_bdata),
        .sel    (i_bf_mem_sel),
        .o_data (w_bf_bdata)
    );

    mux_2x1 #(
        .WIDTH(FFT_W)
    ) U_FFT_CORE_OUT_MUX (
        .i_data0(i_mem1_adata),
        .i_data1(i_mem1_bdata),
        .sel    (i_core_out_sel),
        .o_data (w_fft_core_data)
    );

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            o_bf_adata    <= {FFT_W{1'b0}};
            o_bf_bdata    <= {FFT_W{1'b0}};
            o_rdata_valid <= 1'b0;

            o_fft_core_data  <= {FFT_W{1'b0}};
			o_fft_core_valid <= 1'b0;
        end else begin
            o_rdata_valid    <= i_rdata_load_en;
			o_fft_core_valid <= i_core_out_en;

            if (i_rdata_load_en) begin
                o_bf_adata <= w_bf_adata;
				o_bf_bdata <= w_bf_bdata;
            end
            if (i_core_out_en) begin
                o_fft_core_data <= w_fft_core_data;
            end
        end
    end

endmodule
