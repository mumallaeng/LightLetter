`timescale 1ns / 1ps

module mem_write_path #(
    parameter FFT_W = 40
) (
    input wire               clk,
    input wire               rst,
    input wire               i_buf_data_load_en,  // buffer에서 데이터 두 개 준비 완료
    input wire [FFT_W - 1:0] i_buf_data0,
    input wire [FFT_W - 1:0] i_buf_data1,
    input wire               i_buf_wb0_sel,     // 0: Buffer Load, 1: Butterfly

    input wire               i_bf_out_load_en,    // butterfly 연산 결과 valid
    input wire               i_mem_pinpong_sel, // 0: MEM0, 1:MEM1
    input wire [FFT_W - 1:0] i_y0_data,
    input wire [FFT_W - 1:0] i_y1_data,

    output reg [FFT_W - 1:0] o_mem0_wdata_0,
    output reg [FFT_W - 1:0] o_mem0_wdata_1,
    output reg               o_wdata_valid_0,
    output reg [FFT_W - 1:0] o_mem1_wdata_0,
    output reg [FFT_W - 1:0] o_mem1_wdata_1,
    output reg               o_wdata_valid_1
);

    wire [FFT_W - 1:0] y0_2_mem0_wb_data, y1_2_mem0_wb_data;
    wire [FFT_W - 1:0] w_mem0_wdata_0, w_mem0_wdata_1, w_mem1_wdata_0, w_mem1_wdata_1;
    wire               w_wdata_valid_0, w_wdata_valid_1;

    demux_1x2 #(.WIDTH(FFT_W)) U_Y0_WB_DEMUX (
        .i_data (i_y0_data),
        .sel    (i_mem_pinpong_sel),
        .o_data0(y0_2_mem0_wb_data),
        .o_data1(w_mem1_wdata_0)
    );

    demux_1x2 #(.WIDTH(FFT_W)) U_Y1_WB_DEMUX (
        .i_data (i_y1_data),
        .sel    (i_mem_pinpong_sel),
        .o_data0(y1_2_mem0_wb_data),
        .o_data1(w_mem1_wdata_1)
    );

    mux_2x1 #(.WIDTH(FFT_W)) U_MEM0_WDATA0_MUX (
        .i_data0(i_buf_data0),
        .i_data1(y0_2_mem0_wb_data),
        .sel    (i_buf_wb0_sel),
        .o_data (w_mem0_wdata_0)
    );

    mux_2x1 #(.WIDTH(FFT_W)) U_MEM0_WDATA1_MUX (
        .i_data0(i_buf_data1),
        .i_data1(y1_2_mem0_wb_data),
        .sel    (i_buf_wb0_sel),
        .o_data (w_mem0_wdata_1)
    );

    assign w_wdata_valid_0 = i_buf_wb0_sel ? (i_bf_out_load_en && !i_mem_pinpong_sel) : i_buf_data_load_en;
    assign w_wdata_valid_1 = i_buf_wb0_sel && i_bf_out_load_en && i_mem_pinpong_sel;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            o_mem0_wdata_0  <= {FFT_W{1'b0}};
            o_mem0_wdata_1  <= {FFT_W{1'b0}};
            o_mem1_wdata_0  <= {FFT_W{1'b0}};
            o_mem1_wdata_1  <= {FFT_W{1'b0}};

            o_wdata_valid_0 <= 1'b0;
            o_wdata_valid_1 <= 1'b0;
        end else begin
			o_mem0_wdata_0 <= w_wdata_valid_0 ? w_mem0_wdata_0 : {FFT_W{1'b0}};
			o_mem0_wdata_1 <= w_wdata_valid_0 ? w_mem0_wdata_1 : {FFT_W{1'b0}};
			o_mem1_wdata_0 <= w_wdata_valid_1 ? w_mem1_wdata_0 : {FFT_W{1'b0}};
			o_mem1_wdata_1 <= w_wdata_valid_1 ? w_mem1_wdata_1 : {FFT_W{1'b0}};

			o_wdata_valid_0 <= w_wdata_valid_0;
			o_wdata_valid_1 <= w_wdata_valid_1;
        end
    end

endmodule

module mux_2x1 #(
    parameter WIDTH = 40
) (
    input  wire [WIDTH - 1:0] i_data0,
    input  wire [WIDTH - 1:0] i_data1,
    input  wire               sel,
    output wire [WIDTH - 1:0] o_data
);

    assign o_data = (sel) ? i_data1 : i_data0;

endmodule

module demux_1x2 #(
    parameter WIDTH = 40
) (
    input  wire [WIDTH - 1:0] i_data,
    input  wire               sel,
    output reg  [WIDTH - 1:0] o_data0,
    output reg  [WIDTH - 1:0] o_data1
);

    always @(*) begin
        o_data0 = {WIDTH{1'b0}};
        o_data1 = {WIDTH{1'b0}};
        if (sel) begin
            o_data1 = i_data;
        end else begin
            o_data0 = i_data;
        end
    end

endmodule