`timescale 1ns / 1ps
// FC MAC: L multiplies per clock, summed into one CH_W partial sum for the Output Buffer.
// Two pipeline stages: the products register, then the adder tree and the result register,
// so ch_result follows its mac_en by two clocks.
// x is an unsigned activation code, w is a signed weight; lane i sits in bits [16*i +: 16].
// The adder tree is built from continuous assignments rather than an always @(*) over the
// prod array, which simulation, lint and synthesis tools each read differently.

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
    reg sum_valid;

    reg signed [31:0] prod[0:L-1];  // products of the L multiplies, 32 bits to hold the signed result
    reg signed [CH_W-1:0] sum;  // adder tree result, CH_W bits like the conv mac_array

    // adder tree as continuous assignments: chain[g] holds the sum of lanes 0..g,
    // each product sign-extended to the accumulate width like partial_sum does
    wire signed [CH_W-1:0] chain[0:L-1];

    genvar g;
    generate
        assign chain[0] = $signed({{(CH_W - 32) {prod[0][31]}}, prod[0]});
        for (g = 1; g < L; g = g + 1) begin : GEN_ADDER_TREE
            assign chain[g] = chain[g-1] + $signed({{(CH_W - 32) {prod[g][31]}}, prod[g]});
        end
    endgenerate

    // ========== Output Logic ==========
    assign ch_result = sum;
    assign mac_valid = sum_valid;

    // ========== Sequential Logic ==========
    always @(posedge clk) begin : fc_mac_seq
        if (!rst_n) begin
            for (i = 0; i < L; i = i + 1) prod[i] <= 32'sd0;
            prod_valid <= 1'b0;
            sum        <= {CH_W{1'sb0}};
            sum_valid  <= 1'b0;
        end else begin
            // stage 1: one product per lane
            if (mac_en) for (i = 0; i < L; i = i + 1) prod[i] <= $signed({1'b0, x_in[16*i+:16]}) * $signed(w_in[16*i+:16]);
            prod_valid <= mac_en;

            // stage 2: register the adder tree over the products latched last clock
            sum        <= chain[L-1];
            sum_valid  <= prod_valid;
        end
    end

endmodule
