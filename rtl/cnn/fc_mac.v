`timescale 1ns / 1ps
// P multipliers and P accumulators of the shared engine. Every `mac_en` adds x * w[lane]; the
// group's first input (`first`) starts from the bias instead of the running sum, which is the
// DSP48E1's own opmode switch (C + M, then P + M) rather than a fabric mux. Three register
// stages after the issue cycle: operands (DSP A/B registers) -> products (M register) ->
// accumulate (the P register). The flags ride along, so the sums of the group's last input are
// announced on sum_valid in the cycle the accumulate stage handles it.
// x is an unsigned activation code, w a signed weight; lane l sits in bits [16*l +: 16].

module fc_mac #(
    parameter P     = 20,
    parameter ACC_W = 40
) (
    input  wire                clk,
    input  wire                rst_n,
    input  wire [        15:0] x,
    input  wire [    16*P-1:0] w,
    input  wire [    32*P-1:0] b,
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
    reg [    15:0] s1_x;
    reg [16*P-1:0] s1_w;
    reg [32*P-1:0] s1_b;
    reg            s1_first, s1_mac, s1_last, s1_valid;
    reg [     1:0] s1_layer;
    reg [     2:0] s1_group;

    // ========== stage 2: products ==========
    reg signed [31:0] s2_prod[0:P-1];
    reg [32*P-1:0]    s2_b;
    reg               s2_first, s2_mac, s2_last, s2_valid;
    reg [     1:0]    s2_layer;
    reg [     2:0]    s2_group;

    // ========== accumulators ==========
    reg signed [ACC_W-1:0] acc[0:P-1];

    always @(posedge clk) begin
        if (!rst_n) begin
            s1_x <= 16'd0;  s1_w <= {(16 * P) {1'b0}};  s1_b <= {(32 * P) {1'b0}};
            s1_first <= 1'b0; s1_mac <= 1'b0; s1_last <= 1'b0; s1_valid <= 1'b0;
            s1_layer <= 2'd0; s1_group <= 3'd0;
            s2_b <= {(32 * P) {1'b0}};
            s2_first <= 1'b0; s2_mac <= 1'b0; s2_last <= 1'b0; s2_valid <= 1'b0;
            s2_layer <= 2'd0; s2_group <= 3'd0;
        end else begin
            s1_x     <= x;
            s1_w     <= w;
            s1_b     <= b;
            s1_first <= first & mac_en;
            s1_mac   <= mac_en;
            s1_last  <= last & mac_en;
            s1_valid <= mac_en;
            s1_layer <= layer;
            s1_group <= group;

            s2_b     <= s1_b;
            s2_first <= s1_first;
            s2_mac   <= s1_mac;
            s2_last  <= s1_last;
            s2_valid <= s1_valid;
            s2_layer <= s1_layer;
            s2_group <= s1_group;
        end
    end

    genvar l;
    generate
        for (l = 0; l < P; l = l + 1) begin : GEN_LANE
            // product register: unsigned x (17-bit signed) times signed w
            always @(posedge clk) begin
                if (!rst_n) s2_prod[l] <= 32'sd0;
                else s2_prod[l] <= $signed({1'b0, s1_x}) * $signed(s1_w[16*l+:16]);
            end

            // accumulate: the group's first input adds to the bias, the rest to the running sum
            wire signed [ACC_W-1:0] bias_ext = $signed({{(ACC_W - 32) {s2_b[32*l+31]}}, s2_b[32*l+:32]});
            wire signed [ACC_W-1:0] prod_ext = $signed({{(ACC_W - 32) {s2_prod[l][31]}}, s2_prod[l]});
            wire signed [ACC_W-1:0] base     = s2_first ? bias_ext : acc[l];
            wire signed [ACC_W-1:0] lane_sum = (s2_valid & s2_mac) ? base + prod_ext : acc[l];

            always @(posedge clk) begin
                if (!rst_n) acc[l] <= {ACC_W{1'b0}};
                else acc[l] <= lane_sum;
            end

            assign sum[ACC_W*l+:ACC_W] = lane_sum;
        end
    endgenerate

    // ========== Output Logic ==========
    assign sum_valid = s2_valid & s2_mac & s2_last;
    assign sum_layer = s2_layer;
    assign sum_group = s2_group;
    assign busy      = s1_valid | s2_valid;

endmodule
