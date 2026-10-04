`timescale 1ns / 1ps

module rx_bin_detector #(
    parameter MAG_W = 40
) (
    input  wire                 clk,
    input  wire                 rst_n,

    // FFT?Š” bin 0ë¶??„° bin 127ê¹Œì? Powerë¥? ?ˆœ?„œ??ë¡? ì¶œë ¥?•œ?‹¤.
    input  wire [MAG_W-1:0]     fft_mag,
    input  wire                 fft_mag_valid,

    output wire [MAG_W-1:0]     bin8_power,
    output wire [MAG_W-1:0]     bin16_power,
    output wire [MAG_W-1:0]     bin20_power,
    output wire                 fft_block_done
);

    // ?•œ ë²ˆì˜ 128-point FFT?—?„œ ì¶œë ¥?˜?Š” binë³? Power ???¥ ê³µê°„
    reg [MAG_W-1:0] fft_power_mem [0:127];

    // validê°? ?“¤?–´?˜¨ ?ˆœ?„œê°? ê³? bin ë²ˆí˜¸?‹¤.
    reg [6:0] fft_counter;
    reg       fft_block_done_reg;

    assign bin8_power     = fft_power_mem[8];
    assign bin16_power    = fft_power_mem[16];
    assign bin20_power    = fft_power_mem[20];
    assign fft_block_done = fft_block_done_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fft_counter        <= 7'd0;
            fft_block_done_reg <= 1'b0;

            // ì¶œë ¥?œ¼ë¡? ?‚¬?š©?•˜?Š” ?œ„ì¹˜ë§Œ ì´ˆê¸°?™”?•œ?‹¤.
            fft_power_mem[8]   <= {MAG_W{1'b0}};
            fft_power_mem[16]  <= {MAG_W{1'b0}};
            fft_power_mem[20]  <= {MAG_W{1'b0}};
        end
        else begin
            // 128ë²ˆì§¸ ?œ ?š¨ Powerë¥? ë°›ì? ?´?Ÿ­?—ë§? ?•œ ?´?Ÿ­ ë°œìƒ?•œ?‹¤.
            fft_block_done_reg <= 1'b0;

            if (fft_mag_valid) begin
                fft_power_mem[fft_counter] <= fft_mag;

                if (fft_counter == 7'd127) begin
                    fft_counter        <= 7'd0;
                    fft_block_done_reg <= 1'b1;
                end
                else begin
                    fft_counter <= fft_counter + 7'd1;
                end
            end
        end
    end

endmodule
