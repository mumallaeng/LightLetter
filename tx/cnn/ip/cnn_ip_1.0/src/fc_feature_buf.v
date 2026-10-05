`timescale 1ns / 1ps
// Feature buffer: synchronous write, asynchronous read (distributed RAM).
module fc_feature_buf #(
    parameter DEPTH = 400,
    parameter AW    = (DEPTH > 1) ? $clog2(DEPTH) : 1
) (
    input  wire          clk,
    input  wire          we,
    input  wire [AW-1:0] waddr,
    input  wire [  15:0] wdata,
    input  wire [AW-1:0] raddr,
    output wire [  15:0] rdata
);

    (* ram_style = "distributed" *)
    reg [15:0] mem[0:DEPTH-1];

    always @(posedge clk) if (we) mem[waddr] <= wdata;

    assign rdata = mem[raddr];

endmodule
