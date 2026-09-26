`timescale 1ns / 1ps

// One-entry mailbox between the PL receiver and the PS.
//
// When frame_valid is asserted, the decoded packet is stored and
// packet_ready remains high until the PS asserts packet_clear.
// clk must be the same clock used by the AXI GPIO interface.
module rx_latch (
    input  wire        clk,
    input  wire        rst_n,

    // Decoded packet from rx_top.
    input  wire [7:0]  frame_id,
    input  wire [7:0]  data,
    input  wire [7:0]  received_crc,
    input  wire        frame_valid,
    input  wire        packet_error,

    // One-clock pulse from the PS through AXI GPIO channel 2.
    input  wire        packet_clear,

    // Connect packet_word to AXI GPIO channel 1.
    output wire [31:0] packet_word,
    output wire        packet_ready,
    output wire        packet_overrun
);
    reg [7:0] frame_id_reg;
    reg [7:0] data_reg;
    reg [7:0] received_crc_reg;
    reg       packet_error_reg;
    reg       packet_ready_reg;
    reg       packet_overrun_reg;

    assign packet_ready   = packet_ready_reg;
    assign packet_overrun = packet_overrun_reg;

    // AXI GPIO channel 1 bit layout:
    // [31]    packet_ready
    // [30]    packet_error
    // [29]    packet_overrun
    // [28:24] reserved
    // [23:16] frame_id
    // [15:8]  data
    // [7:0]   received_crc
    assign packet_word = {
        packet_ready_reg,
        packet_error_reg,
        packet_overrun_reg,
        5'b00000,
        frame_id_reg,
        data_reg,
        received_crc_reg
    };

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            frame_id_reg       <= 8'h00;
            data_reg           <= 8'h00;
            received_crc_reg   <= 8'h00;
            packet_error_reg   <= 1'b0;
            packet_ready_reg   <= 1'b0;
            packet_overrun_reg <= 1'b0;
        end else begin
            // A new valid packet has priority over packet_clear.
            if (frame_valid) begin
                frame_id_reg     <= frame_id;
                data_reg         <= data;
                received_crc_reg <= received_crc;
                packet_error_reg <= packet_error;
                packet_ready_reg <= 1'b1;

                // Report that the previous packet was overwritten before
                // the PS acknowledged it.
                if (packet_ready_reg && !packet_clear)
                    packet_overrun_reg <= 1'b1;
                else
                    packet_overrun_reg <= 1'b0;
            end else if (packet_clear) begin
                // Keep the packet bytes stable; only mark the mailbox empty.
                packet_ready_reg   <= 1'b0;
                packet_overrun_reg <= 1'b0;
            end
        end
    end
endmodule
