`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// 작성자: Jong.W.Park
// 모듈명: tx_frame_generator
//
// 설명:
//   다음 구조의 32비트 디지털 송신 프레임을 생성한다.
//
//     SFD(8) + Frame ID(8) + DATA(8) + CRC-8(8)
//
//   - SFD          : 8'hD5
//   - 비트 순서    : MSB 우선
//   - CRC 계산 대상: Frame ID + DATA
//   - CRC-8        : POLY=8'h07, INIT=8'h00
//
//   전송 전에 crc8 모듈로 CRC를 먼저 계산하고,
//   계산된 CRC를 포함한 완성된 32비트 프레임을 전송한다.
//
// 핸드셰이크 조건:
//   tx_bit_valid && tx_bit_ready
//
// 동작 참고:
//   - busy == 0인 대기 상태에서만 start 요청을 받는다.
//   - tx_bit_valid == 1이고 tx_bit_ready == 0이면 tx_bit 값을 유지한다.
//   - 첫 번째 SFD 비트가 유효해질 때 frame_start가 한 클록 동안 1이 된다.
//   - 마지막 CRC 비트의 전송이 끝나면 frame_done이 한 클록 동안 1이 된다.
//////////////////////////////////////////////////////////////////////////////////
module tx_frame_generator #(
    parameter [7:0] SFD = 8'hD5
) (
    input  wire       clk,
    input  wire       rst,
    // 프레임 전송 요청 및 입력 데이터
    input  wire       start,
    input  wire [7:0] frame_id,
    input  wire [7:0] data_in,
    // 직렬 비트 스트림 출력
    output wire       tx_bit,
    output wire       tx_bit_valid,
    input  wire       tx_bit_ready,
    // 프레임 생성기 상태 출력
    output reg        frame_start,
    output reg        frame_done,
    output reg        busy
);
    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_CRC_INIT = 3'd1;
    localparam [2:0] ST_CRC_ID = 3'd2;
    localparam [2:0] ST_CRC_DATA = 3'd3;
    localparam [2:0] ST_LOAD = 3'd4;
    localparam [2:0] ST_SEND = 3'd5;

    reg  [ 2:0] state;

    reg  [ 7:0] frame_id_reg;
    reg  [ 7:0] data_reg;

    reg  [31:0] frame_shift;
    reg  [ 5:0] bit_count;

    //=====================================
    // CRC-8 모듈 연결 신호
    //=====================================
    wire        crc_init;
    wire        crc_data_valid;
    wire [ 7:0] crc_data_in;
    wire [ 7:0] crc_out;

    // CRC 초기화 후 Frame ID와 DATA를 각각 한 클록씩 순서대로 입력한다.
    assign crc_init       = (state == ST_CRC_INIT);
    assign crc_data_valid = (state == ST_CRC_ID) || (state == ST_CRC_DATA);
    assign crc_data_in    = (state == ST_CRC_ID) ? frame_id_reg : data_reg;

    crc8 #(
        .POLY(8'h07),
        .INIT(8'h00)
    ) crc8 (
        .clk       (clk),
        .rst       (rst),
        .crc_init  (crc_init),
        .data_in   (crc_data_in),
        .data_valid(crc_data_valid),
        .crc_out   (crc_out)
    );
    //=====================================
    // 송신 비트 스트림
    //=====================================
    // 완성된 프레임의 최상위 비트부터 출력한다.
    // ready가 0이면 시프트하지 않으므로 현재 출력 비트가 그대로 유지된다.
    assign tx_bit       = frame_shift[31];
    assign tx_bit_valid = (state == ST_SEND);

    //=====================================
    // 프레임 생성 FSM
    //====================================
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state        <= ST_IDLE;
            frame_id_reg <= 8'h00;
            data_reg     <= 8'h00;
            frame_shift  <= 32'd0;
            bit_count    <= 6'd0;
            frame_start  <= 1'b0;
            frame_done   <= 1'b0;
            busy         <= 1'b0;
        end else begin
            frame_start <= 1'b0;
            frame_done  <= 1'b0;

            case (state)
                ST_IDLE: begin
                    busy <= 1'b0;
                    if (start) begin
                        // 전송 중 입력값이 바뀌어도 프레임이 변하지 않도록 저장한다.
                        frame_id_reg <= frame_id;
                        data_reg     <= data_in;
                        busy         <= 1'b1;
                        state        <= ST_CRC_INIT;
                    end
                end

                // 새 프레임의 CRC 누적값을 INIT으로 초기화한다.
                ST_CRC_INIT: begin
                    state <= ST_CRC_ID;
                end
                // Frame ID 8비트를 CRC에 먼저 반영한다.
                ST_CRC_ID: begin
                    state <= ST_CRC_DATA;
                end
                // DATA 8비트를 이어서 CRC에 반영한다.
                ST_CRC_DATA: begin
                    state <= ST_LOAD;
                end
                // 이 상태에서는 Frame ID와 DATA가 반영된 crc_out이 준비되어 있다.
                // SFD, Frame ID, DATA, CRC를 묶어 완성된 32비트 프레임을 만든다.
                ST_LOAD: begin
                    frame_shift <= {SFD, frame_id_reg, data_reg, crc_out};

                    // 31부터 0까지 현재 비트를 포함하여 총 32번 전송한다.
                    // 6비트로 선언했지만 값은 0~31만 사용하므로 최상위 비트는 항상 0이다.
                    bit_count   <= 6'd31;

                    // ST_SEND로 전환되면 첫 SFD 비트(frame_shift[31])가 유효해진다.
                    frame_start <= 1'b1;
                    state       <= ST_SEND;
                end
                // valid/ready 핸드셰이크가 성립할 때마다 한 비트씩 전송한다.
                ST_SEND: begin
                    if (tx_bit_ready) begin
                        if (bit_count == 6'd0) begin
                            // bit_count가 0일 때 현재 비트가 32번째 마지막 비트이다.
                            frame_done <= 1'b1;
                            busy       <= 1'b0;
                            state      <= ST_IDLE;
                        end else begin
                            // 다음 비트를 MSB 위치로 옮기고 카운터를 감소시킨다.
                            frame_shift <= {frame_shift[30:0], 1'b0};
                            bit_count   <= bit_count - 1'b1;
                        end
                    end
                end
                default: begin
                    state <= ST_IDLE;
                    busy  <= 1'b0;
                end
            endcase
        end
    end
endmodule
