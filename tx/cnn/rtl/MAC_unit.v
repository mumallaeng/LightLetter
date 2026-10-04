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
    // Operand Register
    // Operands -> FF -> Multiply (DSP A/B input registers)
    // ---------------------------------------------------------
    reg signed [15:0] w0_reg, w1_reg, w2_reg;
    reg signed [15:0] g0_reg, g1_reg, g2_reg;
    reg               row_valid_reg;

    always @(posedge clk) begin
        if (!rst_n) begin
            w0_reg <= 16'sd0;
            w1_reg <= 16'sd0;
            w2_reg <= 16'sd0;
            g0_reg <= 16'sd0;
            g1_reg <= 16'sd0;
            g2_reg <= 16'sd0;

            row_valid_reg <= 1'b0;
        end
        else begin
            w0_reg <= w0;
            w1_reg <= w1;
            w2_reg <= w2;
            g0_reg <= g0;
            g1_reg <= g1;
            g2_reg <= g2;

            row_valid_reg <= row_valid;
        end
    end


    // ---------------------------------------------------------
    // Multiply
    // ---------------------------------------------------------
    wire signed [31:0] mul0 = w0_reg * g0_reg;
    wire signed [31:0] mul1 = w1_reg * g1_reg;
    wire signed [31:0] mul2 = w2_reg * g2_reg;


    // ---------------------------------------------------------
    // Pipeline Register
    // Multiply -> FF -> Add
    // ---------------------------------------------------------
    reg signed [35:0] mul0_reg;
    reg signed [35:0] mul1_reg;
    reg signed [35:0] mul2_reg;

    reg               valid_reg;


    always @(posedge clk) begin
        if (!rst_n) begin
            mul0_reg <= 36'sd0;
            mul1_reg <= 36'sd0;
            mul2_reg <= 36'sd0;

            valid_reg <= 1'b0;
        end
        else begin
            if (row_valid_reg) begin
                mul0_reg <= {{4{mul0[31]}}, mul0};
                mul1_reg <= {{4{mul1[31]}}, mul1};
                mul2_reg <= {{4{mul2[31]}}, mul2};
            end
            else begin
                mul0_reg <= 36'sd0;
                mul1_reg <= 36'sd0;
                mul2_reg <= 36'sd0;
            end

            valid_reg <= row_valid_reg;
        end
    end


    // ---------------------------------------------------------
    // Product Register
    // Multiply -> FF (DSP M) -> FF (DSP P) -> Add
    // ---------------------------------------------------------
    reg signed [35:0] mul0_p;
    reg signed [35:0] mul1_p;
    reg signed [35:0] mul2_p;

    reg               valid_p;

    always @(posedge clk) begin
        if (!rst_n) begin
            mul0_p <= 36'sd0;
            mul1_p <= 36'sd0;
            mul2_p <= 36'sd0;

            valid_p <= 1'b0;
        end
        else begin
            mul0_p <= mul0_reg;
            mul1_p <= mul1_reg;
            mul2_p <= mul2_reg;

            valid_p <= valid_reg;
        end
    end


    // ---------------------------------------------------------
    // Add (fabric adder, so no unregistered DSP adder is inferred)
    // ---------------------------------------------------------
    (* use_dsp = "no" *) wire signed [35:0] mul_sum = mul0_p + mul1_p + mul2_p;

    always @(*) begin
        if (!valid_p) begin
            psum_out  = 36'sd0;
            valid_out = 1'b0;
        end
        else begin
            psum_out  = mul_sum;
            valid_out = 1'b1;
        end
    end

endmodule