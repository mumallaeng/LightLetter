`timescale 1ns / 1ps

module weight_rom_l1 #(
    parameter OCH = 6
) (
    input      [$clog2(OCH)-1:0] out_ch_sel,
    output reg [          143:0] weight_out
);
    always @(*) begin
        case (out_ch_sel)
            0: weight_out = 144'hfa20ee44bfab2c79f116eeb00e0925ed23ca;
            1: weight_out = 144'hecafccb6d80ed3d00fa00c8a13ea1c6c13ce;
            2: weight_out = 144'h00380ec9114d017bf8a30c9d1eae08a006e6;
            3: weight_out = 144'h196310651885ff4e01270aade5b2e29beda4;
            4: weight_out = 144'he0062ad3f8b80d1017fdfcf4010efcdded09;
            5: weight_out = 144'h1a540906f76d1d0aee7ce9c61de7fd9ed95e;
            default: weight_out = 0;
        endcase
    end
endmodule
