`timescale 1ps / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: bfsk_mapper
// Date : 2026.09.16
//////////////////////////////////////////////////////////////////////////////////

module bfsk_mapper #(
    parameter [1:0] SYMBOL_IDLE = 2'b00,
    parameter [1:0] SYMBOL_BIT0 = 2'b01,
    parameter [1:0] SYMBOL_BIT1 = 2'b10,
    parameter [1:0] SYMBOL_SYNC = 2'b11
) (
    //global_signals
    input  wire       clk,
    input  wire       rst,
    //Frame Generator -> BFSK Mapper
    input  wire       tx_bit,
    input  wire       tx_bit_valid,
    output wire       tx_bit_ready,
    //TX FSM -> BFSK Mapper
    input  wire       sync_valid,
    output wire       sync_ready,
    //BFSK Mapper -> Carrier Generator
    output reg  [1:0] symbol_type,
    output reg        symbol_valid,
    output reg        symbol_start,
    input  wire       symbol_done
);
    reg active;

    assign sync_ready   = ~active;

    assign tx_bit_ready = (~active) && (~sync_valid);

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            active       <= 1'b0;
            symbol_type  <= SYMBOL_IDLE;
            symbol_valid <= 1'b0;
            symbol_start <= 1'b0;
        end else begin
            symbol_start <= 1'b0;

            if (!active) begin
                symbol_valid <= 1'b0;
                symbol_type  <= SYMBOL_IDLE;

                if (sync_valid) begin
                    active       <= 1'b1;
                    symbol_type  <= SYMBOL_SYNC;
                    symbol_valid <= 1'b1;
                    symbol_start <= 1'b1;
                end else if (tx_bit_valid) begin
                    active       <= 1'b1;
                    symbol_type  <= tx_bit ? SYMBOL_BIT1 : SYMBOL_BIT0;
                    symbol_valid <= 1'b1;
                    symbol_start <= 1'b1;
                end
            end else begin
                if (symbol_done) begin
                    active       <= 1'b0;
                    symbol_type  <= SYMBOL_IDLE;
                    symbol_valid <= 1'b0;
                end
            end
        end
    end
endmodule
