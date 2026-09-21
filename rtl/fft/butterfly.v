`timescale 1ns / 1ps

module butterfly #(
    parameter integer DATA_WIDTH  = 20,
    parameter integer COEFF_WIDTH = 16,
    parameter integer FRAC_BITS   = 14,
    parameter integer T_WIDTH     = DATA_WIDTH
)(
    input wire clk,
    input wire rst,
    input wire i_read_data_valid,

    input wire signed [DATA_WIDTH-1:0] a_re,
    input wire signed [DATA_WIDTH-1:0] a_im,
    input wire signed [DATA_WIDTH-1:0] b_re,
    input wire signed [DATA_WIDTH-1:0] b_im,
    input wire [2*COEFF_WIDTH-1:0] twiddle_factor,

    output wire signed [DATA_WIDTH-1:0] y0_re,
    output wire signed [DATA_WIDTH-1:0] y0_im,
    output wire signed [DATA_WIDTH-1:0] y1_re,
    output wire signed [DATA_WIDTH-1:0] y1_im,
    output wire o_bf_out_valid
);

    wire signed [T_WIDTH-1:0] t_re, t_im;

    // 현재 B/W를 직접 받아 조합 연산
    multiplier #(
        .DATA_WIDTH (DATA_WIDTH),
        .COEFF_WIDTH(COEFF_WIDTH),
        .FRAC_BITS  (FRAC_BITS),
        .T_WIDTH    (T_WIDTH)
    ) u_multiplier (
        .b_re          (b_re),
        .b_im          (b_im),
        .twiddle_factor(twiddle_factor),
        .t_re          (t_re),
        .t_im          (t_im)
    );

    // 출력 저장 없이 외부로 전달
    assign y0_re = a_re + t_re;
    assign y0_im = a_im + t_im;
    assign y1_re = a_re - t_re;
    assign y1_im = a_im - t_im;

    // Only the valid-control state is stored; A/B/W and Y are NOT stored.
    // E0: request accepted in IDLE. E1: enter DONE, valid rises.
    // E2: external receiver samples Y/valid; return to IDLE.
    // Upper logic must hold A/B/W until external result capture is complete.
    localparam [2:0] IDLE=3'd0, WAIT_CALC=3'd1, DONE=3'd4;
    reg [2:0] state;

    assign o_bf_out_valid = (state == DONE);

    always @(posedge clk) begin
        if (rst) begin
            state <= IDLE;
        end else begin
            case (state)
                IDLE: begin
                    if (i_read_data_valid)
                        state <= WAIT_CALC;
                end
                WAIT_CALC: state <= DONE;
                DONE: state <= IDLE;
                default: state <= IDLE;
            endcase
        end
    end
endmodule
