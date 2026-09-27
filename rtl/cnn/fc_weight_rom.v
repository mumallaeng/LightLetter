`timescale 1ns / 1ps
// Weight ROM of the shared engine: one row of P weights per (layer, group, input),
// row = ROM_BASE[layer] + group * N_IN + input, lane l (neuron group*P + l) in bits [16*l +: 16].
// Synchronous read: the row addressed in one cycle is on w_out in the next, so fc_ctrl
// addresses the row the next cycle needs. Contents come from export_fc_vectors.py (fc_weight.mem).

module fc_weight_rom #(
    parameter P        = 20,
    parameter ROWS     = 3168,
    parameter ROM_FILE = "fc_weight.mem",
    parameter AW       = $clog2(ROWS)
) (
    input  wire            clk,
    input  wire [ AW-1:0]  addr,
    output reg  [16*P-1:0] w_out
);

    (* ram_style = "block" *)
    reg [16*P-1:0] mem[0:ROWS-1];

    initial $readmemh(ROM_FILE, mem);

    // BRAM read register, no reset (like the conv-side ROMs)
    always @(posedge clk) w_out <= mem[addr];

endmodule
