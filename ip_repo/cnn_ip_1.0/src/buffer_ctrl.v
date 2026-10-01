`timescale 1ns / 1ps
// Buffer Controller: BRAM for cross-group partial sums (N x C_OUT), sync read/write.
// The reader feeds the next-cycle address, so rdata always matches the current address.

module buffer_ctrl #(
    parameter DEPTH = 1936,
    parameter ACC_W = 40,
    parameter AW    = (DEPTH > 1) ? $clog2(DEPTH) : 1
) (
    input  wire                    clk,
    input  wire        [AW-1:0]    raddr,
    input  wire        [AW-1:0]    waddr,
    input  wire signed [ACC_W-1:0] wdata,
    input  wire                    we,
    output reg  signed [ACC_W-1:0] rdata
);

    (* ram_style = "block" *)
    reg signed [ACC_W-1:0] mem[0:DEPTH-1];

    always @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
        rdata <= mem[raddr];
    end

endmodule
