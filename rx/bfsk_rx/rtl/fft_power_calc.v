`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// 모듈명: fft_power_calc
// 설명: FFT 복소수 출력의 Power(Re^2 + Im^2)를 rx_top 입력으로 변환한다.
// 대상: 단일 채널, signed fixed-point FFT 출력. floating-point는 지원하지 않는다.
// TDATA: 하위 padded slot은 Re, 상위 padded slot은 Im이다.
// 각 slot은 실제 성분 폭을 8bit 경계로 올림한 폭이며 padding은 무시한다.
// 기본 12bit 설정: Re = TDATA[11:0], Im = TDATA[27:16].
// TDATA[15:12], [31:28]은 padding이다. 전체 TDATA 폭은 32bit다.
// 출력: raw power >> POWER_SHIFT, 출력 폭 초과 시 최대값으로 포화한다.
// 기본 12bit 성분의 제곱합은 최대 2^23이므로 POWER_SHIFT=0으로 보존한다.
// 입력 수락 edge를 포함해 세 번째 rising edge에 결과를 출력한다.
// 연속 입력은 매 클록 처리 가능하며 reset 중에는 입력을 수락하지 않는다.
// rx_top은 ready가 없으므로 출력은 매 valid 클록에 반드시 소비되어야 한다.
//////////////////////////////////////////////////////////////////////////////////
module fft_power_calc #(
    parameter FFT_OUT_W = 12,
    parameter POWER_W = 24,
    parameter POWER_SHIFT = (2*FFT_OUT_W > POWER_W) ? (2*FFT_OUT_W-POWER_W) : 0
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK",
       X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS_FFT, ASSOCIATED_RESET rst_n" *)
    input wire clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST",
       X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input wire rst_n,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_FFT TDATA" *)
    input wire [2*(((FFT_OUT_W+7)/8)*8)-1:0] s_axis_fft_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_FFT TVALID" *)
    input wire s_axis_fft_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_FFT TREADY" *)
    output wire s_axis_fft_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_FFT TLAST" *)
    input wire s_axis_fft_tlast,

    output reg [POWER_W-1:0] fft_mag,
    output reg fft_mag_valid,
    output reg fft_mag_last,
    output reg power_saturated
);
    localparam FFT_SLOT_W = ((FFT_OUT_W+7)/8)*8;
    localparam SQUARE_W = 2*FFT_OUT_W;
    localparam SUM_W = SQUARE_W+1;
    // Wide enough for either the raw sum or the requested output width.
    localparam RESIZE_W = (SUM_W > POWER_W+1) ? SUM_W : POWER_W+1;

    wire signed [FFT_OUT_W-1:0] fft_re = s_axis_fft_tdata[FFT_OUT_W-1:0];
    wire signed [FFT_OUT_W-1:0] fft_im = s_axis_fft_tdata[FFT_SLOT_W+:FFT_OUT_W];
    wire input_accept = s_axis_fft_tvalid && s_axis_fft_tready;
    assign s_axis_fft_tready = rst_n;

    reg [SQUARE_W-1:0] re_squared, im_squared;
    reg [SUM_W-1:0] power_sum;
    reg square_valid, sum_valid;
    reg square_last, sum_last;

    wire [RESIZE_W-1:0] extended_sum = {{(RESIZE_W-SUM_W){1'b0}}, power_sum};
    wire [RESIZE_W-1:0] shifted_power = extended_sum >> POWER_SHIFT;
    wire too_large = |shifted_power[RESIZE_W-1:POWER_W];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            re_squared <= 0;
            im_squared <= 0;
            power_sum <= 0;
            square_valid <= 0;
            sum_valid <= 0;
            square_last <= 0;
            sum_last <= 0;
            fft_mag <= 0;
            fft_mag_valid <= 0;
            fft_mag_last <= 0;
            power_saturated <= 0;
        end else begin
            // 1단계: signed 성분을 곱한 양수 결과를 전체 폭으로 저장한다.
            square_valid <= input_accept;
            square_last <= input_accept && s_axis_fft_tlast;
            if (input_accept) begin
                re_squared <= fft_re * fft_re;
                im_squared <= fft_im * fft_im;
            end

            // 2단계: carry를 포함해 두 제곱값을 더한다.
            sum_valid <= square_valid;
            sum_last <= square_valid && square_last;
            if (square_valid)
                power_sum <= {1'b0, re_squared} + {1'b0, im_squared};

            // 3단계: 모든 bin에 동일한 shift를 적용한다. wrap-around는 금지한다.
            fft_mag_valid <= sum_valid;
            fft_mag_last <= sum_valid && sum_last;
            power_saturated <= sum_valid && too_large;
            if (sum_valid)
                fft_mag <= too_large ? {POWER_W{1'b1}} : shifted_power[POWER_W-1:0];
        end
    end
endmodule
