`timescale 1ns / 1ps
// Bias ROM: one row of P biases per (layer, group), asynchronous read, contents fixed at synthesis ($readmemh).
module fc_bias_rom #(
    parameter P        = 20,
    parameter ROWS     = 13,
    parameter ROM_FILE = "fc_bias.mem",
    parameter AW       = $clog2(ROWS)
) (
    input  wire [ AW-1:0]  addr,
    output wire [32*P-1:0] b_out
);

    reg [32*P-1:0] mem[0:ROWS-1];

    initial $readmemh(ROM_FILE, mem);

    assign b_out = mem[addr];

endmodule
