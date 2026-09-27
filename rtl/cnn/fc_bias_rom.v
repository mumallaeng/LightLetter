`timescale 1ns / 1ps
// Bias ROM of the shared engine: one row of P INT32 biases per (layer, group),
// row = BIAS_BASE[layer] + group, lane l in bits [32*l +: 32]. Asynchronous read; it is read
// in the group's LOAD cycle only. Contents come from export_fc_vectors.py (fc_bias.mem).

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
