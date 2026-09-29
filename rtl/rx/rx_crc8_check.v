`timescale 1ns / 1ps

module rx_crc8_check (
    input wire clk,
    input wire rst_n,

    // rx_frame_decoder 출력: decode_valid와 함께 유효
    input wire [7:0] frame_id,
    input wire [7:0] data,
    input wire [7:0] received_crc,
    input wire       decode_valid,

    // CRC 비교 결과: 각각 1클럭 pulse
    output reg frame_valid,
    output reg crc_error
);
    localparam [7:0] CRC_POLY = 8'h07;
    localparam [7:0] CRC_INIT = 8'h00;

    // 기존 CRC에 한 바이트를 MSB first로 반영
    function [7:0] crc8_byte;
        input [7:0] crc_in;
        input [7:0] byte_in;

        reg [7:0] crc;
        integer i;
        begin
            crc = crc_in;
            for (i = 7; i >= 0; i = i - 1) begin
                if (crc[7] ^ byte_in[i])
                    crc = {crc[6:0], 1'b0} ^ CRC_POLY;
                else
                    crc = {crc[6:0], 1'b0};
            end
            crc8_byte = crc;
        end
    endfunction

    wire [7:0] crc_after_id;
    wire [7:0] calculated_crc;

    // SFD와 수신 CRC는 제외하고 FRAME_ID → DATA 순서로 계산
    assign crc_after_id   = crc8_byte(CRC_INIT, frame_id);
    assign calculated_crc = crc8_byte(crc_after_id, data);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            frame_valid <= 1'b0;
            crc_error   <= 1'b0;
        end
        else begin
            frame_valid <= 1'b0;
            crc_error   <= 1'b0;

            if (decode_valid) begin
                if (calculated_crc == received_crc)
                    frame_valid <= 1'b1;
                else
                    crc_error <= 1'b1;
            end
        end
    end
endmodule
