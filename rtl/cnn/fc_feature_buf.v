`timescale 1ns / 1ps
// Feature buffer: one layer's inputs. Written one value per clock, read asynchronously
// (distributed RAM), so the value at raddr is on rdata in the same cycle.
// Three instances in fc_top: the MaxPooling frame (400), the FC1 outputs (120), the FC2 outputs (84).

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
