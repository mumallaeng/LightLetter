`timescale 1ns / 1ps

module fft_top #(
    parameter ADC_BIT = 12,
    parameter FFT_DATA_BIT = 40
)(
    input  wire clk,
    input  wire rst,
    
    input  wire        vauxp14,
    input  wire        vauxn14
    // power output
    //output wire [39:0] power_result,
    //output wire        fft_mag_valid

    );
    wire [39:0] power_result;
    wire        fft_mag_valid;

    wire [15:0] xadc_do;
    wire [4:0]  xadc_channel;
    wire        xadc_eoc;
    wire        xadc_drdy;

    wire clk_100mhz;

    wire locked;
    reg [1:0] rst_pipe;

    always @(posedge clk_100mhz or posedge rst or negedge locked) begin
        if (rst || !locked)
            rst_pipe <= 2'b11;
        else
            rst_pipe <= {rst_pipe[0], 1'b0};
    end

    wire rst_100mhz = rst_pipe[1];

      clk_wiz_0 U_CLK_WIZ
   (
    // Clock out ports
    .clk_out1(clk_100mhz),     // output clk_out1
    // Status and control signals
    .reset(rst), // input reset
    .locked(locked),       // output locked
   // Clock in ports
    .clk_in1(clk));      // input clk_in1


    xadc_wiz_0 U_XADC_IP (
      .di_in      (16'd0),        // input wire [15 : 0] di_in
      .daddr_in   (7'h1E),        // input wire [6 : 0] daddr_in
      .den_in     (xadc_eoc),     // input wire den_in
      .dwe_in     (1'b0),         // input wire dwe_in
      .drdy_out   (xadc_drdy),    // output wire drdy_out
      .do_out     (xadc_do),      // output wire [15 : 0] do_out
      .dclk_in    (clk_100mhz),          // input wire dclk_in
      .reset_in   (rst_100mhz),          // input wire reset_in
      .vp_in      (1'b0),         // input wire vp_in
      .vn_in      (1'b0),         // input wire vn_in
      .vauxp14    (vauxp14),      // input wire vauxp14
      .vauxn14    (vauxn14),      // input wire vauxn14
      .channel_out(xadc_channel), // output wire [4 : 0] channel_out
      .eoc_out    (xadc_eoc),     // output wire eoc_out
      .alarm_out  (),  
      .eos_out    (),
      .busy_out   ()
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
	    .clk(clk_100mhz),
	    .rst(rst_100mhz),
	    .i_adc_data(xadc_do[15:4]),
	    .i_adc_data_valid(xadc_drdy && (xadc_channel == 5'h1E)),
	    .o_frame_valid(w_frame_valid),
	    .i_frame_ready(w_frame_ready),
	    .o_buf_data_valid(w_buf_data_valid),
	    .i_buf_data_ready(w_buf_data_ready),
	    .o_buf_data0(w_buf_data0),
	    .o_buf_data1(w_buf_data1),
	    .o_overflow()
);

    wire [19:0] w_fft_core_data_re;
    wire [19:0] w_fft_core_data_im;
    wire w_fft_core_valid;

    fft_core #(
        .FFT_DATA_BIT(FFT_DATA_BIT)
    ) U_FFT_CORE (
        .clk(clk_100mhz),
        .rst(rst_100mhz),
        .i_frame_valid(w_frame_valid),
        .o_frame_ready(w_frame_ready),
        .i_buf_data_valid(w_buf_data_valid),
        .o_buf_data_ready(w_buf_data_ready),
        .i_buf_data0(w_buf_data0),
        .i_buf_data1(w_buf_data1),
        .o_fft_core_data({w_fft_core_data_re,w_fft_core_data_im}),
        .o_fft_core_valid(w_fft_core_valid)
);

    fft_power U_FFT_POWER (
        .clk             (clk_100mhz),
        .rst             (rst_100mhz),
        .i_fft_core_data ({w_fft_core_data_re,w_fft_core_data_im}),
        .i_fft_core_valid(w_fft_core_valid),
        .power_result    (power_result),
        .fft_mag_valid   (fft_mag_valid)
    );

    ila_0 U_ILA (
    .clk   (clk_100mhz),
    .probe0(power_result),
    .probe1(fft_mag_valid)
    );
endmodule
