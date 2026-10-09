`timescale 1ns / 1ps
// Controller of the shared engine: layer / group / input counters (IDLE -> RUN -> FLUSH).
// The weight and bias ROMs hold their rows in processing order (layer, group, input), so both
// addresses are frame-wide counters that start at 0 and wrap after the last row.
module fc_ctrl #(
    parameter N_IN1      = 400,
    parameter N_IN2      = 120,
    parameter N_IN3      = 84,
    parameter GROUPS1    = 6,
    parameter GROUPS2    = 5,
    parameter GROUPS3    = 2,
    parameter ROM_AW     = 12,
    parameter BIAS_AW    = 4,
    parameter IN_AW      = 9
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire               fc_in_valid,
    input  wire               mac_busy,
    input  wire               hold_free,
    output wire               fc_in_ready,
    output wire               fc1_in_we,
    output wire [ IN_AW-1:0]  fc1_in_waddr,
    output wire [       1:0]  layer,      // 1..3, 0 in IDLE
    output wire [       2:0]  group,
    output wire [ IN_AW-1:0]  feature_raddr,
    output wire [ROM_AW-1:0]  weight_addr, // prefetch for the next cycle
    output wire               weight_en,   // read only when the row changes, and in IDLE to load row 0
    output wire [BIAS_AW-1:0] bias_addr,   // row of the current group
    output reg                first,
    output reg                mac_en,
    output reg                last
);

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_FLUSH = 2'd2;

    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] FILL_FULL = N_IN1;

    reg [       1:0] state, state_next;
    reg [       1:0] layer_r, layer_next;
    reg [       2:0] group_r, group_next;
    reg [ IN_AW-1:0] i, i_next;
    reg [ IN_AW-1:0] fill_cnt, fill_cnt_next;
    reg [ROM_AW-1:0] w_addr, w_addr_next;
    reg [BIAS_AW-1:0] b_addr, b_addr_next;

    localparam [31:0] N_IN1_C = N_IN1, N_IN2_C = N_IN2, N_IN3_C = N_IN3;
    localparam [31:0] GROUPS1_C = GROUPS1, GROUPS2_C = GROUPS2, GROUPS3_C = GROUPS3;
    reg [ IN_AW-1:0] n_in;
    reg [       3:0] n_groups;
    always @(*) begin
        case (layer_r)
            2'd1:    begin n_in = N_IN1_C[IN_AW-1:0]; n_groups = GROUPS1_C[3:0]; end
            2'd2:    begin n_in = N_IN2_C[IN_AW-1:0]; n_groups = GROUPS2_C[3:0]; end
            default: begin n_in = N_IN3_C[IN_AW-1:0]; n_groups = GROUPS3_C[3:0]; end
        endcase
    end

    wire lastin     = (i == n_in - 1'b1);
    wire lastgroup  = ({1'b0, group_r} + 1'b1 >= n_groups);
    wire frame_end  = lastin & lastgroup & (layer_r == 2'd3);
    wire avail      = ~((layer_r == 2'd1) & (group_r == 3'd0)) | (i < fill_cnt);
    wire can        = (state == S_RUN) & avail & (~lastin | hold_free);

    // ========== input fill (independent of the FSM) ==========
    assign fc_in_ready = (fill_cnt < FILL_FULL[IN_AW-1:0]);
    assign fc1_in_we    = fc_in_valid & fc_in_ready;
    assign fc1_in_waddr = fill_cnt;

    // ========== Next State Logic ==========
    always @(*) begin : fc_ctrl_comb
        state_next    = state;
        layer_next    = layer_r;
        group_next    = group_r;
        i_next        = i;
        fill_cnt_next = fc1_in_we ? fill_cnt + 1'b1 : fill_cnt;
        w_addr_next   = w_addr;
        b_addr_next   = b_addr;
        first         = 1'b0;
        mac_en        = 1'b0;
        last          = 1'b0;

        case (state)
            S_IDLE: begin
                if (fill_cnt != {IN_AW{1'b0}}) begin
                    state_next = S_RUN;
                    layer_next = 2'd1;
                    group_next = 3'd0;
                    i_next     = {IN_AW{1'b0}};
                end
            end
            S_RUN: begin
                if (can) begin
                    mac_en = 1'b1;
                    first  = (i == {IN_AW{1'b0}});
                    last   = lastin;
                    w_addr_next = frame_end ? {ROM_AW{1'b0}} : w_addr + 1'b1;
                    if (!lastin) begin
                        i_next = i + 1'b1;
                    end else begin
                        i_next      = {IN_AW{1'b0}};
                        b_addr_next = frame_end ? {BIAS_AW{1'b0}} : b_addr + 1'b1;
                        if (!lastgroup) group_next = group_r + 1'b1;
                        else            state_next = S_FLUSH;
                    end
                end
            end
            default: begin
                if (!mac_busy & hold_free) begin
                    if (layer_r == 2'd1) fill_cnt_next = {IN_AW{1'b0}};
                    if (layer_r != 2'd3) begin
                        layer_next = layer_r + 1'b1;
                        group_next = 3'd0;
                        i_next     = {IN_AW{1'b0}};
                        state_next = S_RUN;
                    end else begin
                        layer_next = 2'd0;
                        state_next = S_IDLE;
                    end
                end
            end
        endcase
    end

    assign layer         = layer_r;
    assign group         = group_r;
    assign feature_raddr = i;
    assign weight_addr   = w_addr_next;
    assign weight_en     = mac_en | (state == S_IDLE);
    assign bias_addr     = b_addr;

    always @(posedge clk) begin
        if (!rst_n) begin
            state    <= S_IDLE;
            layer_r  <= 2'd0;
            group_r  <= 3'd0;
            i        <= {IN_AW{1'b0}};
            fill_cnt <= {IN_AW{1'b0}};
            w_addr   <= {ROM_AW{1'b0}};
            b_addr   <= {BIAS_AW{1'b0}};
        end else begin
            state    <= state_next;
            layer_r  <= layer_next;
            group_r  <= group_next;
            i        <= i_next;
            fill_cnt <= fill_cnt_next;
            w_addr   <= w_addr_next;
            b_addr   <= b_addr_next;
        end
    end

endmodule
