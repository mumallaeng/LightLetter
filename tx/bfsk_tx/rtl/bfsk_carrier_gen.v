`timescale 1ps / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: bfsk_carrier_gen
// Date: 2026.09.16
// 설명:
//   BFSK Mapper가 전달한 symbol_type에 따라 Square-Wave Carrier를 생성한다.
//
//
// Symbol Encoding:
//   2'b00 : IDLE  -> Carrier OFF
//   2'b01 : BIT0  -> 10 kHz
//   2'b10 : BIT1  -> 20 kHz
//   2'b11 : SYNC  -> 25 kHz
//
//
// 현재 프로젝트 기준:
//   Fs              = 160 kSample/s
//   SYMBOL_SAMPLES  = 256
//   Symbol Time     = 1.6 ms
//
// 동작:
//   1. symbol_valid=1인 상태에서 symbol_start=1을 확인하면 Symbol을 시작한다.
//   2. BIT0/BIT1/SYNC이면 해당 주파수의 Square-Wave를 출력한다.
//   3. IDLE이면 optical_tx=0, tx_enable=0으로 유지한다.
//   4. 정확히 1 Symbol 시간이 지나면 symbol_done을 1 Clock Pulse로 발생시킨다.
//
//
// 주의:
//   - System Clock은 Top 설계에 따라 바뀔 수 있으므로 Parameter화한다.
//   - F0/F1/FSYNC 및 Symbol 길이 역시 Parameter로 관리한다.
//   - CLK_FREQ_HZ가 각 Carrier의 2배 주파수로 정확히 나누어 떨어지는 것을 권장한다.
//////////////////////////////////////////////////////////////////////////////////

module bfsk_carrier_gen #(
    //global param
    parameter integer       CLK_FREQ_HZ    = 100_000_000,
    parameter integer       FS_HZ          = 160_000,
    parameter integer       SYMBOL_SAMPLES = 256,
    //bit freq param
    parameter integer       F0_HZ          = 10_000,
    parameter integer       F1_HZ          = 20_000,
    parameter integer       FSYNC_HZ       = 25_000,
    //fsm param
    parameter         [1:0] SYMBOL_IDLE    = 2'b00,
    parameter         [1:0] SYMBOL_BIT0    = 2'b01,
    parameter         [1:0] SYMBOL_BIT1    = 2'b10,
    parameter         [1:0] SYMBOL_SYNC    = 2'b11
) (
    //global signals
    input  wire       clk,
    input  wire       rst,
    //BFSK Mapper -> Carrier Generator
    input  wire [1:0] symbol_type,
    input  wire       symbol_valid,
    input  wire       symbol_start,
    //Carrier Generator -> BFSK Mapper & TX FSM
    output reg        symbol_done,
    //Carrier Generator -> Optical Driver
    output reg        optical_tx,
    output reg        tx_enable
);
    //===========================================
    //  Symbol Rate
    //  160_000 / 256 = 625 Symbol/s
    //  1 Symbol = 1 / 625s = 1.6ms
    //===========================================
    localparam integer SYMBOL_RATE_HZ = FS_HZ / SYMBOL_SAMPLES;
    localparam integer SYMBOL_CYCLES = CLK_FREQ_HZ / SYMBOL_RATE_HZ;

    localparam integer F0_HALF_CYCLES = CLK_FREQ_HZ / (2 * F0_HZ);
    localparam integer F1_HALF_CYCLES = CLK_FREQ_HZ / (2 * F1_HZ);
    localparam integer FSYNC_HALF_CYCLES = CLK_FREQ_HZ / (2 * FSYNC_HZ);

    reg        active;
    reg [ 1:0] current_symbol;
    //
    reg [31:0] symbol_count;
    reg [31:0] carrier_count;
    reg [31:0] half_period_cycles;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            active             <= 1'b0;
            current_symbol     <= SYMBOL_IDLE;
            //
            symbol_count       <= 32'd0;
            carrier_count      <= 32'd0;
            half_period_cycles <= 32'd0;
            //
            symbol_done        <= 1'b0;
            optical_tx         <= 1'b0;
            tx_enable          <= 1'b0;
        end else begin
            //symbol done : 종료 시점의 1clk pulse
            symbol_done <= 1'b0;
            //=======================================================
            //symbol start
            //=======================================================
            if (!active) begin
                optical_tx <= 1'b0;
                tx_enable  <= 1'b0;
                if (symbol_start && symbol_valid) begin
                    active         <= 1'b1;
                    current_symbol <= symbol_type;
                    //
                    symbol_count   <= 32'b0;
                    carrier_count  <= 32'b0;
                    optical_tx     <= 1'b0;

                    case (symbol_type)
                        SYMBOL_BIT0: begin
                            tx_enable <= 1'b1;
                            half_period_cycles <= F0_HALF_CYCLES;
                        end
                        SYMBOL_BIT1: begin
                            tx_enable <= 1'b1;
                            half_period_cycles <= F1_HALF_CYCLES;
                        end
                        SYMBOL_SYNC: begin
                            tx_enable <= 1'b1;
                            half_period_cycles <= FSYNC_HALF_CYCLES;
                        end
                        default: begin
                            //IDEL은 Carrier출력 X
                            tx_enable          <= 1'b0;
                            half_period_cycles <= 32'd0;
                        end
                    endcase
                end
            end else begin
                //=======================================================
                //Currnt Symbol Output
                //=======================================================
                //Last Clock of Symbol
                if (symbol_count == SYMBOL_CYCLES - 1) begin
                    active         <= 1'b0;
                    current_symbol <= SYMBOL_IDLE;
                    //
                    symbol_count   <= 32'd0;
                    carrier_count  <= 32'd0;
                    //
                    optical_tx     <= 1'b0;
                    tx_enable      <= 1'b0;
                    //
                    symbol_done    <= 1'b1;
                end else begin
                    symbol_count <= symbol_count + 1'b1;
                    // BIT0, BIT1, SYNC -> Square Wave Gen
                    if (tx_enable) begin
                        if (carrier_count == half_period_cycles - 1) begin
                            carrier_count <= 32'd0;
                            optical_tx    <= ~optical_tx;
                        end else begin
                            carrier_count <= carrier_count + 1'b1;
                        end
                    end else begin
                        //IDLE Symbol
                        carrier_count <= 32'd0;
                        optical_tx    <= 1'b0;
                    end
                end
            end
        end
    end
endmodule
