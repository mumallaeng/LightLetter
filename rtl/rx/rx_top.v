`timescale 1ns / 1ps

// FFT Power 입력부터 CRC 검증까지 연결. UI buffer와 UART는 외부 연결.
module rx_top #(
    parameter MAG_W = 24,
    parameter SYNC_MIN_BLOCKS = 6
) (
    input wire clk,
    input wire rst_n,
    input wire [MAG_W-1:0] fft_mag,
    input wire fft_mag_valid,

    output wire [7:0] frame_id,
    output wire [7:0] data,
    output wire [7:0] received_crc,
    output wire frame_valid,
    output wire packet_error,
    output wire decoder_error,
    output wire crc_error,

    // 파형 확인용 출력
    output wire fft_block_done,
    output wire [1:0] block_code,
    output wire block_code_valid,
    output wire [1:0] symbol_code,
    output wire symbol_valid,
    output wire frame_start,
    output wire frame_abort,
    output wire frame_finish,
    output wire decode_valid
);
    wire [MAG_W-1:0] bin8_power, bin16_power, bin20_power;

    rx_bin_detector #(.MAG_W(MAG_W)) u_bin_detector (
        .clk(clk), .rst_n(rst_n), .fft_mag(fft_mag),
        .fft_mag_valid(fft_mag_valid), .bin8_power(bin8_power),
        .bin16_power(bin16_power), .bin20_power(bin20_power),
        .fft_block_done(fft_block_done)
    );

    rx_symbol_detector #(.MAG_W(MAG_W)) u_symbol_detector (
        .clk(clk), .rst_n(rst_n), .bin8_power(bin8_power),
        .bin16_power(bin16_power), .bin20_power(bin20_power),
        .fft_block_done(fft_block_done), .block_code(block_code),
        .block_code_valid(block_code_valid)
    );

    rx_symbol_sync #(.SYNC_MIN_BLOCKS(SYNC_MIN_BLOCKS)) u_symbol_sync (
        .clk(clk), .rst_n(rst_n), .block_code(block_code),
        .block_code_valid(block_code_valid), .frame_finish(frame_finish),
        .symbol_code(symbol_code), .symbol_valid(symbol_valid),
        .frame_start(frame_start), .frame_abort(frame_abort)
    );

    rx_frame_decoder u_frame_decoder (
        .clk(clk), .rst_n(rst_n), .frame_start(frame_start),
        .frame_abort(frame_abort), .symbol_code(symbol_code),
        .symbol_valid(symbol_valid), .frame_id(frame_id), .data(data),
        .received_crc(received_crc), .decode_valid(decode_valid),
        .frame_finish(frame_finish), .packet_error(decoder_error)
    );

    rx_crc8_check u_crc8_check (
        .clk(clk), .rst_n(rst_n), .frame_id(frame_id), .data(data),
        .received_crc(received_crc), .decode_valid(decode_valid),
        .frame_valid(frame_valid), .crc_error(crc_error)
    );

    assign packet_error = decoder_error | crc_error;
endmodule
