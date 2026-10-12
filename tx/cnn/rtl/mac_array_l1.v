`timescale 1ns / 1ps

module mac_array_l1 (
    input wire         clk,
    input wire         rst_n,
    input wire [143:0] win_in,
    input wire         win_valid,
    input wire [143:0] weight_in,

    output wire [35:0] ch_result0,
    output wire        mac_valid
);


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
        .row_valid (win_valid),
        .psum_out  (c0_row0_sum)
    );

    MAC_unit U_MAC_C0_R1 (
        .clk       (clk),
        .rst_n     (rst_n),
        .win_row   (win_in[95:48]),
        .weight_row(weight_in[95:48]),
        .row_valid (win_valid),
        .psum_out  (c0_row1_sum)
    );

    MAC_unit U_MAC_C0_R2 (
        .clk       (clk),
        .rst_n     (rst_n),
        .win_row   (win_in[143:96]),
        .weight_row(weight_in[143:96]),
        .row_valid (win_valid),
        .psum_out  (c0_row2_sum)
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

    // ===============================================================
    // valid는 아래 공통 3단 파이프라인에서 관리
    // 유닛별 valid 및 row valid AND 로직 제거
    // ===============================================================
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin

            c0_row0_reg  <= 36'd0;
            c0_row1_reg  <= 36'd0;
            c0_row2_reg  <= 36'd0;
        end else begin

            c0_row0_reg  <= c0_row0_sum;
            c0_row1_reg  <= c0_row1_sum;
            c0_row2_reg  <= c0_row2_sum;
        end
    end


    // ===============================================================
    // FF를 통과한 3개 row 결과를 조합적으로 합산
    // ===============================================================

    wire signed [35:0] c0_final_sum;

    assign c0_final_sum = c0_row0_reg + c0_row1_reg + c0_row2_reg;

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

    // 공통 valid: 곱셈 FF -> row 합 FF -> 최종 결과 FF
    reg mul_valid_reg;
    reg row_valid_reg;
    reg mac_valid_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mul_valid_reg <= 1'b0;
            row_valid_reg <= 1'b0;
            mac_valid_reg <= 1'b0;
        end else begin
            mul_valid_reg <= win_valid;
            row_valid_reg <= mul_valid_reg;
            mac_valid_reg <= row_valid_reg;
        end
    end


    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ch_result0_reg <= 36'd0;
        end else begin
            ch_result0_reg <= c0_final_sum;
        end
    end


    // ===============================================================
    // 최종 FF 출력을 외부 포트로 연결
    // ===============================================================

    assign ch_result0 = ch_result0_reg;
    assign mac_valid  = mac_valid_reg;


endmodule
