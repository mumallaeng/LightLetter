`timescale 1ns / 1ps

module fft_buffer #(
	parameter ADC_BIT = 12,
	parameter FFT_W   = 40
) (
	input  wire                 clk,
	input  wire                 rst,
	// from ADC
	input  wire [ADC_BIT - 1:0] i_adc_data,
	input  wire                 i_adc_data_valid,
	// Frame handshake
	output reg                  o_frame_valid,
	input  wire                 i_frame_ready,
	// Data pair handshake
	output reg                  o_buf_data_valid,
	input  wire                 i_buf_data_ready,

	output wire [FFT_W - 1:0]   o_buf_data0,
	output wire [FFT_W - 1:0]   o_buf_data1,
	output wire                 o_overflow
);

	localparam REAL_W = FFT_W / 2;

	localparam ST_IDLE = 2'd0, ST_READ = 2'd1, ST_SEND = 2'd2;

	// 한 주소에 샘플 두 개 저장
	reg [2*ADC_BIT - 1:0] buffer0 [0:63];
	reg [2*ADC_BIT - 1:0] buffer1 [0:63];

	reg [1:0] c_state, n_state;

	// ADC 수집 관리
	reg [6:0] adc_cnt_reg, adc_cnt_next; // 0~127
	reg       wsel_reg, wsel_next;       // select buffer to write

	reg [ADC_BIT - 1:0] sample_reg, sample_next; // 짝수 sample 임시 보관

	// 프레임 저장 상태
	reg full0_reg, full0_next;
	reg full1_reg, full1_next;

	// Core 전달 관리
	reg [5:0] pair_cnt_reg, pair_cnt_next; // 데이터 한쌍
	reg       rsel_reg, rsel_next;         // select buffer to read

	reg overflow_reg, overflow_next;       // 버퍼 사용 중일 때 데이터 들어오면 1

	// 동기식 메모리 읽기 결과
	reg [2*ADC_BIT - 1:0] rdata0;
	reg [2*ADC_BIT - 1:0] rdata1;

	// 메모리 제어
	reg w_mem0_wen, w_mem0_ren;
	reg w_mem1_wen, w_mem1_ren;
    // write full : 버퍼 사용 여부 / read full :  버퍼에 완성된 프레임이 있는지
	wire write_buf_full, read_buf_full;

	wire [2*ADC_BIT - 1:0] read_pair;
	wire [  ADC_BIT - 1:0] sample0; // 먼저 수집한 샘플 x[2*p]
	wire [  ADC_BIT - 1:0] sample1; // 나중에 수집한 샘플 x[2*p + 1]

	assign write_buf_full = wsel_reg ? full1_reg : full0_reg;
	assign read_buf_full  = rsel_reg ? full1_reg : full0_reg;
	assign read_pair      = rsel_reg ? rdata1 : rdata0;

	assign sample0 = read_pair[2*ADC_BIT - 1:ADC_BIT];
	assign sample1 = read_pair[ADC_BIT - 1:0];

	// Real: unsigned ADC 값을 0 확장
	// Imaginary: 0
	assign o_buf_data0 = {{(REAL_W-ADC_BIT){1'b0}}, sample0, {REAL_W{1'b0}}};
	assign o_buf_data1 = {{(REAL_W-ADC_BIT){1'b0}},	sample1, {REAL_W{1'b0}}};
	assign o_overflow = overflow_reg;

	// ========================================
	// 1. 현재값 갱신
	// ========================================
	always @(posedge clk or posedge rst) begin
		if (rst) begin
			c_state      <= ST_IDLE;
			adc_cnt_reg  <= 7'd0;
			wsel_reg     <= 1'b0;
			sample_reg   <= {ADC_BIT{1'b0}};

			full0_reg    <= 1'b0;
			full1_reg    <= 1'b0;

			pair_cnt_reg <= 6'd0;
			rsel_reg     <= 1'b0;

			overflow_reg <= 1'b0;
		end else begin
			c_state      <= n_state;

			adc_cnt_reg  <= adc_cnt_next;
			wsel_reg     <= wsel_next;
			sample_reg   <= sample_next;

			full0_reg    <= full0_next;
			full1_reg    <= full1_next;

			pair_cnt_reg <= pair_cnt_next;
			rsel_reg     <= rsel_next;

			overflow_reg <= overflow_next;
		end
	end

	// ========================================
	// 2. 다음값 및 제어 출력 생성
	// ========================================
	always @(*) begin
		n_state       = c_state;

		adc_cnt_next  = adc_cnt_reg;
		wsel_next     = wsel_reg;
		sample_next   = sample_reg;

		full0_next    = full0_reg;
		full1_next    = full1_reg;

		pair_cnt_next = pair_cnt_reg;
		rsel_next     = rsel_reg;

		overflow_next = overflow_reg;

		o_frame_valid    = 1'b0;
		o_buf_data_valid = 1'b0;

		w_mem0_wen = 1'b0;
		w_mem1_wen = 1'b0;
		w_mem0_ren = 1'b0;
		w_mem1_ren = 1'b0;

		if (!rst) begin
			// --------------------------------
			// ADC 수집
			// 아래 전달 FSM과 독립적으로 동작
			// --------------------------------
			if (i_adc_data_valid) begin
				if (write_buf_full) begin
					// 기존 프레임 보존, 새 샘플 누락 표시
					overflow_next = 1'b1;
				end else begin
					if (adc_cnt_reg[0] == 1'b0) begin
						// 첫 번째 샘플 임시 보관
						sample_next = i_adc_data;
					end else begin
						// 두 번째 샘플 도착: 한 쌍 저장
						if (wsel_reg == 1'b0) begin
							w_mem0_wen = 1'b1;
						end else begin
							w_mem1_wen = 1'b1;
						end
					end

					if (adc_cnt_reg == 7'd127) begin
						adc_cnt_next = 7'd0;
						wsel_next  = ~wsel_reg;

						if (wsel_reg == 1'b0) begin
							full0_next = 1'b1;
						end else begin
							full1_next = 1'b1;
						end
					end else begin
						adc_cnt_next = adc_cnt_reg + 7'd1;
					end
				end
			end

			// --------------------------------
			// Core로 프레임 전달
			// --------------------------------
			case (c_state)
				ST_IDLE: begin
					pair_cnt_next = 6'd0;
					o_frame_valid = read_buf_full;
					if (o_frame_valid && i_frame_ready) begin
						n_state = ST_READ;
					end
				end

				ST_READ: begin
					// 다음 상승 에지에서 메모리 읽기
					if (rsel_reg == 1'b0) begin
						w_mem0_ren = 1'b1;
					end else begin
						w_mem1_ren = 1'b1;
					end
					n_state = ST_SEND;
				end

				ST_SEND: begin
					o_buf_data_valid = 1'b1;

					// 가져갈 때까지 데이터와 valid 유지
					if (o_buf_data_valid && i_buf_data_ready) begin
						if (pair_cnt_reg < 6'd63) begin
							pair_cnt_next = pair_cnt_reg + 6'd1;
							n_state       = ST_READ;
						end else begin
							// 마지막 쌍 전달 완료
							pair_cnt_next = 6'd0;
							rsel_next   = ~rsel_reg;
							n_state       = ST_IDLE;

							// 전달한 버퍼를 빈 상태로 변경
							if (rsel_reg == 1'b0) begin
								full0_next = 1'b0;
							end else begin
								full1_next = 1'b0;
							end
						end
					end
				end
			endcase
		end
	end

	// ========================================
	// 3. 메모리 읽기 / 쓰기
	// 메모리 배열과 읽기 데이터는 reset하지 않음
	// ========================================
	always @(posedge clk) begin
		if (w_mem0_wen) begin
			buffer0[adc_cnt_reg[6:1]] <= {sample_reg, i_adc_data};
		end else if (w_mem0_ren) begin
			rdata0 <= buffer0[pair_cnt_reg];
		end
	end

	always @(posedge clk) begin
		if (w_mem1_wen) begin
			buffer1[adc_cnt_reg[6:1]] <= {sample_reg, i_adc_data};
		end else if (w_mem1_ren) begin
			rdata1 <= buffer1[pair_cnt_reg];
		end
	end
endmodule