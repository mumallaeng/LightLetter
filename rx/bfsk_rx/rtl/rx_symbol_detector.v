`timescale 1ns / 1ps

module rx_symbol_detector #(
    parameter MAG_W = 40
) (
    input  wire                 clk,
    input  wire                 rst_n,

    // rx_bin_detector?—?„œ 128-sample FFT ë¸”ë¡ë§ˆë‹¤ ? „?‹¬?˜?Š” ê°?
    input  wire [MAG_W-1:0]     bin8_power,
    input  wire [MAG_W-1:0]     bin16_power,
    input  wire [MAG_W-1:0]     bin20_power,
    input  wire                 fft_block_done,

    // 128-sample FFT ë¸”ë¡ ?•˜?‚˜?— ???•œ ì£¼íŒŒ?ˆ˜ ?Œ? • ê²°ê³¼
    output wire [1:0]           block_code,
    output wire                 block_code_valid
);

    localparam [1:0] CODE_BIT0    = 2'b00;
    localparam [1:0] CODE_BIT1    = 2'b01;
    localparam [1:0] CODE_SYNC    = 2'b10;
    localparam [1:0] CODE_INVALID = 2'b11;

    reg [1:0] block_code_reg;
    reg       block_code_valid_reg;

    // ?‘ ë²ˆì§¸ Power ??ë¹? 20% ?´?•˜?˜ ì°¨ì´?Š” INVALIDë¡? ì²˜ë¦¬?•œ?‹¤.
    // 10 * ìµœë?ê°? > 12 * ?‚˜ë¨¸ì? ê°? ?‘ ê°œë?? ëª¨ë‘ ë§Œì¡±?•´?•¼ ?œ ?š¨?•˜?‹¤.
    // ê³±ì…ˆ ê²°ê³¼ê°? ?„˜ì¹˜ì? ?•Š?„ë¡? MAG_W + 4ë¹„íŠ¸ë¡? ?™•?¥?•œ?‹¤.
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
            // ?ƒˆë¡œìš´ FFT ë¸”ë¡ ?Œ? •?´ ?ˆ?„ ?•Œë§? ?•œ ?´?Ÿ­ ë°œìƒ?•œ?‹¤.
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
                    // ?™ë¥?, ëª¨ë‘ 0 ?˜?Š” ?š°?„¸ ì°¨ì´ê°? 20% ?´?•˜?´ë©? INVALID.
                    block_code_reg <= CODE_INVALID;
                end

                // INVALID?„ ?•˜?‚˜?˜ ?™„ë£Œëœ ?Œ? • ê²°ê³¼?´ë¯?ë¡? validë¥? ë°œìƒ?‹œ?‚¨?‹¤.
                block_code_valid_reg <= 1'b1;
            end
        end
    end

endmodule
