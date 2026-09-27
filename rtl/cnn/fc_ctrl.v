`timescale 1ns / 1ps
// Controller of the shared engine: layer / group / input counters.
//   IDLE  -> RUN once the frame's first input has arrived
//   RUN   : one input per clock; FC1 group 0 only as far as inputs have arrived (i < fill_cnt).
//           The group's first input carries `first` (the accumulators start from the bias,
//           DSP opmode C + M); the group's last input waits until the drain register is free
//   FLUSH : after a layer's last group, until the MAC pipeline and the drain are empty
// rom_addr is the row the NEXT cycle needs (BRAM prefetch) and re-addresses the same row while
// RUN stalls. grp_base tracks ROM_BASE + group * N_IN with adds only, like the conv-side
// address counters. fill_cnt counts the frame's inputs in act_in and clears after FC1.

module fc_ctrl #(
    parameter N_IN1      = 400,
    parameter N_IN2      = 120,
    parameter N_IN3      = 84,
    parameter GROUPS1    = 6,
    parameter GROUPS2    = 5,
    parameter GROUPS3    = 2,
    parameter ROM_BASE1  = 0,
    parameter ROM_BASE2  = 2400,
    parameter ROM_BASE3  = 3000,
    parameter BIAS_BASE1 = 0,
    parameter BIAS_BASE2 = 6,
    parameter BIAS_BASE3 = 11,
    parameter ROM_AW     = 12,
    parameter BIAS_AW    = 4,
    parameter IN_AW      = 9
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire               fc_in_valid,
    input  wire               mac_busy,   // <- fc_mac
    input  wire               hold_free,  // <- fc_drain
    output wire               fc_in_ready,
    output wire               in_we,      // act_in write of fc_in_data at in_waddr
    output wire [ IN_AW-1:0]  in_waddr,
    output wire [       1:0]  layer,      // 1..3, 0 in IDLE
    output wire [       2:0]  group,
    output wire [ IN_AW-1:0]  x_raddr,    // input index i
    output reg  [ROM_AW-1:0]  rom_addr,   // prefetch for the next cycle
    output reg  [BIAS_AW-1:0] bias_addr,  // the group's bias row, read with the first input
    output reg                first,
    output reg                mac_en,
    output reg                last
);

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_FLUSH = 2'd2;

    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] FILL_FULL = N_IN1;

    // registers: reg / reg_next
    reg [       1:0] state, state_next;
    reg [       1:0] layer_r, layer_next;
    reg [       2:0] group_r, group_next;
    reg [ IN_AW-1:0] i, i_next;
    reg [ IN_AW-1:0] fill_cnt, fill_cnt_next;
    reg [ROM_AW-1:0] grp_base, grp_base_next;

    // per-layer constants of the layer being run (32-bit, sliced at the use sites)
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

    wire lastin = (i == n_in - 1'b1);
    wire avail  = ~((layer_r == 2'd1) & (group_r == 3'd0)) | (i < fill_cnt);
    wire can    = (state == S_RUN) & avail & (~lastin | hold_free);

    // ========== input fill (independent of the FSM) ==========
    assign fc_in_ready = (fill_cnt < FILL_FULL[IN_AW-1:0]);
    assign in_we       = fc_in_valid & fc_in_ready;
    assign in_waddr    = fill_cnt;

    // ========== Next State Logic ==========
    always @(*) begin : fc_ctrl_comb
        state_next    = state;
        layer_next    = layer_r;
        group_next    = group_r;
        i_next        = i;
        fill_cnt_next = in_we ? fill_cnt + 1'b1 : fill_cnt;
        grp_base_next = grp_base;
        first         = 1'b0;
        mac_en        = 1'b0;
        last          = 1'b0;

        case (state)
            S_IDLE: begin
                if (fill_cnt != {IN_AW{1'b0}}) begin
                    state_next    = S_RUN;
                    layer_next    = 2'd1;
                    group_next    = 3'd0;
                    i_next        = {IN_AW{1'b0}};
                    grp_base_next = ROM_BASE1;
                end
            end
            S_RUN: begin
                if (can) begin
                    mac_en = 1'b1;
                    first  = (i == {IN_AW{1'b0}});
                    last   = lastin;
                    if (!lastin) begin
                        i_next = i + 1'b1;
                    end else begin
                        i_next = {IN_AW{1'b0}};
                        if ({1'b0, group_r} + 1'b1 < n_groups) begin
                            group_next    = group_r + 1'b1;
                            grp_base_next = grp_base + {{(ROM_AW - IN_AW) {1'b0}}, n_in};
                            state_next    = S_RUN;
                        end else begin
                            state_next = S_FLUSH;
                        end
                    end
                end
            end
            default: begin  // S_FLUSH
                if (!mac_busy & hold_free) begin
                    if (layer_r == 2'd1) fill_cnt_next = in_we ? {{(IN_AW - 1) {1'b0}}, 1'b1} : {IN_AW{1'b0}};
                    if (layer_r != 2'd3) begin
                        layer_next    = layer_r + 1'b1;
                        group_next    = 3'd0;
                        i_next        = {IN_AW{1'b0}};
                        grp_base_next = (layer_r == 2'd1) ? ROM_BASE2 : ROM_BASE3;
                        state_next    = S_RUN;
                    end else begin
                        layer_next = 2'd0;
                        state_next = S_IDLE;
                    end
                end
            end
        endcase

        // ========== Output Logic ==========
        // the row the next cycle issues: where the counters land after this cycle
        rom_addr = grp_base_next + {{(ROM_AW - IN_AW) {1'b0}}, i_next};
        case (layer_r)
            2'd1:    bias_addr = BIAS_BASE1 + group_r;
            2'd2:    bias_addr = BIAS_BASE2 + group_r;
            2'd3:    bias_addr = BIAS_BASE3 + group_r;
            default: bias_addr = {BIAS_AW{1'b0}};
        endcase
    end

    assign layer   = layer_r;
    assign group   = group_r;
    assign x_raddr = i;

    always @(posedge clk) begin
        if (!rst_n) begin
            state    <= S_IDLE;
            layer_r  <= 2'd0;
            group_r  <= 3'd0;
            i        <= {IN_AW{1'b0}};
            fill_cnt <= {IN_AW{1'b0}};
            grp_base <= {ROM_AW{1'b0}};
        end else begin
            state    <= state_next;
            layer_r  <= layer_next;
            group_r  <= group_next;
            i        <= i_next;
            fill_cnt <= fill_cnt_next;
            grp_base <= grp_base_next;
        end
    end

endmodule
