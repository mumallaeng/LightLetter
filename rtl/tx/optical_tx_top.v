`timescale 1ps / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: optical_tx_top
//
// 설명:
//   현재까지 검증한 TX 블록을 하나로 연결하는 통합 Top 모듈이다.
//
// 전체 흐름:
//   char_id / char_valid
//          ↓
//       tx_fsm
//          ↓
//   tx_frame_generator
//          ↓
//      bfsk_mapper
//          ↓
//   bfsk_carrier_gen
//          ↓
//   optical_tx / tx_enable
//
// 송신 순서:
//   1. char_valid && char_ready에서 Character ID 수락
//   2. SYNC Symbol x PREAMBLE_SYMBOLS
//   3. SFD + Frame ID + DATA + CRC-8
//   4. 송신 완료 후 IDLE 복귀
//
// 외부 인터페이스:
//   - 현재 단계에서는 내부 TX 검증용 char_id/valid/ready 인터페이스를 유지한다.
//   - 추후 AXI4-Lite Slave Wrapper가 이 외부 인터페이스를 감싸도록 구성한다.
//
// 주의:
//   - 본 모듈은 새로운 통신 규격을 정의하지 않고 기존 PASS 모듈을 연결한다.
//   - 주파수 및 Symbol 관련 값은 Parameter로 하위 Carrier Generator에 전달한다.
//////////////////////////////////////////////////////////////////////////////////

module optical_tx_top #(
    //Clock Param
    parameter integer       CLK_FREQ_HZ      = 100_000_000,
    parameter integer       FS_HZ            = 160_000,
    parameter integer       SYMBOL_SAMPLES   = 256,
    //BIT Param
    parameter integer       F0_HZ            = 10_000,
    parameter integer       F1_HZ            = 20_000,
    parameter integer       FSYNC_HZ         = 25_000,
    //Tranmission param
    parameter integer       PREAMBLE_SYMBOLS = 4,
    parameter         [7:0] SFD              = 8'hD5
) (
    //global signals
    input  wire       clk,
    input  wire       rst,
    //character Input Interface
    input  wire [7:0] char_id,
    input  wire       char_valid,
    output wire       char_ready,
    output wire       tx_busy,
    //optical TX Output
    output wire       optical_tx,
    output wire       tx_enable
);
    // ============================================================
    // TX FSM <-> Frame Generator
    // ============================================================
    wire [7:0] latched_char_id;
    wire [7:0] frame_id;

    wire       frame_gen_start;
    wire       frame_gen_done;  // 마지막 비트를 Mapper에 전달 완료
    wire       frame_done;  // 마지막 Symbol의 광출력까지 완료
    reg        last_symbol_pending;

    //Debug
    wire       frame_stream_start;
    wire       frame_gen_busy;

    // ============================================================
    // Frame Generator <-> BFSK Mapper
    // ============================================================
    wire       tx_bit;
    wire       tx_bit_valid;
    wire       tx_bit_ready;

    // ============================================================
    // TX FSM <-> BFSK Mapper
    // ============================================================
    wire       sync_valid;
    wire       sync_ready;

    // ============================================================
    // BFSK Mapper <-> Carrier Generator
    // ============================================================
    wire [1:0] symbol_type;
    wire       symbol_valid;
    wire       symbol_start;
    wire       symbol_done;

    // ============================================================
    // 마지막 비트 전달 후 실제 Carrier 출력 완료 대기
    // ============================================================
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            last_symbol_pending <= 1'b0;
        end else if (frame_gen_done) begin
            last_symbol_pending <= 1'b1;
        end else if (frame_done) begin
            last_symbol_pending <= 1'b0;
        end
    end

    assign frame_done = last_symbol_pending && symbol_done;

    // ============================================================
    // TX FSM
    // ============================================================
    tx_fsm #(
        .PREAMBLE_SYMBOLS(PREAMBLE_SYMBOLS)
    ) U_TX_FSM (
        .clk            (clk),
        .rst            (rst),
        //
        .char_id        (char_id),
        .char_valid     (char_valid),
        .char_ready     (char_ready),
        .tx_busy        (tx_busy),
        //
        .latched_char_id(latched_char_id),
        .frame_id       (frame_id),
        .frame_gen_start(frame_gen_start),
        .frame_done     (frame_done),
        //
        .sync_valid     (sync_valid),
        .sync_ready     (sync_ready),
        //
        .symbol_done    (symbol_done)
    );

    // ============================================================
    // Frame Generator + CRC8
    // ============================================================
    tx_frame_generator #(
        .SFD(SFD)
    ) U_TX_FRAME_GENERATOR (
        .clk         (clk),
        .rst         (rst),
        //
        .start       (frame_gen_start),
        .frame_id    (frame_id),
        .data_in     (latched_char_id),
        //
        .tx_bit      (tx_bit),
        .tx_bit_valid(tx_bit_valid),
        .tx_bit_ready(tx_bit_ready),
        //
        .frame_start (frame_stream_start),
        .frame_done  (frame_gen_done),
        .busy        (frame_gen_busy)
    );

    // ============================================================
    // BFSK Mapper
    // ============================================================
    bfsk_mapper U_BFSK_MAPPER (
        .clk         (clk),
        .rst         (rst),
        //
        .tx_bit      (tx_bit),
        .tx_bit_valid(tx_bit_valid),
        .tx_bit_ready(tx_bit_ready),
        //
        .sync_valid  (sync_valid),
        .sync_ready  (sync_ready),
        //
        .symbol_type (symbol_type),
        .symbol_valid(symbol_valid),
        .symbol_start(symbol_start),
        .symbol_done (symbol_done)
    );

    // ============================================================
    // BFSK Carrier Generator
    // ============================================================
    bfsk_carrier_gen #(
        .CLK_FREQ_HZ   (CLK_FREQ_HZ),
        .FS_HZ         (FS_HZ),
        .SYMBOL_SAMPLES(SYMBOL_SAMPLES),
        //
        .F0_HZ         (F0_HZ),
        .F1_HZ         (F1_HZ),
        .FSYNC_HZ      (FSYNC_HZ)
    ) bfsk_carrier_gen (
        .clk         (clk),
        .rst         (rst),
        //
        .symbol_type (symbol_type),
        .symbol_valid(symbol_valid),
        .symbol_start(symbol_start),
        .symbol_done (symbol_done),
        //
        .optical_tx  (optical_tx),
        .tx_enable   (tx_enable)
    );
endmodule
