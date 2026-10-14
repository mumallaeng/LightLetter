`timescale 1ns / 1ps

// Zero Gating Array: 입력 픽셀이 0이면 해당 곱셈기의 입력(win_in / weight_in)을 이전 값으로 유지
//   is_zero[k]   <= (win_next[k] == 0)          : 곱셈 결과 마스킹용 (mul_en = cal_valid_rt & ~is_zero)
//   win_in[k]    <= win_next[k]    (en = ~zero) : 0이면 hold -> 곱셈기 입력 토글 없음
//   weight_in[k] <= weight_next[k] (en = ~zero)
//   cal_valid_rt <= cal_valid                   : 데이터와 같은 단으로 정렬
//
// 데이터 레지스터는 리셋 없음 (mul_en 으로 마스킹되므로 초기값 무관)
//   -> DSP48 입력 레지스터(AREG/BREG + CE)로 흡수되어 fabric FF 가 늘지 않는다
module zero_gating_array #(
    parameter N = 27
) (
    input                 clk,
    input                 rst_n,
    input      [N*16-1:0] win_next,
    input      [N*16-1:0] weight_next,
    input                 cal_valid,
    output reg [N*16-1:0] win_in,
    output reg [N*16-1:0] weight_in,
    output reg [   N-1:0] is_zero,
    output reg            cal_valid_rt
);
    integer k;

    // ----- 제어 신호 -----
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            is_zero      <= {N{1'b1}};
            cal_valid_rt <= 1'b0;
        end else begin
            cal_valid_rt <= cal_valid;
            for (k = 0; k < N; k = k + 1) is_zero[k] <= (win_next[k*16+:16] == 16'd0);
        end
    end

    // ----- 데이터 (0이면 hold) -----
    always @(posedge clk) begin
        for (k = 0; k < N; k = k + 1) begin
            if (win_next[k*16+:16] != 16'd0) begin
                win_in[k*16+:16]    <= win_next[k*16+:16];
                weight_in[k*16+:16] <= weight_next[k*16+:16];
            end
        end
    end
endmodule
