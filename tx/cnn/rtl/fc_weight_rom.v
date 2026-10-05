`timescale 1ns / 1ps
// Weight ROM: one row of P weights, synchronous read (BRAM); row = ROM_BASE[layer] + group * N_IN + input.
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

    // BRAM read register, no reset
    always @(posedge clk) w_out <= mem[addr];

endmodule
