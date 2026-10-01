`timescale 1ps / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tx_fsm
//설명:
//   BFSK TX 전체 송신 순서를 제어하는 상위 FSM이다.
//
// 송신 순서:
//   IDLE
//     ↓ Character Handshake
//   LOAD_DATA
//     ↓
//   PREAMBLE
//     ↓ SYNC Symbol x 4
//   SEND_FRAME
//     ↓ SFD + Frame ID + DATA + CRC
//   FRAME_DONE
//     ↓
//   IDLE
//
// 주요 동작:
//   - char_valid && char_ready에서 Character ID를 저장한다.
//   - Preamble 구간에서 sync_valid / sync_ready Handshake로
//     SYNC Symbol을 정확히 PREAMBLE_SYMBOLS회 요청한다.
//   - 각 SYNC Symbol은 symbol_done을 확인한 뒤 다음 SYNC를 요청한다.
//   - 마지막 SYNC 완료 후 frame_gen_start를 1 Clock Pulse로 발생시킨다.
//   - frame_done 수신 후 Frame ID를 1 증가시키고 IDLE로 복귀한다.
//   - Frame ID는 8-bit이므로 8'hFF 다음 자동으로 8'h00으로 Roll-over한다.
//
// 인터페이스 기준:
//   - 실제 bfsk_mapper.v의 sync_valid / sync_ready Handshake를 사용한다.
//   - frame_gen_start는 tx_frame_generator.v의 start 포트에 연결한다.
//
// 주의:
//   - Preamble 주파수/시간 생성 자체는 Mapper/Carrier Generator가 담당한다.
//   - 본 모듈은 Symbol의 "순서와 횟수"만 제어한다. 
//////////////////////////////////////////////////////////////////////////////////

module tx_fsm #(
    parameter integer PREAMBLE_SYMBOLS = 4
) (
    //global signals
    input  wire       clk,
    input  wire       rst,
    //CNN -> TX FSM
    input  wire [7:0] char_id,
    input  wire       char_valid,
    output reg        char_ready,
    output reg        tx_busy,
    //TX FSM -> Frame Generator
    output reg  [7:0] latched_char_id,
    output reg  [7:0] frame_id,
    output reg        frame_gen_start,
    input  wire       frame_done,
    //TX FSM -> BFSK Mapper
    output reg        sync_valid,
    input  wire       sync_ready,
    //Carrier Generator -> TX FSM
    input  wire       symbol_done
);
    //==============================================
    //FSM Status
    //==============================================
    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_LOAD_DATA = 3'd1;
    localparam [2:0] ST_PREAMBLE = 3'd2;
    localparam [2:0] ST_SEND_FRAME = 3'd3;
    localparam [2:0] ST_FRAME_DONE = 3'd4;

    reg [2:0] state;
    //completed Preamble SYNC Symbol count
    reg [7:0] preamble_count;
    //wait symbol handshake complete
    reg       sync_inflight;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state           <= ST_IDLE;
            //
            char_ready      <= 1'b1;
            tx_busy         <= 1'b0;
            //
            latched_char_id <= 8'h00;
            frame_id        <= 8'h00;
            //
            frame_gen_start <= 1'b0;
            //
            sync_valid      <= 1'b0;
            sync_inflight   <= 1'b0;
            preamble_count  <= 8'd0;
        end else begin
            //Default Pulse output = Acitve Low
            frame_gen_start <= 1'b0;
            case (state)
                //================================================
                //Wait input New Character 
                //================================================
                ST_IDLE: begin
                    char_ready     <= 1'b1;
                    tx_busy        <= 1'b0;

                    sync_valid     <= 1'b0;
                    sync_inflight  <= 1'b0;
                    preamble_count <= 8'd0;

                    if (char_valid && char_ready) begin
                        latched_char_id <= char_id;

                        char_ready      <= 1'b0;
                        tx_busy         <= 1'b1;

                        state           <= ST_LOAD_DATA;
                    end
                end
                //================================================
                //Preamble Start when Latched data stable
                //================================================
                ST_LOAD_DATA: begin
                    char_ready     <= 1'b0;
                    tx_busy        <= 1'b1;


                    sync_valid     <= 1'b0;
                    sync_inflight  <= 1'b0;
                    preamble_count <= 8'd0;

                    state          <= ST_PREAMBLE;
                end
                //================================================
                //Request 25KHz SYNC Symbol exactly N-times
                //================================================
                ST_PREAMBLE: begin
                    char_ready <= 1'b0;
                    tx_busy    <= 1'b1;

                    if (!sync_inflight) begin
                        if (!sync_valid) begin
                            sync_valid <= 1'b1;
                        end else if (sync_valid && sync_ready) begin
                            sync_valid    <= 1'b0;
                            sync_inflight <= 1'b1;
                        end
                    end else begin
                        sync_valid <= 1'b0;

                        if (symbol_done) begin
                            sync_inflight <= 1'b0;

                            if (preamble_count == PREAMBLE_SYMBOLS - 1) begin
                                preamble_count  <= 8'd0;
                                frame_gen_start <= 1'b1;
                                state           <= ST_SEND_FRAME;
                            end else begin
                                preamble_count <= preamble_count + 1'b1;
                            end
                        end
                    end
                end
                //===========================================================
                // wait 32bit frame transaction complete @ frame geanerator
                //===========================================================
                ST_SEND_FRAME: begin
                    char_ready    <= 1'b0;
                    tx_busy       <= 1'b1;

                    sync_valid    <= 1'b0;
                    sync_inflight <= 1'b0;

                    if (frame_done) begin
                        state <= ST_FRAME_DONE;
                    end
                end
                ST_FRAME_DONE: begin
                    frame_id      <= frame_id + 1'b1;

                    char_ready    <= 1'b1;
                    tx_busy       <= 1'b0;

                    sync_valid    <= 1'b0;
                    sync_inflight <= 1'b0;

                    state         <= ST_IDLE;
                end
                default: begin
                    state           <= ST_IDLE;

                    char_ready      <= 1'b1;
                    tx_busy         <= 1'b0;

                    sync_valid      <= 1'b0;
                    sync_inflight   <= 1'b0;
                    preamble_count  <= 8'd0;

                    frame_gen_start <= 1'b0;
                end
            endcase
        end
    end
endmodule
