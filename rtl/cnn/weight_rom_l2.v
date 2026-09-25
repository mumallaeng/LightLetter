`timescale 1ns / 1ps

module weight_rom_l2 #(
    parameter OCH = 16
) (
    input      [$clog2(OCH)-1:0] out_ch_sel,
    output reg [          143:0] weight_out
);
    always @(*) begin
        case (out_ch_sel)
            0: weight_out = 144'h0847ed93b7cf4feee9f1ecd701952d2e4035;
            1: weight_out = 144'hbabeb14bbe5dfea0245fe74326bc225c0f26;
            2: weight_out = 144'he57404370b5f04fef2a6238f274c23700a49;
            3: weight_out = 144'h16b21d9b22b5c04b05d24138c5e1d6ec0d45;
            4: weight_out = 144'hd2c82c2ad0b12fe32ce62033e1e7e9b7ee82;
            5: weight_out = 144'h11c40ea6008a2799ef62f8123364f6e3bcb6;
            default: weight_out = 0;
        endcase
    end
endmodule

module out_ch0_weight (
    input              clk,
    input              is_ch35,
    output reg [143:0] weight_out
);
    // rom
    reg [143:0] rom[0:1];

    initial begin
        $readmemh("l2_weight_ch0.mem", rom);
    end

    always @(posedge clk) begin
        weight_out <= rom[is_ch35];
    end

endmodule
