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
    output wire o_bf_read_data_valid
);

    /******************** Input registers ********************/

    reg signed [DATA_WIDTH-1:0] a_re_reg;
    reg signed [DATA_WIDTH-1:0] a_im_reg;
    reg signed [DATA_WIDTH-1:0] b_re_reg;
    reg signed [DATA_WIDTH-1:0] b_im_reg;
    reg [2*COEFF_WIDTH-1:0] twiddle_factor_reg;

    /******************** Multiplier results ********************/

    wire signed [T_WIDTH-1:0] t_re;
    wire signed [T_WIDTH-1:0] t_im;

    reg signed [T_WIDTH-1:0] t_re_reg;
    reg signed [T_WIDTH-1:0] t_im_reg;

    /******************** FSM states ********************/

    localparam [2:0] IDLE    = 3'd0,
                     WAIT_T  = 3'd2,
                     ADD_SUB = 3'd3,
                     DONE    = 3'd4;

    reg [2:0] state;

    /******************** Combinational multiplier ********************/

    multiplier #(
        .DATA_WIDTH (DATA_WIDTH),
        .COEFF_WIDTH(COEFF_WIDTH),
        .FRAC_BITS  (FRAC_BITS),
        .T_WIDTH    (T_WIDTH)
    ) u_multiplier (
        .b_re          (b_re_reg),
        .b_im          (b_im_reg),
        .twiddle_factor(twiddle_factor_reg),
        .t_re          (t_re),
        .t_im          (t_im)
    );

    /******************** Output valid ********************/

    assign o_bf_read_data_valid = (state == DONE);

    /******************** FSM and storage ********************/

    always @(posedge clk) begin
        if (rst) begin
            state <= IDLE;

            a_re_reg <= {DATA_WIDTH{1'b0}};
            a_im_reg <= {DATA_WIDTH{1'b0}};
            b_re_reg <= {DATA_WIDTH{1'b0}};
            b_im_reg <= {DATA_WIDTH{1'b0}};
            twiddle_factor_reg <= {(2*COEFF_WIDTH){1'b0}};

            t_re_reg <= {T_WIDTH{1'b0}};
            t_im_reg <= {T_WIDTH{1'b0}};

            y0_re <= {DATA_WIDTH{1'b0}};
            y0_im <= {DATA_WIDTH{1'b0}};
            y1_re <= {DATA_WIDTH{1'b0}};
            y1_im <= {DATA_WIDTH{1'b0}};
        end
        else begin
            case (state)
                IDLE: begin
                    if (i_read_data_valid) begin
                        // E0: A/B와 계수를 함께 저장
                        a_re_reg <= a_re;
                        a_im_reg <= a_im;
                        b_re_reg <= b_re;
                        b_im_reg <= b_im;
                        twiddle_factor_reg <= twiddle_factor;

                        state <= WAIT_T;
                    end
                end

                WAIT_T: begin
                    // E1: 한 클럭 동안 계산된 T를 저장
                    // 조합형 Multiplier이므로 mul_done은 없음
                    t_re_reg <= t_re;
                    t_im_reg <= t_im;

                    state <= ADD_SUB;
                end

                ADD_SUB: begin
                    // E2: A ± T를 저장하고 출력 valid 발생
                    y0_re <= a_re_reg + t_re_reg;
                    y0_im <= a_im_reg + t_im_reg;
                    y1_re <= a_re_reg - t_re_reg;
                    y1_im <= a_im_reg - t_im_reg;

                    state <= DONE;
                end

                DONE: begin
                    // E3: 출력 valid 해제, IDLE 복귀
                    state <= IDLE;
                end

                default: begin
                    state <= IDLE;
                end
            endcase
        end
    end

endmodule