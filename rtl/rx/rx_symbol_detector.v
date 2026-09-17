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
                if ((bin8_power > bin16_power) &&
                    (bin8_power > bin20_power)) begin
                    block_code_reg <= CODE_BIT0;
                end
                else if ((bin16_power > bin8_power) &&
                         (bin16_power > bin20_power)) begin
                    block_code_reg <= CODE_BIT1;
                end
                else if ((bin20_power > bin8_power) &&
                         (bin20_power > bin16_power)) begin
                    block_code_reg <= CODE_SYNC;
                end
                else begin
                    // 최대 Power가 동률이면 주파수를 확정하지 않는다.
                    block_code_reg <= CODE_INVALID;
                end

                // INVALID도 하나의 완료된 판정 결과이므로 valid를 발생시킨다.
                block_code_valid_reg <= 1'b1;
            end
        end
    end

endmodule
