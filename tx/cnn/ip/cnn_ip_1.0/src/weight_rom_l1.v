`timescale 1ns / 1ps

// conv1 weight ROM: one row per output channel, registered read, contents fixed at synthesis ($readmemh).
// 144 bit = 9 taps x INT16, tap 0 at the LSB. Rows come from golden/export_conv_roms.py.
// weight_out comes out 1 clk after out_ch_sel; weight_addr_ctrl_l1 delays cal_valid (cal_valid_rt) to match.
// rom_en = 0 keeps the last row (same as weight_rom_l2); it must come in the same clk as out_ch_sel.
module weight_rom_l1 #(
    parameter OCH      = 6,
    parameter MEM_FILE = "l1_weight.mem"
) (
    input                        clk,
    input                        rst_n,
    input      [$clog2(OCH)-1:0] out_ch_sel,
    input                        rom_en,
    output reg [          143:0] weight_out
);
    reg [143:0] rom[0:OCH-1];

    initial $readmemh(MEM_FILE, rom);

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            weight_out <= 144'd0;
        end else if (rom_en) begin
            weight_out <= rom[out_ch_sel];
        end
    end
endmodule
