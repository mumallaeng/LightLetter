`timescale 1ns / 1ps
// Holds one group's P sums and quantizes them one per clock: FC1 / FC2 to the next feature buffer, FC3 to the logit stream.
module fc_quant_out #(
    parameter P          = 20,
    parameter ACC_W      = 40,
    parameter N_OUT1     = 120,
    parameter N_OUT2     = 84,
    parameter N_OUT3     = 26,
    parameter SCALE_EXP1 = 16,
    parameter SCALE_EXP2 = 15,
    parameter SCALE_EXP3 = 14
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
    output wire [        6:0] feature_waddr,
    output wire [       15:0] feature_wdata,
    output wire signed [15:0] logit_data,
    output wire               logit_valid
);

    localparam LANE_AW = $clog2(P + 1);

    localparam [31:0] P32 = P;

    reg [ACC_W*P-1:0] hold, hold_next;
    reg               valid, valid_next;
    reg [LANE_AW-1:0] idx, idx_next;
    reg [LANE_AW-1:0] len, len_next;
    reg [        1:0] layer, layer_next;
    reg [        2:0] group, group_next;

    // ========== the lane being output ==========
    wire signed [ACC_W-1:0] cur = $signed(hold[ACC_W*idx+:ACC_W]);

    wire [ACC_W-1:0] relu_out;
    wire [     15:0] q1, q2;
    wire signed [15:0] q3;

    relu #(.ACC_W(ACC_W)) u_relu (.x_in(cur), .y_out(relu_out));
    quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP1)) u_q1 (.x_in(relu_out), .y_out(q1));
    quantizer #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP2)) u_q2 (.x_in(relu_out), .y_out(q2));
    quantizer_signed #(.ACC_W(ACC_W), .SCALE_EXP(SCALE_EXP3)) u_q3 (.x_in(cur), .y_out(q3));

    wire [15:0] y = (layer == 2'd1) ? q1 : (layer == 2'd2) ? q2 : q3;

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
    wire to_logit = (layer == 2'd3);
    wire pop      = valid & (to_logit ? logit_ready : 1'b1);

    assign hold_free   = ~valid;
    assign feature_we      = valid & ~to_logit;
    assign feature_layer   = layer;
    assign feature_waddr   = {4'd0, group} * 7'd20 + {2'd0, idx};
    assign feature_wdata   = y;
    assign logit_data  = $signed(y);
    assign logit_valid = valid & to_logit;

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
        if (!rst_n) begin
            hold  <= {(ACC_W * P) {1'b0}};
            valid <= 1'b0;
            idx   <= {LANE_AW{1'b0}};
            len   <= {LANE_AW{1'b0}};
            layer <= 2'd0;
            group <= 3'd0;
        end else begin
            hold  <= hold_next;
            valid <= valid_next;
            idx   <= idx_next;
            len   <= len_next;
            layer <= layer_next;
            group <= group_next;
        end
    end

endmodule
