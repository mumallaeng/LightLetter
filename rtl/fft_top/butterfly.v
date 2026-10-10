`timescale 1ns / 1ps

module butterfly #(
    parameter integer DATA_WIDTH  = 20,
    parameter integer COEFF_WIDTH = 16,
    parameter integer FRAC_BITS   = 14,
    parameter integer T_WIDTH     = DATA_WIDTH + 1
)(
    input wire clk,
    input wire rst,
    input wire i_read_data_valid,

    input wire signed [DATA_WIDTH-1:0] a_re,
    input wire signed [DATA_WIDTH-1:0] a_im,
    input wire signed [DATA_WIDTH-1:0] b_re,
    input wire signed [DATA_WIDTH-1:0] b_im,
    input wire [2*COEFF_WIDTH-1:0] twiddle_factor,

    output reg signed [DATA_WIDTH-1:0] y0_re,
    output reg signed [DATA_WIDTH-1:0] y0_im,
    output reg signed [DATA_WIDTH-1:0] y1_re,
    output reg signed [DATA_WIDTH-1:0] y1_im,
    output wire o_bf_out_valid
);
    // Pipeline stage 0: register input and twiddle together.
    reg signed [DATA_WIDTH-1:0] a_re_s0, a_im_s0, b_re_s0, b_im_s0;
    reg [2*COEFF_WIDTH-1:0] twiddle_s0;
    reg valid_s0;

    // Pipeline stage 1: complex multiplication and delay A by one cycle.
    wire signed [T_WIDTH-1:0] t_re_comb, t_im_comb;
    reg signed [T_WIDTH-1:0] t_re_s1, t_im_s1;
    reg signed [DATA_WIDTH-1:0] a_re_s1, a_im_s1;
    reg valid_s1;

    // Pipeline stage 2: add / subtract, register results and valid.
    reg valid_s2;
    assign o_bf_out_valid = valid_s2;

    multiplier #(
        .DATA_WIDTH (DATA_WIDTH),
        .COEFF_WIDTH(COEFF_WIDTH),
        .FRAC_BITS  (FRAC_BITS),
        .T_WIDTH    (T_WIDTH)
    ) u_multiplier (
        .b_re          (b_re_s0),
        .b_im          (b_im_s0),
        .twiddle_factor(twiddle_s0),
        .t_re          (t_re_comb),
        .t_im          (t_im_comb)
    );

    always @(posedge clk) begin
        if (rst) begin
            valid_s0 <= 1'b0;
            valid_s1 <= 1'b0;
            valid_s2 <= 1'b0;

            a_re_s0 <= 0; 
            a_im_s0 <= 0;
            b_re_s0 <= 0; 
            b_im_s0 <= 0;
            twiddle_s0 <= 0;

            a_re_s1 <= 0; 
            a_im_s1 <= 0;
            t_re_s1 <= 0; 
            t_im_s1 <= 0;

            y0_re <= 0; 
            y0_im <= 0;
            y1_re <= 0; 
            y1_im <= 0;
        end else begin
            valid_s0 <= i_read_data_valid;
            a_re_s0 <= a_re;
            a_im_s0 <= a_im;
            b_re_s0 <= b_re;
            b_im_s0 <= b_im;
            twiddle_s0 <= twiddle_factor;

            valid_s1 <= valid_s0;
            a_re_s1 <= a_re_s0;
            a_im_s1 <= a_im_s0;
            t_re_s1 <= t_re_comb;
            t_im_s1 <= t_im_comb;

            valid_s2 <= valid_s1;
            y0_re <= a_re_s1 + t_re_s1;
            y0_im <= a_im_s1 + t_im_s1;
            y1_re <= a_re_s1 - t_re_s1;
            y1_im <= a_im_s1 - t_im_s1;
        end
    end
endmodule