`timescale 1ns / 1ps
// Bias ROM: per-output-channel bias, async read, contents fixed at synthesis ($readmemh).

module bias_rom #(
    parameter C_OUT     = 16,
    parameter BIAS_FILE = "conv2_bias.mem",
    parameter AW        = (C_OUT > 1) ? $clog2(C_OUT) : 1
) (
    input  wire        [AW-1:0] addr,
    output wire signed [31:0]   rdata
);

    reg signed [31:0] mem[0:C_OUT-1];

    initial $readmemh(BIAS_FILE, mem);

    assign rdata = mem[addr];

endmodule
