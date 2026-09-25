`timescale 1ns / 1ps
// Input staging: ping-pong buffers of L values.
// The calc buffer feeds fc_mac while the fill buffer collects the next chunk;
// fc_ctrl swaps them with chunk_done.

module fc_staging #(
    parameter L         = 25,   // FC1=25; FC2=10; FC3=5
    parameter N_IN      = 400,  // FC1=400; FC2=120; FC3=84
    parameter NUM_CHUNK = 16    // FC1=16; FC2=12; FC3=17
) (
    input  wire            clk,
    input  wire            rst_n,
    input  wire [    15:0] in_data,
    input  wire            in_valid,
    input  wire            chunk_done,
    output reg             in_ready,
    output reg             comp_full,
    output reg             next_full,
    output reg  [16*L-1:0] x_out
);

    localparam ENTRY_W = 16 * L;
    localparam CNT_AW = (L > 1) ? $clog2(L) : 1;
    localparam CHK_AW = (NUM_CHUNK > 1) ? $clog2(NUM_CHUNK) : 1;

    // only the last chunk can be short, so its length is a compile-time constant
    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] LANE_LAST = L - 1;
    localparam [31:0] CHUNK_LAST = NUM_CHUNK - 1;
    localparam [31:0] TAIL_LAST = N_IN - (NUM_CHUNK - 1) * L - 1;

    // registers: reg / reg_next
    reg [ENTRY_W-1:0] sbuf0, sbuf0_next;
    reg [ENTRY_W-1:0] sbuf1, sbuf1_next;
    reg [1:0] full, full_next;
    reg fill_sel, fill_sel_next;
    reg calc_sel, calc_sel_next;
    reg [CNT_AW-1:0] fill_cnt, fill_cnt_next;  // lane being filled
    reg [CHK_AW-1:0] fill_chunk, fill_chunk_next;  // chunk being collected

    wire take = in_valid & in_ready;
    wire tail_chunk = (fill_chunk == CHUNK_LAST[CHK_AW-1:0]);
    wire chunk_last = tail_chunk ? (fill_cnt == TAIL_LAST[CNT_AW-1:0]) : (fill_cnt == LANE_LAST[CNT_AW-1:0]);

    // ========== Next State / Counter Logic ==========
    always @(*) begin : fc_staging_comb
        sbuf0_next      = sbuf0;
        sbuf1_next      = sbuf1;
        full_next       = full;
        fill_sel_next   = fill_sel;
        calc_sel_next   = calc_sel;
        fill_cnt_next   = fill_cnt;
        fill_chunk_next = fill_chunk;

        if (take) begin
            // lane 0 clears the buffer, so lanes past a short chunk read 0
            if (!fill_sel) begin
                if (fill_cnt == {CNT_AW{1'b0}}) sbuf0_next = {{(ENTRY_W - 16) {1'b0}}, in_data};
                else sbuf0_next[{fill_cnt, 4'd0}+:16] = in_data;
            end else begin
                if (fill_cnt == {CNT_AW{1'b0}}) sbuf1_next = {{(ENTRY_W - 16) {1'b0}}, in_data};
                else sbuf1_next[{fill_cnt, 4'd0}+:16] = in_data;
            end

            fill_cnt_next = chunk_last ? {CNT_AW{1'b0}} : fill_cnt + 1'b1;

            if (chunk_last) begin
                full_next[fill_sel] = 1'b1;
                fill_sel_next       = ~fill_sel;
                fill_chunk_next     = tail_chunk ? {CHK_AW{1'b0}} : fill_chunk + 1'b1;
            end
        end

        // fc_ctrl releases the calc buffer; a release in the same cycle wins over the fill above
        if (chunk_done) begin
            full_next[calc_sel] = 1'b0;
            calc_sel_next       = ~calc_sel;
        end
    end

    // ========== Output Logic ==========
    always @(*) begin : fc_staging_out
        in_ready  = ~full[fill_sel];
        comp_full = full[calc_sel];
        next_full = full[~calc_sel];
        x_out     = calc_sel ? sbuf1 : sbuf0;
    end

    always @(posedge clk) begin : fc_staging_seq
        if (!rst_n) begin
            sbuf0      <= {ENTRY_W{1'b0}};
            sbuf1      <= {ENTRY_W{1'b0}};
            full       <= 2'b00;
            fill_sel   <= 1'b0;
            calc_sel   <= 1'b0;
            fill_cnt   <= {CNT_AW{1'b0}};
            fill_chunk <= {CHK_AW{1'b0}};
        end else begin
            sbuf0      <= sbuf0_next;
            sbuf1      <= sbuf1_next;
            full       <= full_next;
            fill_sel   <= fill_sel_next;
            calc_sel   <= calc_sel_next;
            fill_cnt   <= fill_cnt_next;
            fill_chunk <= fill_chunk_next;
        end
    end

endmodule
