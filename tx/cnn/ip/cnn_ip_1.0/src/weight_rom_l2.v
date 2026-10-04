`timescale 1ns / 1ps

module weight_rom_l2 #(
    parameter OCH = 16
) (
    input                    clk,
    input                    is_ch35,
    input  [$clog2(OCH)-1:0] out_ch_sel,
    output [          431:0] weight_out
);
    wire [431:0] och_weight[0:OCH-1];

    genvar i;
    generate
        for (i = 0; i < OCH; i = i + 1) begin : g_och_rom
            out_ch_weight #(
                // l2_weight_ch00.mem - l2_weight_ch15.mem (8'd48 = '0')
                .MEM_FILE({
                    "l2_weight_ch", 8'd48 + (i / 10), 8'd48 + (i % 10), ".mem"
                })
            ) U_OCH_ROM (
                .clk       (clk),
                .is_ch35   (is_ch35),
                .weight_out(och_weight[i])
            );
        end
    endgenerate

    // ----- out_ch mux -----
    assign weight_out = och_weight[out_ch_sel];
endmodule

module out_ch_weight #(
    parameter MEM_FILE = "l2_weight_ch00.mem"
) (
    input              clk,
    input              is_ch35,
    output reg [431:0] weight_out
);
    // rom
    reg [431:0] rom[0:1];

    initial begin
        $readmemh(MEM_FILE, rom);
    end

    always @(posedge clk) begin
        weight_out <= rom[is_ch35];
    end

endmodule
