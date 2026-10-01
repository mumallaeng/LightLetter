`timescale 1ns / 1ps

module tb_ADC_butterfly_final();
    parameter integer NOISY = 0;
    localparam ADC_FILE = NOISY ? "fft_input_10000hz_noisy.mem"
                               : "fft_input_10000hz_clean.mem";
    localparam ROM_FILE = "twiddle_128_q14.mem";
    localparam time TIME_BUDGET_NS = 800000;

    // 1. 시험할 회로 연결: 외부 ROM -> Butterfly -> Y0/Y1
    reg clk = 0, rst = 1, i_read_data_valid = 0;
    always #10 clk = ~clk;  // 50MHz
    reg signed [19:0] a_re = 0, a_im = 0, b_re = 0, b_im = 0;
    reg [5:0] twiddle_addr = 0;
    wire [31:0] twiddle_factor;
    wire signed [19:0] y0_re, y0_im, y1_re, y1_im;
    wire o_bf_read_data_valid;

    twiddle_rom #(.MEM_FILE(ROM_FILE)) u_twiddle_rom (
        .clk(clk), .twiddle_addr(twiddle_addr), .twiddle_factor(twiddle_factor)
    );
    butterfly dut (
        .clk(clk), .rst(rst), .i_read_data_valid(i_read_data_valid),
        .a_re(a_re), .a_im(a_im), .b_re(b_re), .b_im(b_im),
        .twiddle_factor(twiddle_factor),
        .y0_re(y0_re), .y0_im(y0_im), .y1_re(y1_re), .y1_im(y1_im),
        .o_bf_read_data_valid(o_bf_read_data_valid)
    );

    // 2. 파형 관찰용 별명: 내부 신호를 TB 화면에서 쉽게 찾기 위한 선언
    wire [2:0] bf_state = dut.state;
    wire [31:0] rom_word = twiddle_factor, stored_factor = dut.twiddle_factor_reg;
    wire signed [15:0] w_re = dut.u_multiplier.w_re, w_im = dut.u_multiplier.w_im;
    wire signed [20:0] t_re = dut.t_re, t_im = dut.t_im;
    wire signed [20:0] t_re_reg = dut.t_re_reg, t_im_reg = dut.t_im_reg;
    wire [79:0] actual_y = {y0_re, y0_im, y1_re, y1_im};

    // 3. 파일 데이터와 기대값. 64비트는 기준 계산 중 값이 잘리지 않도록 사용.
    reg [39:0] adc_samples [0:127];  // {real20, imaginary20}
    reg [31:0] coefficients [0:63]; // {real16, imaginary16}, Q14
    reg [31:0] expected_w;
    reg signed [63:0] ar, ai, br, bi, wr, wi;
    reg signed [63:0] expected_t_re, expected_t_im;
    reg signed [63:0] expected_y0_re, expected_y0_im, expected_y1_re, expected_y1_im;
    reg [79:0] expected_y, previous_y;
    integer coefficient_index, pair_index, n, passed = 0;
    integer accepted_count = 0, completed_count = 0;
    time accepted_time, previous_accept_time = 0, y_valid_time;
    time test_start_time = 0, test_elapsed_time = 0;
    reg test_started = 0, test_finished = 0;

    // 반복문 횟수와 별개로 실제 회로가 수락/완료한 횟수도 확인합니다.
    always @(posedge clk) begin
        if (!rst) begin
            if (bf_state == 0 && i_read_data_valid) accepted_count = accepted_count + 1;
            if (o_bf_read_data_valid) completed_count = completed_count + 1;
        end
    end

    // 기존 ADC 시험과 동일한 7비트 역순 주소: 예) 1 -> 64
    function [6:0] reverse7;
        input [6:0] value;
        begin
            reverse7 = {value[0],value[1],value[2],value[3],value[4],value[5],value[6]};
        end
    endfunction

    // check(조건, 설명): 조건이 1이 아니면 원인을 출력하고 중단합니다.
    task check;
        input condition;
        input [8*80-1:0] message;
        begin
            if (condition !== 1'b1)
                $fatal(1, "FAIL W=%0d pair=%0d time=%0t: %0s",
                       coefficient_index, pair_index, $time, message);
        end
    endtask

    function fits_signed;
        input signed [63:0] value;
        input integer bits;
        begin
            fits_signed = (value >= -(64'sd1 << (bits-1))) &&
                          (value <   (64'sd1 << (bits-1)));
        end
    endfunction

    // 다음 상승 에지 직후 상태, valid, 출력값을 함께 확인합니다.
    // #1은 실제 회로 지연이 아니라 nonblocking 대입이 반영되기를 기다리는 시간입니다.
    task check_edge;
        input [2:0] expected_state;
        input expected_valid;
        input [79:0] wanted_y;
        begin
            @(posedge clk); #1;
            check(bf_state === expected_state, "State mismatch");
            check(o_bf_read_data_valid === expected_valid, "Output valid mismatch");
            check(actual_y === wanted_y, "Y mismatch / output hold failure");
        end
    endtask

    // A/B/W로 정답을 직접 계산합니다. DUT 결과를 정답으로 사용하지 않습니다.
    task calculate_expected;
        begin
            ar = a_re; ai = a_im; br = b_re; bi = b_im;
            expected_w = coefficients[coefficient_index];
            wr = $signed(expected_w[31:16]); wi = $signed(expected_w[15:0]);
            expected_t_re = (br*wr - bi*wi) >>> 14;
            expected_t_im = (br*wi + bi*wr) >>> 14;
            expected_y0_re = ar + expected_t_re; expected_y0_im = ai + expected_t_im;
            expected_y1_re = ar - expected_t_re; expected_y1_im = ai - expected_t_im;
            check(fits_signed(expected_t_re,21) && fits_signed(expected_t_im,21), "T overflow");
            check(fits_signed(expected_y0_re,20) && fits_signed(expected_y0_im,20) &&
                  fits_signed(expected_y1_re,20) && fits_signed(expected_y1_im,20), "Y overflow");
            expected_y = {expected_y0_re[19:0], expected_y0_im[19:0],
                          expected_y1_re[19:0], expected_y1_im[19:0]};
        end
    endtask

    // 4. 한 쌍 시험: 입력 준비 -> E0 수락 -> E1 T 저장 -> E2 결과 -> E3 복귀
    task test_one_pair;
        begin
            @(negedge clk);
            {a_re,a_im} = adc_samples[reverse7(pair_index*2)];
            {b_re,b_im} = adc_samples[reverse7(pair_index*2+1)];
            twiddle_addr = coefficient_index;
            calculate_expected;
            previous_y = actual_y;
            if (pair_index == 0) begin // W가 바뀌면 동기식 ROM 읽기 1클럭 대기
                @(posedge clk); #1;
                @(negedge clk);
            end
            check(rom_word === expected_w, "External ROM mismatch");
            i_read_data_valid = 1;
            check_edge(2, 0, previous_y); // E0: WAIT_T, 입력 저장
            accepted_time = $time - 1;
            if (passed != 0)
                check(accepted_time-previous_accept_time == ((pair_index==0) ? 100 : 80),
                      "Input acceptance interval mismatch");
            previous_accept_time = accepted_time;
            check($signed(dut.a_re_reg) === ar && $signed(dut.a_im_reg) === ai &&
                  $signed(dut.b_re_reg) === br && $signed(dut.b_im_reg) === bi &&
                  stored_factor === expected_w, "A/B/W capture mismatch");
            @(negedge clk);
            i_read_data_valid = 0;
            check_edge(3, 0, previous_y); // E1: ADD_SUB, T 저장
            check({w_re,w_im} === expected_w && stored_factor === expected_w, "Stored W mismatch");
            check($signed(t_re) === expected_t_re && $signed(t_im) === expected_t_im &&
                  $signed(t_re_reg) === expected_t_re && $signed(t_im_reg) === expected_t_im,
                  "T mismatch");
            check_edge(4, 1, expected_y); // E2: DONE, Y 출력
            y_valid_time = $time - 1;
            check(y_valid_time-accepted_time == 40, "Latency must be 40 ns");
            if (pair_index == 32 && coefficient_index % 16 == 0)
                $display("W[%0d]=%h A=(%0d,%0d) B=(%0d,%0d) T=(%0d,%0d) Y0=(%0d,%0d) Y1=(%0d,%0d)",
                         coefficient_index, expected_w, a_re,a_im,b_re,b_im,
                         t_re,t_im,y0_re,y0_im,y1_re,y1_im);
            check_edge(0, 0, expected_y); // E3: IDLE, Y 유지
            check($time-1-y_valid_time == 20, "Valid width must be 20 ns");
            passed = passed + 1;
        end
    endtask

    // 5. 전체 시험 순서: 파일 읽기 -> 리셋 -> W[1]~W[63] × 64쌍 -> 최종 판정
    initial begin
        check(NOISY == 0 || NOISY == 1, "NOISY must be 0 or 1");
        $readmemh(ADC_FILE, adc_samples);
        $readmemh(ROM_FILE, coefficients);
        for (n=0; n<128; n=n+1)
            check((^adc_samples[n]) !== 1'bx, "ADC file missing/invalid data");
        for (n=0; n<64; n=n+1)
            check((^coefficients[n]) !== 1'bx, "Coefficient file missing/invalid data");
        repeat (3) @(posedge clk);
        #1;
        check(bf_state === 0 && o_bf_read_data_valid === 0 && actual_y === 80'd0, "Reset failed");
        @(negedge clk);
        rst = 0;
        test_start_time = $time;
        test_started = 1;
        for (coefficient_index=1; coefficient_index<64; coefficient_index=coefficient_index+1) begin
            for (pair_index=0; pair_index<64; pair_index=pair_index+1)
                test_one_pair;
            $display("PASS W[%0d]=%h: 64 ADC pairs", coefficient_index, expected_w);
        end
        check(passed==4032 && accepted_count==4032 && completed_count==4032, "Test count mismatch");
        test_elapsed_time = $time - 1 - test_start_time;
        check(test_elapsed_time <= TIME_BUDGET_NS, "800 us budget exceeded");
        test_finished = 1;
        $display("TOTAL TIME PASS: count=%0d, elapsed=%0d ns, budget=%0d ns",
                 passed, test_elapsed_time, TIME_BUDGET_NS);
        $display("ADC BUTTERFLY ROM PASS: %s, %0d results", ADC_FILE, passed);
        $display("TIMING PASS: latency=40 ns, valid width=20 ns, acceptance interval=80 ns (W boundary=100 ns)");
        $display("SCOPE: independent ADC pair tests, not one FFT frame or post-route timing.");
        $finish;
    end

    // 완료하지 못하고 멈추는 경우도 시간 초과로 잡습니다.
    initial begin
        wait (test_started);
        #(TIME_BUDGET_NS + 2);
        check(test_finished, "TIMEOUT: test did not finish within 800 us");
    end
endmodule
