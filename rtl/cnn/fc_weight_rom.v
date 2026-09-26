`timescale 1ns / 1ps
// Weight ROM: one row per (chunk, neuron), L weights per row, lane 0 in the low 16 bits.
// Row index = chunk * N_OUT + neuron, which is what fc_ctrl drives on rom_addr.
// The read is synchronous, so the address leads the mac_en it belongs to by one clock.
// Contents come from export_fc_vectors.py (rtl/cnn/mem/fcK_weight.mem).

module fc_weight_rom #(
    parameter L         = 25,               // FC1=25; FC2=10; FC3=5
    parameter N_OUT     = 120,              // FC1=120; FC2=84; FC3=36
    parameter NUM_CHUNK = 16,               // FC1=16; FC2=12; FC3=17
    parameter ROM_FILE  = "fc1_weight.mem"  // rtl/cnn/mem
) (
    input  wire                               clk,
    input  wire [$clog2(NUM_CHUNK*N_OUT)-1:0] addr,
    output reg  [                   16*L-1:0] w_out      // lane i = w_out[16*i +: 16], signed
);

    localparam DEPTH = NUM_CHUNK * N_OUT;

    (* ram_style = "block" *)
    reg [16*L-1:0] mem[0:DEPTH-1];

    initial $readmemh(ROM_FILE, mem);

    // BRAM read register: w_out holds the row addressed on the previous clock.
    // No reset, like the conv-side BRAMs - the first row is valid one clock after the first address.
    always @(posedge clk) w_out <= mem[addr];

endmodule
