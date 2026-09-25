`timescale 1ns / 1ps
// FC MAC: L multiplies per clock, summed into one CH_W partial sum for the Output Buffer.
// Three pipeline stages: the products register, the first half of the adder tree, then the
// second half and the result register, so ch_result follows its mac_en by three clocks.
// x is an unsigned activation code, w is a signed weight; lane i sits in bits [16*i +: 16].
// The adder tree is balanced (log2(L) levels) and split in half by a register, so no clock
// carries more than half of it - a chain would have put L-1 adders in one.

module fc_mac #(
    parameter L    = 25,  // FC1=25; FC2=10; FC3=5
    parameter CH_W = 36   // FC1=36; FC2=36; FC3=36
) (
    input  wire                   clk,
    input  wire                   rst_n,
    input  wire                   mac_en,
    input  wire        [16*L-1:0] w_in,
    input  wire        [16*L-1:0] x_in,
    output wire signed [CH_W-1:0] ch_result,
    output wire                   mac_valid
);
    integer i;

    reg prod_valid;
    reg mid_valid;
    reg sum_valid;

    reg signed [31:0] prod[0:L-1];  // products of the L multiplies, 32 bits to hold the signed result
    reg signed [CH_W-1:0] sum;  // adder tree result, CH_W bits like the conv mac_array

    // Balanced adder tree, cut in half by a register. Level 0 holds the sign-extended products
    // and each level pairs them up, an odd node moving up untouched. SPLIT is where the pipeline
    // register sits, so neither half is deeper than ceil(LEVELS/2) adders.
    localparam LEVELS = (L > 1) ? $clog2(L) : 1;
    localparam SPLIT = LEVELS / 2;
    localparam MIDN = (L + (1 << SPLIT) - 1) >> SPLIT;  // nodes at the split

    /* verilator lint_off UNOPTFLAT */
    // each level reads the one below it, which Verilator reads as the array depending on itself
    wire signed [CH_W-1:0] lo[0:SPLIT][0:L-1];  // products up to the split
    wire signed [CH_W-1:0] hi[SPLIT:LEVELS][0:L-1];  // the split down to one value
    /* verilator lint_on UNOPTFLAT */

    reg signed [CH_W-1:0] mid[0:MIDN-1];  // pipeline register at the split

    genvar lv, g;
    generate
        for (g = 0; g < L; g = g + 1) begin : GEN_LEAF
            assign lo[0][g] = $signed({{(CH_W - 32) {prod[g][31]}}, prod[g]});
        end

        for (lv = 1; lv <= SPLIT; lv = lv + 1) begin : GEN_LO_LEVEL
            localparam integer PREV = (L + (1 << (lv - 1)) - 1) >> (lv - 1);  // nodes one level down
            localparam integer CUR = (PREV + 1) >> 1;

            for (g = 0; g < CUR; g = g + 1) begin : GEN_NODE
                if (2 * g + 1 < PREV) begin : GEN_PAIR
                    assign lo[lv][g] = lo[lv-1][2*g] + lo[lv-1][2*g+1];
                end else begin : GEN_ODD
                    assign lo[lv][g] = lo[lv-1][2*g];
                end
            end
        end

        for (g = 0; g < MIDN; g = g + 1) begin : GEN_MID
            assign hi[SPLIT][g] = mid[g];
        end

        for (lv = SPLIT + 1; lv <= LEVELS; lv = lv + 1) begin : GEN_HI_LEVEL
            localparam integer PREV = (L + (1 << (lv - 1)) - 1) >> (lv - 1);
            localparam integer CUR = (PREV + 1) >> 1;

            for (g = 0; g < CUR; g = g + 1) begin : GEN_NODE
                if (2 * g + 1 < PREV) begin : GEN_PAIR
                    assign hi[lv][g] = hi[lv-1][2*g] + hi[lv-1][2*g+1];
                end else begin : GEN_ODD
                    assign hi[lv][g] = hi[lv-1][2*g];
                end
            end
        end
    endgenerate

    // ========== Output Logic ==========
    assign ch_result = sum;
    assign mac_valid = sum_valid;

    // ========== Sequential Logic ==========
    always @(posedge clk) begin : fc_mac_seq
        if (!rst_n) begin
            for (i = 0; i < L; i = i + 1) prod[i] <= 32'sd0;
            for (i = 0; i < MIDN; i = i + 1) mid[i] <= {CH_W{1'sb0}};
            prod_valid <= 1'b0;
            mid_valid  <= 1'b0;
            sum        <= {CH_W{1'sb0}};
            sum_valid  <= 1'b0;
        end else begin
            // stage 1: one product per lane
            if (mac_en) for (i = 0; i < L; i = i + 1) prod[i] <= $signed({1'b0, x_in[16*i+:16]}) * $signed(w_in[16*i+:16]);
            prod_valid <= mac_en;

            // stage 2: the first half of the tree over the products latched last clock
            for (i = 0; i < MIDN; i = i + 1) mid[i] <= lo[SPLIT][i];
            mid_valid <= prod_valid;

            // stage 3: the rest of the tree
            sum       <= hi[LEVELS][0];
            sum_valid <= mid_valid;
        end
    end

endmodule
