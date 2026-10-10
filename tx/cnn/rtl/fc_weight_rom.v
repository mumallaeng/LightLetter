`timescale 1ns / 1ps
// Weight ROM: one row of P weights, synchronous read (BRAM); rows in processing order: layer, group, input.
module fc_weight_rom #(
    parameter P        = 20,
    parameter ROWS     = 3168,
    parameter ROM_FILE = "fc_weight.mem",
    parameter AW       = $clog2(ROWS)
) (
    input  wire            clk,
    input  wire            en,
    input  wire [ AW-1:0]  addr,
    output reg  [16*P-1:0] w_out
);

    (* ram_style = "block" *)
    reg [16*P-1:0] mem[0:ROWS-1];

    initial $readmemh(ROM_FILE, mem);

    // BRAM read register, no reset; w_out holds its row while en is low
    always @(posedge clk) if (en) w_out <= mem[addr];

endmodule
