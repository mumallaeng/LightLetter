`timescale 1ns / 1ps

module MAC_array #(
    parameter NUM_ACTIVE_CH = 3
)(
    input  wire         clk,
    input  wire         rst_n,
    input  wire [431:0] win_in,
    input  wire [2:0]   win_valid,
    input  wire [431:0] weight_in,

    output wire [35:0] ch_result0,
    output wire [35:0] ch_result1,
    output wire [35:0] ch_result2,
    output wire        mac_valid
);


// ---------------------------------------------------------------
// Channel 0
// ---------------------------------------------------------------

wire [35:0] c0_row0_sum;
wire [35:0] c0_row1_sum;
wire [35:0] c0_row2_sum;

wire        c0_row0_valid;
wire        c0_row1_valid;
wire        c0_row2_valid; // row0 / row1 / row2를 서로 독립적으로 계산

MAC_unit U_MAC_C0_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[47:0]),
    .weight_row(weight_in[47:0]),
    .row_valid (win_valid[0]),
    .psum_out  (c0_row0_sum),
    .valid_out (c0_row0_valid)
);

MAC_unit U_MAC_C0_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[95:48]),
    .weight_row(weight_in[95:48]),
    .row_valid (win_valid[0]),
    .psum_out  (c0_row1_sum),
    .valid_out (c0_row1_valid)
);

MAC_unit U_MAC_C0_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[143:96]),
    .weight_row(weight_in[143:96]),
    .row_valid (win_valid[0]),
    .psum_out  (c0_row2_sum),
    .valid_out (c0_row2_valid)
);


// ---------------------------------------------------------------
// Channel 1
// ---------------------------------------------------------------

wire [35:0] c1_row0_sum;
wire [35:0] c1_row1_sum;
wire [35:0] c1_row2_sum;

wire        c1_row0_valid;
wire        c1_row1_valid;
wire        c1_row2_valid;

MAC_unit U_MAC_C1_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[191:144]),
    .weight_row(weight_in[191:144]),
    .row_valid (win_valid[1]),
    .psum_out  (c1_row0_sum),
    .valid_out (c1_row0_valid)
);

MAC_unit U_MAC_C1_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[239:192]),
    .weight_row(weight_in[239:192]),
    .row_valid (win_valid[1]),
    .psum_out  (c1_row1_sum),
    .valid_out (c1_row1_valid)
);

MAC_unit U_MAC_C1_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[287:240]),
    .weight_row(weight_in[287:240]),
    .row_valid (win_valid[1]),
    .psum_out  (c1_row2_sum),
    .valid_out (c1_row2_valid)
);


// ---------------------------------------------------------------
// Channel 2
// ---------------------------------------------------------------

wire [35:0] c2_row0_sum;
wire [35:0] c2_row1_sum;
wire [35:0] c2_row2_sum;

wire        c2_row0_valid;
wire        c2_row1_valid;
wire        c2_row2_valid;

MAC_unit U_MAC_C2_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[335:288]),
    .weight_row(weight_in[335:288]),
    .row_valid (win_valid[2]),
    .psum_out  (c2_row0_sum),
    .valid_out (c2_row0_valid)
);

MAC_unit U_MAC_C2_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[383:336]),
    .weight_row(weight_in[383:336]),
    .row_valid (win_valid[2]),
    .psum_out  (c2_row1_sum),
    .valid_out (c2_row1_valid)
);

MAC_unit U_MAC_C2_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[431:384]),
    .weight_row(weight_in[431:384]),
    .row_valid (win_valid[2]),
    .psum_out  (c2_row2_sum),
    .valid_out (c2_row2_valid)
);


// ===============================================================
// 각 row MAC 결과를 FF에 저장
//
//     MAC row0 ─┐
//     MAC row1 ─┼→ FF
//     MAC row2 ─┘
//
// ===============================================================

reg signed [35:0] c0_row0_reg;
reg signed [35:0] c0_row1_reg;
reg signed [35:0] c0_row2_reg;

reg signed [35:0] c1_row0_reg;
reg signed [35:0] c1_row1_reg;
reg signed [35:0] c1_row2_reg;

reg signed [35:0] c2_row0_reg;
reg signed [35:0] c2_row1_reg;
reg signed [35:0] c2_row2_reg;


// ===============================================================
// valid도 데이터와 동일하게 FF를 통과시킴
//
// 데이터가 FF에서 한 단계 지연되므로 valid 역시 동일하게 한 단계 지연
// ===============================================================

reg c0_valid_reg;
reg c1_valid_reg;
reg c2_valid_reg;


always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin

        c0_row0_reg <= 36'd0;
        c0_row1_reg <= 36'd0;
        c0_row2_reg <= 36'd0;

        c1_row0_reg <= 36'd0;
        c1_row1_reg <= 36'd0;
        c1_row2_reg <= 36'd0;

        c2_row0_reg <= 36'd0;
        c2_row1_reg <= 36'd0;
        c2_row2_reg <= 36'd0;

        c0_valid_reg <= 1'b0;
        c1_valid_reg <= 1'b0;
        c2_valid_reg <= 1'b0;

    end else begin

        c0_row0_reg <= c0_row0_sum;
        c0_row1_reg <= c0_row1_sum;
        c0_row2_reg <= c0_row2_sum;

        c1_row0_reg <= c1_row0_sum;
        c1_row1_reg <= c1_row1_sum;
        c1_row2_reg <= c1_row2_sum;

        c2_row0_reg <= c2_row0_sum;
        c2_row1_reg <= c2_row1_sum;
        c2_row2_reg <= c2_row2_sum;

        c0_valid_reg <= c0_row0_valid & c0_row1_valid & c0_row2_valid;
        c1_valid_reg <= c1_row0_valid & c1_row1_valid & c1_row2_valid;
        c2_valid_reg <= c2_row0_valid & c2_row1_valid & c2_row2_valid;

    end
end


// ===============================================================
// FF를 통과한 3개 row 결과를 조합적으로 합산
// ===============================================================

wire signed [35:0] c0_final_sum;
wire signed [35:0] c1_final_sum;
wire signed [35:0] c2_final_sum;

assign c0_final_sum = c0_row0_reg + c0_row1_reg + c0_row2_reg;
assign c1_final_sum = c1_row0_reg + c1_row1_reg + c1_row2_reg;
assign c2_final_sum = c2_row0_reg + c2_row1_reg + c2_row2_reg;


// ===============================================================
// 최종 결과에도 FF를 추가
//
//     row MAC
//        ↓
//       FF
//        ↓
//    row 결과 ADD
//        ↓
//       FF
//        ↓
//    ch_result
//
// 최종 결과와 mac_valid를 같은 클록에 맞춤
// ===============================================================

reg signed [35:0] ch_result0_reg;
reg signed [35:0] ch_result1_reg;
reg signed [35:0] ch_result2_reg;

reg mac_valid_reg;


always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin

        ch_result0_reg <= 36'd0;
        ch_result1_reg <= 36'd0;
        ch_result2_reg <= 36'd0;

        mac_valid_reg <= 1'b0;

    end else begin

        ch_result0_reg <= c0_final_sum;
        ch_result1_reg <= c1_final_sum;
        ch_result2_reg <= c2_final_sum;

        // NUM_ACTIVE_CH개의 채널이 모두 유효할 때만 valid
        if (NUM_ACTIVE_CH >= 3)
            mac_valid_reg <= c0_valid_reg & c1_valid_reg & c2_valid_reg;

        else if (NUM_ACTIVE_CH == 2)
            mac_valid_reg <= c0_valid_reg & c1_valid_reg;

        else
            mac_valid_reg <= c0_valid_reg;
    end
end


// ===============================================================
// 최종 FF 출력을 외부 포트로 연결
// ===============================================================

assign ch_result0 = ch_result0_reg;
assign ch_result1 = ch_result1_reg;
assign ch_result2 = ch_result2_reg;

assign mac_valid = mac_valid_reg;


endmodule