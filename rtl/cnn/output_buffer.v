`timescale 1ns / 1ps
// Output Buffer (top): mac_array -> Output Buffer -> ReLU & Quantization
// Order: input-channel group (outer) -> pixel -> output channel (inner)
//   conv1: N=676, C_OUT=6,  NUM_GROUPS=1      conv2: N=121, C_OUT=16, NUM_GROUPS=2 (default)

module output_buffer #(
    parameter N          = 121,
    parameter C_OUT      = 16,
    parameter NUM_GROUPS = 2,
    parameter CH_W       = 36,
    parameter ACC_W      = 40,
    parameter BIAS_FILE  = "conv2_bias.mem"   // rtl/cnn/mem, conv1: "conv1_bias.mem"
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire signed [CH_W-1:0]  ch_result0,
    input  wire signed [CH_W-1:0]  ch_result1,   // 0 in conv1
    input  wire signed [CH_W-1:0]  ch_result2,   // 0 in conv1
    input  wire                    mac_valid,
    output wire                    ch3_5_en,     // processing the second group
    output wire                    ch_done,      // travels with sum_data: 1 on values of the last pixel
    output wire signed [ACC_W-1:0] sum_data,
    output wire                    sum_valid
);

    localparam CH_AW = (C_OUT > 1) ? $clog2(C_OUT) : 1;
    localparam PIX_AW = (N > 1) ? $clog2(N) : 1;
    localparam GRP_AW = (NUM_GROUPS > 1) ? $clog2(NUM_GROUPS) : 1;
    localparam BUF_AW = (N * C_OUT > 1) ? $clog2(N * C_OUT) : 1;

    localparam [1:0] OB_IDLE = 2'd0, OB_ACCUM_G0 = 2'd1,  // first group: store (conv1 outputs here)
    OB_ACCUM_G1 = 2'd2;  // second group: accumulate and output

    // registers: reg / reg_next
    reg [1:0] state, state_next;
    reg [CH_AW-1:0] out_ch_cnt, out_ch_cnt_next;
    reg [PIX_AW-1:0] pixel_cnt, pixel_cnt_next;
    reg [GRP_AW-1:0] group_cnt, group_cnt_next;
    reg [BUF_AW-1:0] buf_addr, buf_addr_next;  // = pixel_cnt * C_OUT + out_ch_cnt

    wire mac_fire    = mac_valid & (state != OB_IDLE);

    wire first_phase = (group_cnt == {GRP_AW{1'b0}});
    wire last_phase  = (group_cnt == $unsigned(NUM_GROUPS - 1));

    wire ch_last     = (out_ch_cnt == $unsigned(C_OUT - 1));
    wire pixel_last  = (pixel_cnt  == $unsigned(N - 1));
    wire pass_last   = ch_last & pixel_last;

    // ========== Next State / Counter Logic ==========
    always @(*) begin
        state_next      = state;
        out_ch_cnt_next = out_ch_cnt;
        pixel_cnt_next  = pixel_cnt;
        group_cnt_next  = group_cnt;
        buf_addr_next   = buf_addr;

        if (state == OB_IDLE) begin
            state_next = OB_ACCUM_G0;
        end else if (mac_fire) begin
            out_ch_cnt_next = ch_last   ? {CH_AW{1'b0}}  : out_ch_cnt + 1'b1;
            buf_addr_next   = pass_last ? {BUF_AW{1'b0}} : buf_addr + 1'b1;

            if (ch_last) pixel_cnt_next = pixel_last ? {PIX_AW{1'b0}} : pixel_cnt + 1'b1;

            if (pass_last) begin
                group_cnt_next = last_phase ? {GRP_AW{1'b0}} : group_cnt + 1'b1;
                state_next     = last_phase ? OB_ACCUM_G0    : OB_ACCUM_G1;
            end
        end
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            state      <= OB_IDLE;
            out_ch_cnt <= {CH_AW{1'b0}};
            pixel_cnt  <= {PIX_AW{1'b0}};
            group_cnt  <= {GRP_AW{1'b0}};
            buf_addr   <= {BUF_AW{1'b0}};
        end else begin
            state      <= state_next;
            out_ch_cnt <= out_ch_cnt_next;
            pixel_cnt  <= pixel_cnt_next;
            group_cnt  <= group_cnt_next;
            buf_addr   <= buf_addr_next;
        end
    end

    // ========== Submodule ==========
    wire signed [31:0]      bias_rdata;
    wire signed [ACC_W-1:0] buf_rdata;
    wire signed [ACC_W-1:0] ps_sum;
    wire                    ps_we;

    bias_rom #(
        .C_OUT    (C_OUT),
        .BIAS_FILE(BIAS_FILE)
    ) u_bias_rom (
        .addr (out_ch_cnt),
        .rdata(bias_rdata)
    );

    generate
        if (NUM_GROUPS > 1) begin : GEN_BUFFER_CTRL
            // sync read: feed the next address so rdata matches the current buf_addr
            buffer_ctrl #(
                .DEPTH(N * C_OUT),
                .ACC_W(ACC_W)
            ) u_buffer_ctrl (
                .clk  (clk),
                .raddr(buf_addr_next),
                .waddr(buf_addr),
                .wdata(ps_sum),
                .we   (ps_we & ~last_phase),
                .rdata(buf_rdata)
            );
        end else begin : GEN_NO_BUFFER
            assign buf_rdata = $signed({ACC_W{1'b0}});
        end
    endgenerate

    partial_sum #(
        .CH_W (CH_W),
        .ACC_W(ACC_W)
    ) u_partial_sum (
        .ch_result0 (ch_result0),
        .ch_result1 (ch_result1),
        .ch_result2 (ch_result2),
        .mac_valid  (mac_fire),
        .first_phase(first_phase),
        .last_phase (last_phase),
        .buf_rdata  (buf_rdata),
        .bias_rdata (bias_rdata),
        .sum        (ps_sum),
        .we         (ps_we),
        .sum_data   (sum_data),
        .sum_valid  (sum_valid)
    );

    // ========== Output Logic ==========
    assign ch3_5_en = (group_cnt != {GRP_AW{1'b0}});
    assign ch_done  = sum_valid & pixel_last;

endmodule
