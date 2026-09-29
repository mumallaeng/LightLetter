`timescale 1ns / 1ps

module rx_symbol_detector #(
    parameter MAG_W = 24
) (
    input  wire                 clk,
    input  wire                 rst_n,

    // rx_bin_detector에서 128-sample FFT 블록마다 전달되는 값
    input  wire [MAG_W-1:0]     bin8_power,
    input  wire [MAG_W-1:0]     bin16_power,
    input  wire [MAG_W-1:0]     bin20_power,
    input  wire                 fft_block_done,

    // 128-sample FFT 블록 하나에 대한 주파수 판정 결과
    output wire [1:0]           block_code,
    output wire                 block_code_valid
);

    localparam [1:0] CODE_BIT0    = 2'b00;
    localparam [1:0] CODE_BIT1    = 2'b01;
    localparam [1:0] CODE_SYNC    = 2'b10;
    localparam [1:0] CODE_INVALID = 2'b11;

    reg [1:0] block_code_reg;
    reg       block_code_valid_reg;

    // 두 번째 Power 대비 20% 이하의 차이는 INVALID로 처리한다.
    // 10 * 최대값 > 12 * 나머지 값 두 개를 모두 만족해야 유효하다.
    // 곱셈 결과가 넘치지 않도록 MAG_W + 4비트로 확장한다.
    wire [MAG_W+3:0] p8  = {4'b0000, bin8_power};
    wire [MAG_W+3:0] p16 = {4'b0000, bin16_power};
    wire [MAG_W+3:0] p20 = {4'b0000, bin20_power};
    wire [MAG_W+3:0] p8_x10  = (p8 << 3) + (p8 << 1);
    wire [MAG_W+3:0] p16_x10 = (p16 << 3) + (p16 << 1);
    wire [MAG_W+3:0] p20_x10 = (p20 << 3) + (p20 << 1);
    wire [MAG_W+3:0] p8_x12  = (p8 << 3) + (p8 << 2);
    wire [MAG_W+3:0] p16_x12 = (p16 << 3) + (p16 << 2);
    wire [MAG_W+3:0] p20_x12 = (p20 << 3) + (p20 << 2);

    assign block_code       = block_code_reg;
    assign block_code_valid = block_code_valid_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            block_code_reg       <= CODE_INVALID;
            block_code_valid_reg <= 1'b0;
        end
        else begin
            // 새로운 FFT 블록 판정이 있을 때만 한 클럭 발생한다.
            block_code_valid_reg <= 1'b0;

            if (fft_block_done) begin
                if ((p8_x10 > p16_x12) &&
                    (p8_x10 > p20_x12)) begin
                    block_code_reg <= CODE_BIT0;
                end
                else if ((p16_x10 > p8_x12) &&
                         (p16_x10 > p20_x12)) begin
                    block_code_reg <= CODE_BIT1;
                end
                else if ((p20_x10 > p8_x12) &&
                         (p20_x10 > p16_x12)) begin
                    block_code_reg <= CODE_SYNC;
                end
                else begin
                    // 동률, 모두 0 또는 우세 차이가 20% 이하이면 INVALID.
                    block_code_reg <= CODE_INVALID;
                end

                // INVALID도 하나의 완료된 판정 결과이므로 valid를 발생시킨다.
                block_code_valid_reg <= 1'b1;
            end
        end
    end

endmodule
