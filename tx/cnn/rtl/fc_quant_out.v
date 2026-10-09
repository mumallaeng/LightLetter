`timescale 1ns / 1ps
// Holds one group's P sums and quantizes them one per clock through two output stages: FC1 / FC2 to the next feature buffer, FC3 to the logit stream.
module fc_quant_out #(
    parameter P          = 20,
    parameter ACC_W      = 40,
    parameter N_OUT1     = 120,
    parameter N_OUT2     = 84,
    parameter N_OUT3     = 26,
    parameter SCALE_EXP1 = 16,
    parameter SCALE_EXP2 = 14,
    parameter SCALE_EXP3 = 14,
    parameter WADDR_AW   = 7            // feature buffer address width
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire               sum_valid,
    input  wire [ACC_W*P-1:0] sum,
    input  wire [        1:0] sum_layer,
    input  wire [        2:0] sum_group,
    input  wire               logit_ready,
    output wire               hold_free,
    output wire               feature_we,
    output wire [        1:0] feature_layer,
    output wire [WADDR_AW-1:0] feature_waddr,
    output wire [       15:0] feature_wdata,
    output wire signed [15:0] logit_data,
    output wire               logit_valid
);

    localparam LANE_AW = $clog2(P + 1);

    localparam [31:0] P32 = P;
    localparam [WADDR_AW-1:0] P_W = P;

    reg [ACC_W*P-1:0] hold, hold_next;
    reg               valid, valid_next;
    reg [LANE_AW-1:0] idx, idx_next;
    reg [LANE_AW-1:0] len, len_next;
    reg [        1:0] layer, layer_next;
    reg [        2:0] group, group_next;

    reg signed [ACC_W-1:0] s1_cur;
    reg                    s1_valid;
    reg [             1:0] s1_layer;
    reg [WADDR_AW-1:0]     s1_waddr;
    reg [            15:0] s2_y;
    reg                    s2_valid;
    reg [             1:0] s2_layer;
    reg [WADDR_AW-1:0]     s2_waddr;

    // ========== the lane being output ==========
    wire signed [ACC_W-1:0] cur = $signed(hold[ACC_W*idx+:ACC_W]);

    wire signed [15:0] q1, q2, q3;

    quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP1), .RELU(1)) u_q1 (.x_in(s1_cur), .y_out(q1));

    // FC2 and FC3 share one quantizer when their shifts match; FC2 zeroes a negative sum in front of it
    generate
        if (SCALE_EXP2 == SCALE_EXP3) begin : GEN_SHARED_Q
            wire signed [ACC_W-1:0] x23 = (s1_layer == 2'd2 && s1_cur[ACC_W-1]) ? {ACC_W{1'b0}} : s1_cur;
            wire signed [15:0] q23;
            quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP3), .RELU(0)) u_q23 (.x_in(x23), .y_out(q23));
            assign q2 = q23;
            assign q3 = q23;
        end else begin : GEN_SPLIT_Q
            quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP2), .RELU(1)) u_q2 (.x_in(s1_cur), .y_out(q2));
            quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP3), .RELU(0)) u_q3 (.x_in(s1_cur), .y_out(q3));
        end
    endgenerate

    wire [15:0] y = (s1_layer == 2'd1) ? q1 : (s1_layer == 2'd2) ? q2 : q3;

    // neurons in the group that just finished: the layer's last group can be short
    function [LANE_AW-1:0] group_len;
        input [1:0] l;
        input [2:0] g;
        reg   [31:0] left;
        begin
            left = ((l == 2'd1) ? N_OUT1 : (l == 2'd2) ? N_OUT2 : N_OUT3) - {29'd0, g} * P32;
            group_len = (left < P32) ? left[LANE_AW-1:0] : P32[LANE_AW-1:0];
        end
    endfunction

    // ========== Output Logic ==========
    wire s2_logit = s2_valid & (s2_layer == 2'd3);
    wire adv      = ~s2_logit | logit_ready;
    wire pop      = valid & adv;

    assign hold_free     = ~(valid | s1_valid | s2_valid);
    assign feature_we    = s2_valid & ~s2_logit;
    assign feature_layer = s2_layer;
    assign feature_waddr = s2_waddr;
    assign feature_wdata = s2_y;
    assign logit_data    = $signed(s2_y);
    assign logit_valid   = s2_logit;

    // ========== Next State Logic ==========
    always @(*) begin : fc_quant_out_comb
        hold_next  = hold;
        valid_next = valid;
        idx_next   = idx;
        len_next   = len;
        layer_next = layer;
        group_next = group;

        if (pop) begin
            if (idx + 1'b1 >= len) begin
                valid_next = 1'b0;
                idx_next   = {LANE_AW{1'b0}};
            end else begin
                idx_next = idx + 1'b1;
            end
        end

        // a finished group lands here; fc_ctrl guarantees the register is free by then
        if (sum_valid) begin
            hold_next  = sum;
            valid_next = 1'b1;
            idx_next   = {LANE_AW{1'b0}};
            len_next   = group_len(sum_layer, sum_group);
            layer_next = sum_layer;
            group_next = sum_group;
        end
    end

    always @(posedge clk) begin
        if (adv) begin
            s1_cur   <= cur;
            s1_layer <= layer;
            s1_waddr <= group * P_W + idx;
            s2_y     <= y;
            s2_layer <= s1_layer;
            s2_waddr <= s1_waddr;
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            s1_valid <= 1'b0;
            s2_valid <= 1'b0;
        end else if (adv) begin
            s1_valid <= pop;
            s2_valid <= s1_valid;
        end
    end

    always @(posedge clk) hold <= hold_next;

    always @(posedge clk) begin
        if (!rst_n) begin
            valid <= 1'b0;
            idx   <= {LANE_AW{1'b0}};
            len   <= {LANE_AW{1'b0}};
            layer <= 2'd0;
            group <= 3'd0;
        end else begin
            valid <= valid_next;
            idx   <= idx_next;
            len   <= len_next;
            layer <= layer_next;
            group <= group_next;
        end
    end

endmodule
