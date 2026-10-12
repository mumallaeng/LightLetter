`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// 작성자: Jong.W.Park
// 모듈명: crc8
//
// 설명:
//   입력 바이트를 MSB부터 처리하는 CRC-8 계산기이다.
//   조합 논리로 8비트를 한 번에 계산하므로 유효한 바이트 하나를
//   클록 한 주기마다 CRC 누적값에 반영할 수 있다.
//
// CRC 사양:
//   생성 다항식 : 8'h07
//   초기값      : 8'h00
//   XOROUT     : 8'h00
//   REFIN      : false
//   REFOUT     : false
//   비트 순서   : MSB 우선
//
// 사용 순서:
//   1. 새 프레임 계산 전에 crc_init을 한 클록 동안 1로 만든다.
//   2. 계산할 한 바이트를 data_in에 넣는다.
//   3. data_valid를 한 클록 동안 1로 만든다.
//   4. 유효한 바이트가 들어올 때마다 crc_out이 갱신된다.
//
// 계산 예:
//   Frame ID = 8'h00
//   DATA     = 8'h41
//   결과     = 8'hC0
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

    // 0단계의 현재 CRC부터 입력 8비트를 모두 반영한 8단계 결과까지
    // 9개의 8비트 값을 하나의 72비트 벡터로 연결한다.
    wire [71:0] crc_stage;

    // 첫 단계는 이전 클록까지 누적된 CRC 값이다.
    assign crc_stage[7:0] = crc_out;

    genvar i;
    generate
        for (i = 0; i < 8; i = i + 1) begin : GEN_CRC_STAGE
            wire [7:0] current_crc;
            wire       feedback;

            assign current_crc = crc_stage[(i*8)+:8];

            // data_in[7]부터 data_in[0]까지 MSB 우선 순서로 처리한다.
            // feedback이 1이면 왼쪽 시프트한 CRC에 생성 다항식을 XOR한다.
            assign feedback = current_crc[7] ^ data_in[7-i];
            assign crc_stage[((i+1)*8)+:8] = feedback?((current_crc<<1)^POLY):(current_crc<<1);
        end
    endgenerate

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            // 전체 모듈 리셋
            crc_out <= INIT;
        end else if (crc_init) begin
            // 새 프레임 계산을 시작하기 전 CRC 누적값 초기화
            crc_out <= INIT;
        end else if (data_valid) begin
            // 입력 바이트 8비트를 모두 반영한 최종 단계의 값을 저장한다.
            crc_out <= crc_stage[71:64];
        end
    end
endmodule
