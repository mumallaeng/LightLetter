//`timescale 1ns / 1ps
//
//module MAC_unit (
//    input  wire        clk,       
//    input  wire        rst_n,     
//    input  wire [47:0] win_row,
//    input  wire [47:0] weight_row,
//    input  wire        row_valid,
//
//    output reg  [35:0] psum_out,
//    output reg         valid_out
//);
//
//wire signed [15:0] w0 = win_row[15:0];
//wire signed [15:0] w1 = win_row[31:16];
//wire signed [15:0] w2 = win_row[47:32];
//
//wire signed [15:0] g0 = weight_row[15:0];
//wire signed [15:0] g1 = weight_row[31:16];
//wire signed [15:0] g2 = weight_row[47:32];
//
//wire signed [35:0] mul0 = w0 * g0;
//wire signed [35:0] mul1 = w1 * g1;
//wire signed [35:0] mul2 = w2 * g2;
//
//always @(*) begin
//    if (!row_valid) begin
//        psum_out  = 36'd0;
//        valid_out = 1'b0;
//    end
//    else begin
//        psum_out  = mul0 + mul1 + mul2;
//        valid_out = 1'b1;
//    end
//end
//
//endmodule

`timescale 1ns / 1ps

module MAC_unit (
    input  wire        clk,
    input  wire        rst_n,

    input  wire [47:0] win_row,
    input  wire [47:0] weight_row,
    input  wire        row_valid,

    output reg  [35:0] psum_out,
    output reg         valid_out
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

    reg               valid_reg;


    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mul0_reg <= 36'sd0;
            mul1_reg <= 36'sd0;
            mul2_reg <= 36'sd0;

            valid_reg <= 1'b0;
        end
        else begin
            if (row_valid) begin
                mul0_reg <= {{4{mul0[31]}}, mul0};
                mul1_reg <= {{4{mul1[31]}}, mul1};
                mul2_reg <= {{4{mul2[31]}}, mul2};
            end
            else begin
                mul0_reg <= 36'sd0;
                mul1_reg <= 36'sd0;
                mul2_reg <= 36'sd0;
            end

            valid_reg <= row_valid;
        end
    end


    // ---------------------------------------------------------
    // Add
    // ---------------------------------------------------------
    always @(*) begin
        if (!valid_reg) begin
            psum_out  = 36'sd0;
            valid_out = 1'b0;
        end
        else begin
            psum_out  = mul0_reg + mul1_reg + mul2_reg;
            valid_out = 1'b1;
        end
    end

endmodule