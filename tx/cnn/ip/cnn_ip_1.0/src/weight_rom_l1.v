`timescale 1ns / 1ps

// conv1 weight ROM: one row per output channel, async read, contents fixed at synthesis ($readmemh).
// 144 bit = 9 taps x INT16, tap 0 at the LSB. Rows come from golden/export_conv_roms.py.
module weight_rom_l1 #(
    parameter OCH      = 6,
    parameter MEM_FILE = "l1_weight.mem"
) (
    input      [$clog2(OCH)-1:0] out_ch_sel,
    output reg [          143:0] weight_out
);
    reg [143:0] rom[0:OCH-1];

    initial $readmemh(MEM_FILE, rom);

    always @(*) begin
        weight_out = (out_ch_sel < OCH) ? rom[out_ch_sel] : 144'd0;
    end
endmodule
