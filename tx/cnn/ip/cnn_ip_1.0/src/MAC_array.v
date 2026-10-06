`timescale 1ns / 1ps

module MAC_array #(
    // 기존 상위 모듈과의 호환성을 위해 유지
    // 공통 valid 구조에서는 valid 생성에 사용하지 않음
    parameter NUM_ACTIVE_CH = 3
)(
    input  wire         clk,
    input  wire         rst_n,
    input  wire [431:0] win_in,
    input  wire [2:0]   win_valid,
    input  wire [431:0] weight_in,
    // 곱셈기별 enable: mul_en[k] <-> win_in[k*16 +: 16]
    input  wire [26:0]  mul_en,

    output wire [35:0] ch_result0,
    output wire [35:0] ch_result1,
    output wire [35:0] ch_result2,
    output wire        mac_valid
);


// ---------------------------------------------------------------
// common_valid
//
// 세 채널의 win_valid가 동일한 타이밍으로 동작하는 구조
// 출력 valid 파이프라인에만 사용 (곱셈 결과 마스킹은 mul_en이 담당)
// ---------------------------------------------------------------

wire common_valid = win_valid[0];


// ---------------------------------------------------------------
// Channel 0
// ---------------------------------------------------------------

wire [35:0] c0_row0_sum;
wire [35:0] c0_row1_sum;
wire [35:0] c0_row2_sum;

MAC_unit U_MAC_C0_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[47:0]),
    .weight_row(weight_in[47:0]),
    .mul_en    (mul_en[2:0]),
    .psum_out  (c0_row0_sum)
);

MAC_unit U_MAC_C0_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[95:48]),
    .weight_row(weight_in[95:48]),
    .mul_en    (mul_en[5:3]),
    .psum_out  (c0_row1_sum)
);

MAC_unit U_MAC_C0_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[143:96]),
    .weight_row(weight_in[143:96]),
    .mul_en    (mul_en[8:6]),
    .psum_out  (c0_row2_sum)
);


// ---------------------------------------------------------------
// Channel 1
// ---------------------------------------------------------------

wire [35:0] c1_row0_sum;
wire [35:0] c1_row1_sum;
wire [35:0] c1_row2_sum;

MAC_unit U_MAC_C1_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[191:144]),
    .weight_row(weight_in[191:144]),
    .mul_en    (mul_en[11:9]),
    .psum_out  (c1_row0_sum)
);

MAC_unit U_MAC_C1_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[239:192]),
    .weight_row(weight_in[239:192]),
    .mul_en    (mul_en[14:12]),
    .psum_out  (c1_row1_sum)
);

MAC_unit U_MAC_C1_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[287:240]),
    .weight_row(weight_in[287:240]),
    .mul_en    (mul_en[17:15]),
    .psum_out  (c1_row2_sum)
);


// ---------------------------------------------------------------
// Channel 2
// ---------------------------------------------------------------

wire [35:0] c2_row0_sum;
wire [35:0] c2_row1_sum;
wire [35:0] c2_row2_sum;

MAC_unit U_MAC_C2_R0 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[335:288]),
    .weight_row(weight_in[335:288]),
    .mul_en    (mul_en[20:18]),
    .psum_out  (c2_row0_sum)
);

MAC_unit U_MAC_C2_R1 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[383:336]),
    .weight_row(weight_in[383:336]),
    .mul_en    (mul_en[23:21]),
    .psum_out  (c2_row1_sum)
);

MAC_unit U_MAC_C2_R2 (
    .clk       (clk),
    .rst_n     (rst_n),
    .win_row   (win_in[431:384]),
    .weight_row(weight_in[431:384]),
    .mul_en    (mul_en[26:24]),
    .psum_out  (c2_row2_sum)
);


// ===============================================================
// 공통 valid를 데이터와 동일하게 3단 FF로 지연
//
//     common_valid
//          ↓
//     mul_valid_reg    : MAC_unit 내부 곱셈 결과 FF에 대응
//          ↓
//     row_valid_reg    : row 합산 결과 FF에 대응
//          ↓
//     mac_valid_reg    : 최종 채널 결과 FF에 대응
//
// 유닛별 valid FF와 채널별 AND 로직 대신 공통으로 관리
// ===============================================================

reg mul_valid_reg;
reg row_valid_reg;
reg mac_valid_reg;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        mul_valid_reg <= 1'b0;
        row_valid_reg <= 1'b0;
        mac_valid_reg <= 1'b0;
    end else begin
        mul_valid_reg <= common_valid;
        row_valid_reg <= mul_valid_reg;
        mac_valid_reg <= row_valid_reg;
    end
end


// ===============================================================
// 각 row MAC 결과를 FF에 저장
//
//     MAC row0 ─┐
//     MAC row1 ─┼→ FF
//     MAC row2 ─┘
//
// 데이터 레지스터는 기존 구조 그대로 유지
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
// 최종 결과를 FF에 저장
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
// 공통 valid 파이프라인으로 최종 결과와 mac_valid의 타이밍 정렬
// ===============================================================

reg signed [35:0] ch_result0_reg;
reg signed [35:0] ch_result1_reg;
reg signed [35:0] ch_result2_reg;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin

        ch_result0_reg <= 36'd0;
        ch_result1_reg <= 36'd0;
        ch_result2_reg <= 36'd0;

    end else begin

        ch_result0_reg <= c0_final_sum;
        ch_result1_reg <= c1_final_sum;
        ch_result2_reg <= c2_final_sum;

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
