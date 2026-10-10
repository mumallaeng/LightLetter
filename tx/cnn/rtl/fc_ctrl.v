`timescale 1ns / 1ps
// Controller of the shared engine: runs the loop "for layer, for group, for i" one MAC per clock.
// IDLE waits for the first input, RUN issues MACs, FLUSH waits until the layer's results are written.
// The weight and bias ROMs hold their rows in processing order (layer, group, input): a group's
// weight rows start right after the previous group's, and the bias row counts groups.
module fc_ctrl #(
    parameter N_IN1   = 400,
    parameter N_IN2   = 120,
    parameter N_IN3   = 84,
    parameter GROUPS1 = 6,
    parameter GROUPS2 = 5,
    parameter GROUPS3 = 2,
    parameter ROM_AW  = 12,
    parameter BIAS_AW = 4,
    parameter IN_AW   = 9
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire               fc_in_valid,
    input  wire               mac_busy,
    input  wire               hold_free,
    output wire               fc_in_ready,
    output wire               fc1_in_we,
    output wire [  IN_AW-1:0] fc1_in_waddr,
    output wire [        1:0] layer,          // 1..3, 0 in IDLE
    output wire [        2:0] group,
    output wire [  IN_AW-1:0] feature_raddr,
    output wire [ ROM_AW-1:0] weight_addr,    // row of the current input; the row arrives one clock later
    output wire               weight_en,
    output wire [BIAS_AW-1:0] bias_addr,      // row of the current group
    output wire               first,
    output wire               mac_en,
    output wire               last
);

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_FLUSH = 2'd2;
    localparam RPAD = ROM_AW - IN_AW;

    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] FILL_FULL = N_IN1;
    localparam [31:0] N_IN1_C = N_IN1, N_IN2_C = N_IN2, N_IN3_C = N_IN3;
    localparam [31:0] GROUPS1_C = GROUPS1, GROUPS2_C = GROUPS2, GROUPS3_C = GROUPS3;

    reg [        1:0] state;
    reg [        1:0] layer_r;
    reg [        2:0] group_r;
    reg [  IN_AW-1:0] i;
    reg [  IN_AW-1:0] fill_cnt;
    reg [ ROM_AW-1:0] group_row;  // weight row of the group's input 0
    reg [BIAS_AW-1:0] b_addr;

    // ========== layer table ==========
    reg [  IN_AW-1:0] n_in;
    reg [        3:0] n_groups;
    always @(*) begin
        case (layer_r)
            2'd1: begin
                n_in     = N_IN1_C[IN_AW-1:0];
                n_groups = GROUPS1_C[3:0];
            end
            2'd2: begin
                n_in     = N_IN2_C[IN_AW-1:0];
                n_groups = GROUPS2_C[3:0];
            end
            default: begin
                n_in     = N_IN3_C[IN_AW-1:0];
                n_groups = GROUPS3_C[3:0];
            end
        endcase
    end

    // ========== loop conditions ==========
    wire [IN_AW-1:0] i_adv = i + 1'b1;  // next input of the group
    wire [IN_AW-1:0] i_start = {IN_AW{1'b0}};  // first input of a group
    wire             lastin = (i == n_in - 1'b1);
    wire             lastgroup = ({1'b0, group_r} + 1'b1 >= n_groups);
    wire             avail = ~((layer_r == 2'd1) & (group_r == 3'd0)) | (i < fill_cnt);  // FC1 group 0 follows the input

    wire             start = (state == S_IDLE) & (fill_cnt != {IN_AW{1'b0}});
    wire             step = (state == S_RUN) & avail & (~lastin | hold_free);  // one MAC this clock
    wire             group_end = step & lastin;
    wire             layer_end = group_end & lastgroup;
    wire             frame_end = layer_end & (layer_r == 2'd3);
    wire             drained = (state == S_FLUSH) & ~mac_busy & hold_free;
    wire             next_layer = drained & (layer_r != 2'd3);
    wire             frame_done = drained & (layer_r == 2'd3);

    // ========== FSM ==========
    always @(posedge clk) begin
        if (!rst_n) state <= S_IDLE;
        else if (start) state <= S_RUN;
        else if (layer_end) state <= S_FLUSH;
        else if (next_layer) state <= S_RUN;
        else if (frame_done) state <= S_IDLE;
    end

    // ========== loop counters: layer -> group -> i ==========
    always @(posedge clk) begin
        if (!rst_n) begin
            layer_r <= 2'd0;
            group_r <= 3'd0;
            i       <= {IN_AW{1'b0}};
        end else if (start | next_layer) begin
            layer_r <= layer_r + 1'b1;
            group_r <= 3'd0;
            i       <= i_start;
        end else if (frame_done) begin
            layer_r <= 2'd0;
        end else if (step) begin
            if (!lastin) begin
                i <= i_adv;
            end else begin
                i <= i_start;
                if (!lastgroup) group_r <= group_r + 1'b1;
            end
        end
    end

    // ========== ROM rows: weight row = group's row 0 + i, bias row counts groups ==========
    always @(posedge clk) begin
        if (!rst_n) begin
            group_row <= {ROM_AW{1'b0}};
            b_addr    <= {BIAS_AW{1'b0}};
        end else if (group_end) begin
            group_row <= frame_end ? {ROM_AW{1'b0}} : group_row + {{RPAD{1'b0}}, n_in};
            b_addr    <= frame_end ? {BIAS_AW{1'b0}} : b_addr + 1'b1;
        end
    end

    // ========== input fill: next frame is accepted once FC1 is done ==========
    always @(posedge clk) begin
        if (!rst_n) fill_cnt <= {IN_AW{1'b0}};
        else if (next_layer & (layer_r == 2'd1)) fill_cnt <= {IN_AW{1'b0}};
        else if (fc1_in_we) fill_cnt <= fill_cnt + 1'b1;
    end

    // ========== Output Logic ==========
    assign fc_in_ready   = (fill_cnt < FILL_FULL[IN_AW-1:0]);
    assign fc1_in_we     = fc_in_valid & fc_in_ready;
    assign fc1_in_waddr  = fill_cnt;

    assign layer         = layer_r;
    assign group         = group_r;
    assign feature_raddr = i;
    assign mac_en        = step;
    assign first         = step & (i == {IN_AW{1'b0}});
    assign last          = group_end;
    assign weight_addr   = group_row + {{RPAD{1'b0}}, i};
    assign weight_en     = step;
    assign bias_addr     = b_addr;

endmodule
