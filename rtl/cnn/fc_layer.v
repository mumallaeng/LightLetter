`timescale 1ns / 1ps
// One Fully Connected layer: fc_staging -> fc_mac -> output_buffer -> relu_quant / fc_quant_signed.
// Order is chunk (outer) then neuron (inner), so every neuron finishes during the last chunk.
// output_buffer and relu_quant are the conv blocks reused as they are,
// with one "pixel" per frame, C_OUT = neurons and NUM_GROUPS = chunks.

module fc_layer #(
    parameter N_IN        = 400,               // FC1=400; FC2=120; FC3=84
    parameter N_OUT       = 120,               // FC1=120; FC2=84; FC3=36
    parameter L           = 25,                // FC1=25; FC2=10; FC3=5
    parameter NUM_CHUNK   = 16,                // FC1=16; FC2=12; FC3=17
    parameter CH_W        = 36,                // FC1=36; FC2=36; FC3=36
    parameter ACC_W       = 40,                // FC1=40; FC2=38; FC3=38
    parameter RELU        = 1,                 // FC1=1; FC2=1; FC3=0
    parameter SCALE_EXP   = 15,                // FC1=15; FC2=14; FC3=13
    parameter WEIGHT_FILE = "fc1_weight.mem",
    parameter BIAS_FILE   = "fc1_bias.mem"
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [15:0] in_data,
    input  wire        in_valid,
    input  wire        out_ready,
    output wire        in_ready,
    output wire [15:0] out_data,   // unsigned code when RELU = 1, signed logit in FC3
    output wire        out_valid
);

    wire        [                   16*L-1:0] stg_x;
    wire                                      stg_calc_full;
    wire                                      stg_next_full;

    wire                                      ctrl_mac_en;
    wire        [$clog2(NUM_CHUNK*N_OUT)-1:0] ctrl_rom_addr;
    wire                                      ctrl_chunk_done;

    wire        [                   16*L-1:0] rom_w;

    wire signed [                   CH_W-1:0] mac_ch_result;
    wire                                      mac_valid;
    wire signed [                  ACC_W-1:0] ob_sum_data;
    wire                                      ob_sum_valid;

    /* verilator lint_off UNUSEDSIGNAL */
    wire                                      ob_ch3_5_en;  // conv-only: NUM_GROUPS is the chunk count here, not input-channel groups
    /* verilator lint_on UNUSEDSIGNAL */

    // ========== Input staging and control ==========
    fc_staging #(
        .L        (L),
        .N_IN     (N_IN),
        .NUM_CHUNK(NUM_CHUNK)
    ) u_fc_staging (
        .clk       (clk),
        .rst_n     (rst_n),
        .in_data   (in_data),
        .in_valid  (in_valid),
        .chunk_done(ctrl_chunk_done),
        .in_ready  (in_ready),
        .calc_full (stg_calc_full),
        .next_full (stg_next_full),
        .x_out     (stg_x)
    );

    fc_ctrl #(
        .N_OUT    (N_OUT),
        .NUM_CHUNK(NUM_CHUNK)
    ) u_fc_ctrl (
        .clk       (clk),
        .rst_n     (rst_n),
        .calc_full (stg_calc_full),
        .next_full (stg_next_full),
        .mac_en    (ctrl_mac_en),
        .rom_addr  (ctrl_rom_addr),
        .chunk_done(ctrl_chunk_done)
    );

    // ========== Weights and multiply-accumulate ==========
    fc_weight_rom #(
        .L        (L),
        .N_OUT    (N_OUT),
        .NUM_CHUNK(NUM_CHUNK),
        .ROM_FILE (WEIGHT_FILE)
    ) u_fc_weight_rom (
        .clk  (clk),
        .addr (ctrl_rom_addr),
        .w_out(rom_w)
    );

    fc_mac #(
        .L   (L),
        .CH_W(CH_W)
    ) u_fc_mac (
        .clk      (clk),
        .rst_n    (rst_n),
        .mac_en   (ctrl_mac_en),
        .w_in     (rom_w),
        .x_in     (stg_x),
        .ch_result(mac_ch_result),
        .mac_valid(mac_valid)
    );

    // ========== Output Buffer (conv block reused) ==========
    output_buffer #(
        .N         (1),
        .C_OUT     (N_OUT),
        .NUM_GROUPS(NUM_CHUNK),
        .CH_W      (CH_W),
        .ACC_W     (ACC_W),
        .BIAS_FILE (BIAS_FILE)
    ) u_output_buffer (
        .clk       (clk),
        .rst_n     (rst_n),
        .ch_result0(mac_ch_result),
        .ch_result1({CH_W{1'b0}}),
        .ch_result2({CH_W{1'b0}}),
        .mac_valid (mac_valid),
        .ch3_5_en  (ob_ch3_5_en),
        .sum_data  (ob_sum_data),
        .sum_valid (ob_sum_valid)
    );

    // ========== Output stage: ReLU & quantization, or the signed quantizer in FC3 ==========
    generate
        if (RELU == 1) begin : GEN_RELU_QUANT
            /* verilator lint_off UNUSEDSIGNAL */
            wire [15:0] rq_data1;  // PACK is 1, so only lane 0 carries a value
            wire [15:0] rq_data2;
            wire        rq_ch_done;  // N is 1, so every entry is the last pixel
            /* verilator lint_on UNUSEDSIGNAL */

            relu_quant #(
                .ACC_W    (ACC_W),
                .N        (1),
                .C_OUT    (N_OUT),
                .PACK     (1),
                .SCALE_EXP(SCALE_EXP)
            ) u_relu_quant (
                .clk        (clk),
                .rst_n      (rst_n),
                .sum_data   (ob_sum_data),
                .sum_valid  (ob_sum_valid),
                .out_ready  (out_ready),
                .out_data0  (out_data),
                .out_data1  (rq_data1),
                .out_data2  (rq_data2),
                .out_ch_done(rq_ch_done),
                .out_valid  (out_valid)
            );
        end else begin : GEN_QUANT_SIGNED
            fc_quant_signed #(
                .ACC_W    (ACC_W),
                .N_OUT    (N_OUT),
                .SCALE_EXP(SCALE_EXP)
            ) u_fc_quant_signed (
                .clk      (clk),
                .rst_n    (rst_n),
                .sum_data (ob_sum_data),
                .sum_valid(ob_sum_valid),
                .out_ready(out_ready),
                .out_data0(out_data),
                .out_valid(out_valid)
            );
        end
    endgenerate

endmodule
