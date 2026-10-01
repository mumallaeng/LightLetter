`timescale 1ns / 1ps

module fft_core #(
    parameter FFT_DATA_BIT = 40
) (
    input  wire                      clk,
    input  wire                      rst,
    input  wire                      i_frame_valid,
    output wire                      o_frame_ready,
    input  wire                      i_buf_data_valid,
    output wire                      o_buf_data_ready,
    input  wire [FFT_DATA_BIT - 1:0] i_buf_data0,
    input  wire [FFT_DATA_BIT - 1:0] i_buf_data1,
    output wire [FFT_DATA_BIT - 1:0] o_fft_core_data,
    output wire                      o_fft_core_valid
    //output wire                      o_buf_data_load_en
);

    wire       w_buf_data_load_en;
    wire       w_mem_pingpong_sel;
    wire       w_buf_wb0_sel;
    wire       w_bf_out_load_en;
    wire       w_wdata_valid_0;
    wire       w_wdata_valid_1;
    wire [5:0] w_bf_count;
    wire [2:0] w_bf_stage;
    wire [2:0] w_sys_state;
    wire       w_core_out_en;
    wire       w_rdata_load_en;
    wire       w_core_out_sel;
    wire       w_bf_mem_sel;
    wire       w_bf_out_valid;

    assign o_buf_data_ready = w_buf_data_load_en;

    fft_core_controller U_FFT_CORE_CTRL (
        .clk               (clk),
        .rst               (rst),
        .i_frame_valid     (i_frame_valid),
        .o_frame_ready     (o_frame_ready),
        .i_buf_data_valid  (i_buf_data_valid),
        .o_buf_data_load_en(w_buf_data_load_en),
        .o_mem_pingpong_sel(w_mem_pingpong_sel),
        .o_buf_wb0_sel     (w_buf_wb0_sel),
        .o_bf_out_load_en  (w_bf_out_load_en),
        .i_wdata_valid_0   (w_wdata_valid_0),
        .i_wdata_valid_1   (w_wdata_valid_1),
        .o_bf_count        (w_bf_count),
        .o_bf_stage        (w_bf_stage),
        .o_sys_state       (w_sys_state),
        .o_core_out_en     (w_core_out_en),
        .o_rdata_load_en   (w_rdata_load_en),
        .o_core_out_sel    (w_core_out_sel),
        .o_bf_mem_sel      (w_bf_mem_sel),
        .i_bf_out_valid    (w_bf_out_valid)
    );

    wire [FFT_DATA_BIT - 1:0] w_mem0_wdata_0;
    wire [FFT_DATA_BIT - 1:0] w_mem0_wdata_1;
    wire [FFT_DATA_BIT - 1:0] w_mem1_wdata_0;
    wire [FFT_DATA_BIT - 1:0] w_mem1_wdata_1;
    wire [FFT_DATA_BIT - 1:0] w_y0_data;
    wire [FFT_DATA_BIT - 1:0] w_y1_data;

    wire [FFT_DATA_BIT - 1:0] w_bf_adata;
    wire [FFT_DATA_BIT - 1:0] w_bf_bdata;
    wire w_rdata_valid;

    wire signed [19:0] a_re = w_bf_adata[39:20];
    wire signed [19:0] a_im = w_bf_adata[19:0];

    wire signed [19:0] b_re = w_bf_bdata[39:20];
    wire signed [19:0] b_im = w_bf_bdata[19:0];

    wire signed [19:0] y0_re;
    wire signed [19:0] y0_im;
    wire signed [19:0] y1_re;
    wire signed [19:0] y1_im;

    assign w_y0_data = {y0_re, y0_im};
    assign w_y1_data = {y1_re, y1_im};

    mem_write_path #(
        .FFT_W(FFT_DATA_BIT)
    ) U_MEM_WPATH (
        .clk               (clk),
        .rst               (rst),
        .i_buf_data_load_en(w_buf_data_load_en),
        .i_buf_data0       (i_buf_data0),
        .i_buf_data1       (i_buf_data1),
        .i_buf_wb0_sel     (w_buf_wb0_sel),
        .i_bf_out_load_en  (w_bf_out_load_en),
        .i_mem_pinpong_sel (w_mem_pingpong_sel),
        .i_y0_data         (w_y0_data),
        .i_y1_data         (w_y1_data),
        .o_mem0_wdata_0    (w_mem0_wdata_0),
        .o_mem0_wdata_1    (w_mem0_wdata_1),
        .o_wdata_valid_0   (w_wdata_valid_0),
        .o_mem1_wdata_0    (w_mem1_wdata_0),
        .o_mem1_wdata_1    (w_mem1_wdata_1),
        .o_wdata_valid_1   (w_wdata_valid_1)
    );

    wire [6:0] w_addr_a;
    wire [6:0] w_addr_b;
    wire [5:0] w_tw_addr;

    address_generator U_ADDR_GEN (
        .i_bf_stage (w_bf_stage),
        .i_bf_count (w_bf_count),
        .i_sys_state(w_sys_state),
        .o_addr_a   (w_addr_a),
        .o_addr_b   (w_addr_b),
        .o_tw_addr  (w_tw_addr)
    );


    wire [FFT_DATA_BIT - 1:0] w_mem0_adata;
    wire [FFT_DATA_BIT - 1:0] w_mem1_adata;
    wire [FFT_DATA_BIT - 1:0] w_mem0_bdata;
    wire [FFT_DATA_BIT - 1:0] w_mem1_bdata;

    data_memory #(
        .FFT_W(FFT_DATA_BIT)
    ) U_DATA_MEM_0 (
        .clk      (clk),
        .rst      (rst),
        .w_en     (w_wdata_valid_0),
        .i_wdata_0(w_mem0_wdata_0),
        .i_wdata_1(w_mem0_wdata_1),
        .i_waddr  ({w_addr_a, w_addr_b}),
        .i_raddr  ({w_addr_a, w_addr_b}),
        .rdata_a  (w_mem0_adata),
        .rdata_b  (w_mem0_bdata)
    );

    data_memory #(
        .FFT_W(FFT_DATA_BIT)
    ) U_DATA_MEM_1 (
        .clk      (clk),
        .rst      (rst),
        .w_en     (w_wdata_valid_1),
        .i_wdata_0(w_mem1_wdata_0),
        .i_wdata_1(w_mem1_wdata_1),
        .i_waddr  ({w_addr_a, w_addr_b}),
        .i_raddr  ({w_addr_a, w_addr_b}),
        .rdata_a  (w_mem1_adata),
        .rdata_b  (w_mem1_bdata)
    );

    mem_read_path #(
        .FFT_W(FFT_DATA_BIT)
    ) U_MEM_RPATH (
        .clk             (clk),
        .rst             (rst),
        .i_rdata_load_en (w_rdata_load_en),
        .i_bf_mem_sel    (w_bf_mem_sel),
        .i_mem0_adata    (w_mem0_adata),
        .i_mem1_adata    (w_mem1_adata),
        .i_mem0_bdata    (w_mem0_bdata),
        .i_mem1_bdata    (w_mem1_bdata),
        .o_bf_adata      (w_bf_adata),
        .o_bf_bdata      (w_bf_bdata),
        .o_rdata_valid   (w_rdata_valid),
        .i_core_out_en   (w_core_out_en),
        .i_core_out_sel  (w_core_out_sel),
        .o_fft_core_data (o_fft_core_data),
        .o_fft_core_valid(o_fft_core_valid)
    );

    parameter COEFF_WIDTH = 16;
    wire [2*COEFF_WIDTH-1:0] twiddle_factor;

    twiddle_rom U_TWIDDLE_ROM(
        .clk(clk),
        .twiddle_addr(w_tw_addr),
        .twiddle_factor(twiddle_factor)
    );

    wire [39:0] y0_data;

    assign y0_data = {y0_re, y0_im};

    butterfly #(
        .DATA_WIDTH (20),
        .COEFF_WIDTH(16),
        .FRAC_BITS  (14),
        .T_WIDTH    (FFT_DATA_BIT / 2 + 1)
    )U_BUTTERFLY(
        .clk(clk),
        .rst(rst),
        .i_read_data_valid(w_rdata_valid),

        .a_re(a_re),
        .a_im(a_im),
        .b_re(b_re),
        .b_im(b_im),
        .twiddle_factor(twiddle_factor),

        .y0_re(y0_re),
        .y0_im(y0_im),
        .y1_re(y1_re),
        .y1_im(y1_im),
        .o_bf_out_valid(w_bf_out_valid)
    );
endmodule
