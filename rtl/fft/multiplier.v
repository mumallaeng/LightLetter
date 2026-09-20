`timescale 1ns / 1ps

module multiplier #(
    parameter integer DATA_WIDTH  = 20,
    parameter integer COEFF_WIDTH = 16,
    parameter integer FRAC_BITS   = 14,
    parameter integer T_WIDTH     = DATA_WIDTH + 1
)(
    input  wire signed [DATA_WIDTH-1:0] b_re,
    input  wire signed [DATA_WIDTH-1:0] b_im,
    input  wire [2*COEFF_WIDTH-1:0] twiddle_factor,

    output wire signed [T_WIDTH-1:0] t_re,
    output wire signed [T_WIDTH-1:0] t_im
);

    localparam integer PROD_WIDTH = DATA_WIDTH + COEFF_WIDTH;
    localparam integer RAW_WIDTH  = PROD_WIDTH + 1;

    // Packed coefficient: {real, imaginary}.
    wire signed [COEFF_WIDTH-1:0] w_re;
    wire signed [COEFF_WIDTH-1:0] w_im;

    assign w_re = $signed(
        twiddle_factor[2*COEFF_WIDTH-1:COEFF_WIDTH]
    );
    assign w_im = $signed(
        twiddle_factor[COEFF_WIDTH-1:0]
    );

    // Full-width signed products.
    wire signed [PROD_WIDTH-1:0] p0, p1, p2, p3;

    assign p0 = b_re * w_re;
    assign p1 = b_im * w_im;
    assign p2 = b_re * w_im;
    assign p3 = b_im * w_re;

    // Extend each product before addition/subtraction.
    wire signed [RAW_WIDTH-1:0] raw_re, raw_im;

    assign raw_re =
        $signed({p0[PROD_WIDTH-1], p0})
      - $signed({p1[PROD_WIDTH-1], p1});

    assign raw_im =
        $signed({p2[PROD_WIDTH-1], p2})
      + $signed({p3[PROD_WIDTH-1], p3});

    // Remove the coefficient's fractional bits.
    assign t_re = raw_re >>> FRAC_BITS;
    assign t_im = raw_im >>> FRAC_BITS;

endmodule