`timescale 1ns / 1ps
// FC zero gating / zero skipping, used from fc_top (fc_ctrl only sees the results).
//   fc_zero_buf    : built while a feature buffer is written, one per layer input
//   fc_zero_gating : feature register beside the weight ROM output, holds on a zero input
// 0 x weight = 0, so the sums are bit-exact with the plain engine.

// ---------------------------------------------------------------------------
// Zero buffer: written with its feature buffer (same we / waddr / wdata), in address order 0, 1, 2, ...
//   zero[k] : x[k] == 0                         (gating)
//   nxt[a]  : next non-zero index after anchor a (skipping), anchors = 0 and every non-zero k
//   last    : last anchor, 0 when nothing after index 0 is non-zero
// A write to address 0 starts a new vector. nxt[last] is never read: fc_ctrl stops at i == last.
// ---------------------------------------------------------------------------
module fc_zero_buf #(
    parameter DEPTH = 400,
    parameter AW    = (DEPTH > 1) ? $clog2(DEPTH) : 1
) (
    input  wire          clk,
    input  wire          rst_n,
    input  wire          we,
    input  wire [AW-1:0] waddr,
    input  wire [  15:0] wdata,
    input  wire [AW-1:0] raddr,      // fc_ctrl i
    output wire          zero,       // zero[raddr]
    output wire [AW-1:0] nxt,        // nxt[raddr]
    output reg  [AW-1:0] last
);

    (* ram_style = "distributed" *)
    reg          zero_mem[0:DEPTH-1];
    (* ram_style = "distributed" *)
    reg [AW-1:0] nxt_mem [0:DEPTH-1];

    wire start  = (waddr == {AW{1'b0}});
    wire anchor = we & ~start & (wdata != 16'd0);

    always @(posedge clk) if (we) zero_mem[waddr] <= (wdata == 16'd0);

    // link the previous anchor to this one
    always @(posedge clk) if (anchor) nxt_mem[last] <= waddr;

    always @(posedge clk) begin
        if (!rst_n)          last <= {AW{1'b0}};
        else if (we & start) last <= {AW{1'b0}};
        else if (anchor)     last <= waddr;
    end

    assign zero = zero_mem[raddr];
    assign nxt  = nxt_mem[raddr];

endmodule

// ---------------------------------------------------------------------------
// Zero gating array: fc_mac's stage 1 feature register, on the same clk as the weight ROM read register.
//   valid & ~is_zero : load the feature (the ROM reads the row on the same condition, rom_en in fc_top)
//   valid &  is_zero : hold it and the ROM holds its row (no toggle into the DSPs),
//                      zero_q makes fc_mac drop the product
// ---------------------------------------------------------------------------
module fc_zero_gating (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        valid,      // fc_ctrl mac_en
    input  wire        is_zero,    // zero buffer flag of this input
    input  wire [15:0] feature,
    output reg  [15:0] feature_q,
    output reg         zero_q
);

    always @(posedge clk) begin
        if (!rst_n) begin
            feature_q <= 16'd0;
            zero_q    <= 1'b1;
        end else if (valid) begin
            zero_q <= is_zero;
            if (!is_zero) feature_q <= feature;
        end
    end

endmodule
