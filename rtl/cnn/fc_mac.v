`timescale 1ns / 1ps
// FC MAC: L multiplies per clock, summed into one CH_W partial sum for the Output Buffer.
// Two pipeline stages: the products register, then the adder tree and the result register,
// so ch_result follows its mac_en by two clocks.
// x is an unsigned activation code, w is a signed weight; lane i sits in bits [16*i +: 16].

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

    reg prod_valid, prod_valid_next;
    reg sum_valid, sum_valid_next;

    // registers: reg / reg_next
    reg signed [31:0] prod     [0:L-1];  // products of the L multiplies, 32 bits to hold the signed result
    reg signed [31:0] prod_next[0:L-1];
    reg signed [CH_W-1:0] sum, sum_next;  // adder tree result, 32 bits to hold the signed result

    assign ch_result = sum;
    assign mac_valid = sum_valid;

    // ========== Next State / Counter Logic ==========
    always @(*) begin : fc_mac_comb
        for (i = 0; i < L; i = i + 1) begin
            prod_next[i]    = prod[i];
            prod_valid_next = mac_en;
        end
        sum_next       = sum;
        sum_valid_next = prod_valid;

        if (mac_en) for (i = 0; i < L; i = i + 1) prod_next[i] = $signed(x_in[16*i+:16]) * $signed(w_in[16*i+:16]);
        sum_next = $signed({{(CH_W - 32) {prod[0][31]}}, prod[0]});
        for (i = 1; i < L; i = i + 1) sum_next = sum_next + $signed({{(CH_W - 32) {prod[i][31]}}, prod[i]});
    end

    // ========== Sequential Logic ==========
    always @(posedge clk) begin : fc_mac_seq
        if (!rst_n) begin
            for (i = 0; i < L; i = i + 1) prod[i] <= 32'sd0;
            prod_valid <= 1'b0;
            sum        <= {CH_W{1'sb0}};
            sum_valid  <= 1'b0;
        end else begin
            for (i = 0; i < L; i = i + 1) begin
                prod[i]    <= prod_next[i];
                prod_valid <= prod_valid_next;
            end
            sum       <= sum_next;
            sum_valid <= sum_valid_next;
        end
    end

endmodule
