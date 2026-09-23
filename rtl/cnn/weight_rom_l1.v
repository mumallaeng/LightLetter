`timescale 1ns / 1ps

module weight_rom_l1 #(
    parameter OCH = 6
) (
    input                        clk,
    input      [$clog2(OCH)-1:0] out_ch_sel,
    output reg [          143:0] weight_out
);
    reg [143:0] rom[0:OCH-1];

    // initialize
    initial begin
        $readmemh("conv1_weight_144.mem", rom);
    end

    always @(posedge clk) begin
        weight_out <= rom[out_ch_sel];
    end

endmodule
