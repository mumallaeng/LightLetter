`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: crc8
//
// Description:
// CRC-8 calculator
//
// CRC Specification:
//   Polynomial : 8'h07
//   Init       : 8'h00
//   XOROUT     : 8'h00
//   REFIN      : false
//   REFOUT     : false
//   Bit Order  : MSB First
//
// Usage:
//   1. Assert crc_init for one clock before a new frame.
//   2. Present one byte on data_in.
//   3. Assert data_valid for one clock.
//   4. crc_out is updated after each valid byte.
//
// Example:
//   Frame ID = 8'h00
//   DATA     = 8'h41
//   Result   = 8'hC0
//////////////////////////////////////////////////////////////////////////////////
module crc8 #(
    parameter [7:0] POLY = 8'h07,
    parameter [7:0] INIT = 8'h00
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       crc_init,
    input  wire [7:0] data_in,
    input  wire       data_valid,
    output reg  [7:0] crc_out
);

    wire [71:0] crc_stage;
    assign crc_stage[7:0] = crc_out;

    genvar i;
    generate
        for (i = 0; i < 8; i = i + 1) begin : GEN_CRC_STAGE
            wire [7:0] current_crc;
            wire       feedback;

            assign current_crc = crc_stage[(i*8)+:8];

            assign feedback = current_crc[7] ^ data_in[7-i];
            assign crc_stage[((i+1)*8)+:8] = feedback?((current_crc<<1)^POLY):(current_crc<<1);
        end
    endgenerate

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            crc_out <= INIT;
        end else if (crc_init) begin
            crc_out <= INIT;
        end else if (data_valid) begin
            crc_out <= crc_stage[71:64];
        end
    end
endmodule
