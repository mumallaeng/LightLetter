`timescale 1ns / 1ps
// P multipliers and P accumulators, 3-cycle latency; the group's first input starts from the bias (DSP opmode C + M, then P + M).
// Stage 1 is outside: the weight ROM read register and fc_zero_gating's feature / zero registers.
module fc_mac #(
    parameter P     = 20,
    parameter ACC_W = 40
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire [       15:0] feature,    // one clock after mac_en (fc_zero_gating feature_q)
    input  wire [   16*P-1:0] weight,     // one clock after mac_en (weight ROM read register)
    input  wire               zero,       // one clock after mac_en (fc_zero_gating zero_q): operands held, product = 0
    input  wire [   32*P-1:0] bias,
    input  wire               first,
    input  wire               mac_en,
    input  wire               last,
    input  wire [        1:0] layer,
    input  wire [        2:0] group,
    output wire               sum_valid,
    output wire [ACC_W*P-1:0] sum,        // lane l = sum[ACC_W*l +: ACC_W], signed
    output wire [        1:0] sum_layer,
    output wire [        2:0] sum_group,
    output wire               busy,
    output wire               last_inflight  // a group's last input is still in the pipeline
);

    // ========== tag pipeline ==========
    reg       s1_valid, s1_first, s1_last;
    reg [1:0] s1_layer;
    reg [2:0] s1_group;
    reg       s2_valid, s2_first, s2_last;
    reg [1:0] s2_layer;
    reg [2:0] s2_group;
    reg       sum_valid_r;
    reg [1:0] sum_layer_r;
    reg [2:0] sum_group_r;

    always @(posedge clk) begin
        if (!rst_n) begin
            s1_valid    <= 1'b0;
            s2_valid    <= 1'b0;
            sum_valid_r <= 1'b0;
        end else begin
            s1_valid    <= mac_en;
            s2_valid    <= s1_valid;
            sum_valid_r <= s2_valid & s2_last;
        end
    end

    always @(posedge clk) begin
        if (mac_en) begin
            s1_first <= first;
            s1_last  <= last;
            s1_layer <= layer;
            s1_group <= group;
        end
        if (s1_valid) begin
            s2_first <= s1_first;
            s2_last  <= s1_last;
            s2_layer <= s1_layer;
            s2_group <= s1_group;
        end
        if (s2_valid & s2_last) begin
            sum_layer_r <= s2_layer;
            sum_group_r <= s2_group;
        end
    end

    // ========== stage 2: products, and the bias row once per group ==========
    reg signed [    31:0] s2_prod [0:P-1];
    reg        [32*P-1:0] s2_bias;

    always @(posedge clk) if (s1_valid & s1_first) s2_bias <= bias;

    // ========== stage 3: accumulators ==========
    reg signed [ACC_W-1:0] acc[0:P-1];

    genvar l;
    generate
        for (l = 0; l < P; l = l + 1) begin : GEN_LANE
            always @(posedge clk) begin
                if (s1_valid) s2_prod[l] <= zero ? 32'sd0 : $signed({1'b0, feature}) * $signed(weight[16*l+:16]);
            end

            wire signed [ACC_W-1:0] bias_ext = $signed({{(ACC_W - 32) {s2_bias[32*l+31]}}, s2_bias[32*l+:32]});
            wire signed [ACC_W-1:0] prod_ext = $signed({{(ACC_W - 32) {s2_prod[l][31]}}, s2_prod[l]});
            wire signed [ACC_W-1:0] base     = s2_first ? bias_ext : acc[l];

            always @(posedge clk) begin
                if (!rst_n)        acc[l] <= {ACC_W{1'b0}};
                else if (s2_valid) acc[l] <= base + prod_ext;
            end

            assign sum[ACC_W*l+:ACC_W] = acc[l];
        end
    endgenerate

    // ========== Output Logic ==========
    assign sum_valid = sum_valid_r;
    assign sum_layer = sum_layer_r;
    assign sum_group = sum_group_r;
    assign busy      = s1_valid | s2_valid | sum_valid_r;
    // fc_quant_out's hold takes over the clk after sum_valid, so there is no gap
    assign last_inflight = (s1_valid & s1_last) | (s2_valid & s2_last) | sum_valid_r;

endmodule
