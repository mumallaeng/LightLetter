`timescale 1ns / 1ps
// Partial Sum: 3-channel sum + cross-group accumulate + bias (combinational).

module partial_sum #(
    parameter CH_W  = 36,
    parameter ACC_W = 40
) (
    input  wire signed [CH_W-1:0]  ch_result0,
    input  wire signed [CH_W-1:0]  ch_result1,
    input  wire signed [CH_W-1:0]  ch_result2,
    input  wire                    mac_valid,
    input  wire                    first_phase,  // group_cnt == 0
    input  wire                    last_phase,   // group_cnt == NUM_GROUPS-1
    input  wire signed [ACC_W-1:0] buf_rdata,
    input  wire signed [31:0]      bias_rdata,
    output wire signed [ACC_W-1:0] sum,          // -> Buffer Controller
    output wire                    we,
    output wire signed [ACC_W-1:0] sum_data,     // final value
    output wire                    sum_valid
);

    // sign-extend to the accumulate width
    wire signed [ACC_W-1:0] r0   = $signed({{(ACC_W-CH_W){ch_result0[CH_W-1]}}, ch_result0});
    wire signed [ACC_W-1:0] r1   = $signed({{(ACC_W-CH_W){ch_result1[CH_W-1]}}, ch_result1});
    wire signed [ACC_W-1:0] r2   = $signed({{(ACC_W-CH_W){ch_result2[CH_W-1]}}, ch_result2});
    wire signed [ACC_W-1:0] bias = $signed({{(ACC_W-32){bias_rdata[31]}}, bias_rdata});

    wire signed [ACC_W-1:0] raw_sum = r0 + r1 + r2;

    // first group starts from bias, later groups add to the stored value
    assign sum = first_phase ? (raw_sum + bias) : (buf_rdata + raw_sum);
    assign we  = mac_valid;

    assign sum_data  = last_phase ? sum : $signed({ACC_W{1'b0}});
    assign sum_valid = last_phase & mac_valid;

endmodule
