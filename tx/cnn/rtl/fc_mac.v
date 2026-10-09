`timescale 1ns / 1ps
// P multipliers and P accumulators, 3-cycle latency; the group's first input starts from the bias (DSP opmode C + M, then P + M).
module fc_mac #(
    parameter P     = 20,
    parameter ACC_W = 40
) (
    input  wire                clk,
    input  wire                rst_n,
    input  wire [        15:0] feature,
    input  wire [    16*P-1:0] weight,
    input  wire [    32*P-1:0] bias,
    input  wire                first,
    input  wire                mac_en,
    input  wire                last,
    input  wire [         1:0] layer,
    input  wire [         2:0] group,
    output wire                sum_valid,
    output wire [ACC_W*P-1:0]  sum,      // lane l = sum[ACC_W*l +: ACC_W], signed
    output wire [         1:0] sum_layer,
    output wire [         2:0] sum_group,
    output wire                busy
);

    // ========== stage 1: operand registers ==========
    reg [    15:0] s1_feature;
    reg [16*P-1:0] s1_weight;
    reg            s1_first, s1_last, s1_valid;
    reg [     1:0] s1_layer;
    reg [     2:0] s1_group;

    // ========== stage 2: products, and the bias for the group's first input ==========
    reg signed [31:0] s2_prod[0:P-1];
    reg [32*P-1:0]    s2_bias;
    reg               s2_first, s2_last, s2_valid;
    reg [     1:0]    s2_layer;
    reg [     2:0]    s2_group;

    // ========== stage 3: accumulators ==========
    reg signed [ACC_W-1:0] acc[0:P-1];
    reg               sum_valid_r;
    reg [     1:0]    sum_layer_r;
    reg [     2:0]    sum_group_r;

    always @(posedge clk) begin
        s1_feature <= feature;
        s1_weight  <= weight;
        s2_bias    <= bias;
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            s1_first <= 1'b0; s1_last <= 1'b0; s1_valid <= 1'b0;
            s1_layer <= 2'd0; s1_group <= 3'd0;
            s2_first <= 1'b0; s2_last <= 1'b0; s2_valid <= 1'b0;
            s2_layer <= 2'd0; s2_group <= 3'd0;
            sum_valid_r <= 1'b0; sum_layer_r <= 2'd0; sum_group_r <= 3'd0;
        end else begin
            s1_first <= first & mac_en;
            s1_last  <= last & mac_en;
            s1_valid <= mac_en;
            s1_layer <= layer;
            s1_group <= group;

            s2_first <= s1_first;
            s2_last  <= s1_last;
            s2_valid <= s1_valid;
            s2_layer <= s1_layer;
            s2_group <= s1_group;

            sum_valid_r <= s2_valid & s2_last;
            sum_layer_r <= s2_layer;
            sum_group_r <= s2_group;
        end
    end

    genvar l;
    generate
        for (l = 0; l < P; l = l + 1) begin : GEN_LANE
            always @(posedge clk) s2_prod[l] <= $signed({1'b0, s1_feature}) * $signed(s1_weight[16*l+:16]);

            wire signed [ACC_W-1:0] bias_ext = $signed({{(ACC_W - 32) {s2_bias[32*l+31]}}, s2_bias[32*l+:32]});
            wire signed [ACC_W-1:0] prod_ext = $signed({{(ACC_W - 32) {s2_prod[l][31]}}, s2_prod[l]});
            wire signed [ACC_W-1:0] base     = s2_first ? bias_ext : acc[l];

            always @(posedge clk) begin
                if (s2_valid) acc[l] <= base + prod_ext;
            end

            assign sum[ACC_W*l+:ACC_W] = acc[l];
        end
    endgenerate

    // ========== Output Logic ==========
    assign sum_valid = sum_valid_r;
    assign sum_layer = sum_layer_r;
    assign sum_group = sum_group_r;
    assign busy      = s1_valid | s2_valid | sum_valid_r;

endmodule
