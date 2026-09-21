`timescale 1ns / 1ps

module fft_top #(
    parameter ADC_BIT = 12,
    parameter FFT_DATA_BIT = 40
)(
    input  wire clk,
    input  wire rst,
    // buffer input
    input  wire [     ADC_BIT - 1:0] i_adc_data,
    input  wire                 i_adc_data_valid,
    // core output
    output wire [FFT_DATA_BIT - 1:0] o_fft_core_data,
    output wire                      o_fft_core_valid

    );

    wire                      w_frame_valid;
    wire                      w_frame_ready;
    wire                      w_buf_data_valid;
    wire                      w_buf_data_ready;
    wire [FFT_DATA_BIT - 1:0] w_buf_data0;
    wire [FFT_DATA_BIT - 1:0] w_buf_data1;

    fft_buffer #(
	    .ADC_BIT(ADC_BIT),
	    .FFT_W  (FFT_DATA_BIT)
    ) U_FFT_BUFFER (
	    .clk(clk),
	    .rst(rst),
	    .i_adc_data(i_adc_data),
	    .i_adc_data_valid(i_adc_data_valid),
	    .o_frame_valid(w_frame_valid),
	    .i_frame_ready(w_frame_ready),
	    .o_buf_data_valid(w_buf_data_valid),
	    .i_buf_data_ready(w_buf_data_ready),
	    .o_buf_data0(w_buf_data0),
	    .o_buf_data1(w_buf_data1),
	    .o_overflow()
);

    fft_core #(
        .FFT_DATA_BIT(FFT_DATA_BIT)
    ) U_FFT_CORE (
        .clk(clk),
        .rst(rst),
        .i_frame_valid(w_frame_valid),
        .o_frame_ready(w_frame_ready),
        .i_buf_data_valid(w_buf_data_valid),
        .o_buf_data_ready(w_buf_data_ready),
        .i_buf_data0(w_buf_data0),
        .i_buf_data1(w_buf_data1),
        .o_fft_core_data(o_fft_core_data),
        .o_fft_core_valid(o_fft_core_valid)
);
endmodule
