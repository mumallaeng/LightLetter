`timescale 1ns / 1ps

module MAC_unit (
    input  wire        clk,
    input  wire        rst_n,

    input  wire [47:0] win_row,
    input  wire [47:0] weight_row,
    // 곱셈기별 enable: mul_en[k] = 0 이면 k번째 곱 결과를 0으로 마스킹
    //   mul_en[0] -> win_row[15:0]  * weight_row[15:0]
    //   mul_en[1] -> win_row[31:16] * weight_row[31:16]
    //   mul_en[2] -> win_row[47:32] * weight_row[47:32]
    input  wire [2:0]  mul_en,

    output wire [35:0] psum_out
);

    // ---------------------------------------------------------
    // signed input
    // ---------------------------------------------------------
    wire signed [15:0] w0 = win_row[15:0];
    wire signed [15:0] w1 = win_row[31:16];
    wire signed [15:0] w2 = win_row[47:32];

    wire signed [15:0] g0 = weight_row[15:0];
    wire signed [15:0] g1 = weight_row[31:16];
    wire signed [15:0] g2 = weight_row[47:32];


    // ---------------------------------------------------------
    // Multiply
    // ---------------------------------------------------------
    wire signed [31:0] mul0 = w0 * g0;
    wire signed [31:0] mul1 = w1 * g1;
    wire signed [31:0] mul2 = w2 * g2;


    // ---------------------------------------------------------
    // Pipeline Register
    // Multiply -> FF -> Add
    // ---------------------------------------------------------
    reg signed [35:0] mul0_reg;
    reg signed [35:0] mul1_reg;
    reg signed [35:0] mul2_reg;


    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mul0_reg <= 36'sd0;
            mul1_reg <= 36'sd0;
            mul2_reg <= 36'sd0;
        end else begin
            mul0_reg <= mul_en[0] ? {{4{mul0[31]}}, mul0} : 36'sd0;
            mul1_reg <= mul_en[1] ? {{4{mul1[31]}}, mul1} : 36'sd0;
            mul2_reg <= mul_en[2] ? {{4{mul2[31]}}, mul2} : 36'sd0;
        end
    end


    // ---------------------------------------------------------
    // Add
    // ---------------------------------------------------------
    assign psum_out = mul0_reg + mul1_reg + mul2_reg;

endmodule