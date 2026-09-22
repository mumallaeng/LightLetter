`timescale 1ns / 1ps

module power_top (
    input  wire        clk,
    input  wire        rst,
    input  wire [39:0] i_fft_core_data,
    input  wire        i_fft_core_valid,
    output wire [39:0] fft_mag,
    output wire        fft_mag_valid
);
    wire [39:0] power_result;

    power u_power (
        .i_fft_core_data (i_fft_core_data),
        .power_result   (power_result)
    );

    power_mem u_power_mem (
        .clk              (clk),
        .rst              (rst),
        .power_result     (power_result),
        .i_fft_core_valid (i_fft_core_valid),
        .fft_mag          (fft_mag),
        .fft_mag_valid    (fft_mag_valid)
    );
endmodule


// Power 계산: Re² + Im²
module power (
    input  wire [39:0] i_fft_core_data,
    output wire [39:0] power_result
);
    wire signed [19:0] fft_re, fft_im;
    wire signed [39:0] re_square, im_square;
    wire        [40:0] power_sum;

    // [39:20]: 실수부, [19:0]: 허수부
    assign fft_re = $signed(i_fft_core_data[39:20]);
    assign fft_im = $signed(i_fft_core_data[19:0]);

    assign re_square = fft_re * fft_re;
    assign im_square = fft_im * fft_im;
    assign power_sum = {1'b0, re_square} + {1'b0, im_square};

    // 최대값 2^39: unsigned 40비트에 표현 가능
    assign power_result = power_sum[39:0];
endmodule


// Power 결과 128개 저장 후 순차 전송
module power_mem (
    input  wire        clk,
    input  wire        rst,
    input  wire [39:0] power_result,
    input  wire        i_fft_core_valid,
    output reg  [39:0] fft_mag,
    output reg         fft_mag_valid
);
    reg [39:0] mem [0:127];
    reg [6:0]  count;
    reg [1:0]  state;  // 0: 수집, 1: 전송, 2: 마지막 수신 마무리

    wire [6:0] addr;
    wire       collecting, sending, last_word;
    wire       write_en, count_en;

    assign collecting = (state == 2'd0);
    assign sending    = (state == 2'd1);
    assign last_word  = (count == 7'd127);
    assign write_en   = !rst && collecting && i_fft_core_valid;
    assign count_en   = write_en || sending;
    assign addr       = count;

    always @(posedge clk) begin
        // 매 클럭 읽기. valid=0인 데이터는 RX에서 무시
        fft_mag <= mem[addr];

        if (rst) begin
            count         <= 7'd0;
            fft_mag_valid <= 1'b0;
            state         <= 2'd0;
        end else begin
            // 유효한 입력 저장
            if (write_en)
                mem[addr] <= power_result;

            // 저장·출력 위치 갱신
            if (state == 2'd3)
                count <= 7'd0;
            else if (count_en)
                count <= count + 7'd1;

            // 메모리 출력과 valid를 함께 갱신
            fft_mag_valid <= sending;

            // 상태 전환
            case (state)
                2'd0: begin
                    if (i_fft_core_valid && last_word)
                        state <= 2'd1;
                end

                2'd1: begin
                    if (last_word)
                        state <= 2'd2;
                end

                // 이 에지에서 RX가 마지막 값을 수신
                2'd2: state <= 2'd0;

                default: state <= 2'd0;
            endcase
        end
    end
endmodule