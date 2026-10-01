`timescale 1ns / 1ps

`timescale 1ns / 1ps

module fft_power (
    input  wire        clk,
    input  wire        rst,
    input  wire [39:0] i_fft_core_data,
    input  wire        i_fft_core_valid,
    output reg  [39:0] power_result,
    output reg         fft_mag_valid
);

    wire signed [19:0] fft_re, fft_im;
    wire signed [39:0] re_square, im_square;
    wire        [40:0] power_sum;

    assign fft_re = $signed(i_fft_core_data[39:20]);
    assign fft_im = $signed(i_fft_core_data[19:0]);

    // Combinational sum-of-squares calculation
    assign re_square = fft_re * fft_re;
    assign im_square = fft_im * fft_im;
    assign power_sum = {1'b0, re_square} + {1'b0, im_square};

    // Register the result and valid flag on the clock edge
    always @(posedge clk) begin
        if (rst) begin
            power_result  <= 40'd0;
            fft_mag_valid <= 1'b0;
        end else begin
            fft_mag_valid <= i_fft_core_valid;

            if (i_fft_core_valid)
                power_result <= power_sum[39:0];
        end
    end

endmodule
