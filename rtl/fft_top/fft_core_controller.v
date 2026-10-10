`timescale 1ns / 1ps

module fft_core_controller (
    input  wire       clk,
    input  wire       rst,
    // from buffer
    input  wire       i_frame_valid,      // buffer 128개 준비 완료
    output reg        o_frame_ready,      // fft 연산 ready
    input  wire       i_buf_data_valid,   // buffer 2개씩 받는 데이터 유효
    // to write path
    output reg        o_buf_data_load_en, // ST_LOAD enable
    output reg        o_mem_pingpong_sel, // stage 별 저장 mem 위치 지정 / 0: mem0, 1: mem1
    output reg        o_buf_wb0_sel,      // mem0 저장 데이터 select / 0: load data, 1: write back data
    output reg        o_bf_out_load_en,   // butterfly 연산 결과 저장 enable
    // from write path
    input  wire       i_wdata_valid_0,    // mem0 write data 유효
    input  wire       i_wdata_valid_1,    // mem1 write data 유효
    // to address generator
    output wire [5:0] o_bf_count,         // butterfly 연산 횟수
    output wire [2:0] o_bf_stage,         // butterfly 연산 stage
    output wire [2:0] o_sys_state,        // controller 상태
    // to read path
    output reg        o_core_out_en,      // fft core 출력 enable
    output reg        o_rdata_load_en,    // memory data read enable
    output wire       o_core_out_sel,     // even odd 순차 출력
    output reg        o_bf_mem_sel,       // butterfly 연산 memory 선택
    // from butterfly
    input  wire       i_bf_out_valid      // butterfly 연산 결과 a,b 유효
);

    parameter ST_IDLE = 3'd0, ST_LOAD = 3'd1;
    parameter ST_READ = 3'd2, ST_CALC = 3'd3; // Pipeline 변경: ST_WRITE 제거 (기존 State 인코딩 유지)
    parameter ST_OUT  = 3'd5, ST_DONE = 3'd6;

    reg [2:0] c_state, n_state;
    reg [5:0] bf_cnt_reg, bf_cnt_next;
    reg [2:0] bf_stg_reg, bf_stg_next;
    reg       c_step, n_step;
    reg       out_sel_reg, out_sel_next;
    reg       out_last_reg, out_last_next;

    // Pipeline 추가: 현재 Stage의 Butterfly 결과 저장 횟수
    reg [5:0] written_reg, written_next;
    // Pipeline 추가: Memory Read와 Read Path 사이 1클럭 유효 신호 정렬
    reg       read_issue_d;
    // Pipeline 추가: Stage별 결과 저장 메모리의 valid 선택
    wire      stage_write_valid;
    assign stage_write_valid = bf_stg_reg[0] ? i_wdata_valid_0 : i_wdata_valid_1;

    assign o_sys_state = c_state;
    assign o_bf_count = bf_cnt_reg;
    assign o_bf_stage = bf_stg_reg;
    assign o_core_out_sel = out_sel_reg;

    always @(posedge clk, posedge rst) begin
        if (rst) begin
            c_state      <= ST_IDLE;
            bf_cnt_reg   <= 0;
            bf_stg_reg   <= 0;
            c_step       <= 0;
            out_sel_reg  <= 0;
            out_last_reg <= 1'b0;
            // Pipeline 추가: Stage 저장 카운터 및 READ 지연 초기화
            written_reg  <= 0;
            read_issue_d <= 1'b0;
        end else begin
            c_state      <= n_state;
            bf_cnt_reg   <= bf_cnt_next;
            bf_stg_reg   <= bf_stg_next;
            c_step       <= n_step;
            out_sel_reg  <= out_sel_next;
            out_last_reg <= out_last_next;
            // Pipeline 추가: 저장 완료 수와 직전 Read 요청 기록
            written_reg  <= written_next;
            read_issue_d <= (c_state == ST_READ);
        end
    end

    always @(*) begin
        n_state     = c_state;
        bf_cnt_next = bf_cnt_reg;
        bf_stg_next = bf_stg_reg;
        n_step      = c_step;
        out_sel_next = out_sel_reg;
        out_last_next = out_last_reg;
        // Pipeline 추가: 저장 카운터 기본 유지
        written_next = written_reg;

        o_frame_ready      = 1'b0;
        
		o_buf_data_load_en = 1'b0;
		o_bf_out_load_en   = 1'b0;
		o_core_out_en      = 1'b0;
		// Pipeline 변경: READ 요청 후 1클럭 뒤 Read Path에 데이터 전달
		o_rdata_load_en    = read_issue_d;

		o_buf_wb0_sel      = 1'b0;
		o_bf_mem_sel       = 1'b0;
		o_mem_pingpong_sel = 1'b0;

        // Pipeline 변경: READ/CALC에서 이전 Butterfly 결과의 메모리 저장 완료 카운트
        if (((c_state == ST_READ) || (c_state == ST_CALC)) && stage_write_valid) begin
            written_next = written_reg + 6'd1;
        end

        case (c_state)
            ST_IDLE: begin
                o_frame_ready = 1;
                n_step = 0;
                out_sel_next = 0;
                if (i_frame_valid && o_frame_ready) begin
                    bf_cnt_next = 0;
                    bf_stg_next = 0;
                    // Pipeline 추가: 새 FFT 프레임에서 결과 저장 카운터 초기화
                    written_next = 0;
                    n_state = ST_LOAD;
                end
            end 
            ST_LOAD: begin
                case (c_step)
                    0: begin // load buffer data
                        if (i_buf_data_valid) begin
                            o_buf_data_load_en = 1;
                            n_step = 1;
                        end
                    end 
                    1: begin // save buffer data in memory
                        o_buf_data_load_en = 0;
                        if (i_wdata_valid_0) begin
                            if (bf_cnt_reg < 63) begin
                                bf_cnt_next = bf_cnt_next + 1;
                                n_step = 0;
                            end else begin
                                bf_cnt_next = 0;
                                n_step = 0;
                                // Pipeline 추가: 새로운 FFT Stage 시작 전 저장 카운터 초기화
                                written_next = 0;
                                n_state = ST_READ;
                            end
                        end
                    end
                endcase
            end
            ST_READ: begin
                o_buf_wb0_sel      = 1'b1;
                o_bf_mem_sel       = o_bf_stage[0];
                o_mem_pingpong_sel = ~o_bf_stage[0];
                // Pipeline 추가: READ 중에도 이전 Butterfly 연산 결과 저장
                o_bf_out_load_en = i_bf_out_valid;
                case (c_step)
                    0: begin // read data of memory
                        o_rdata_load_en  = 0;
                        n_step = 1;
                        // Pipeline 변경: 첫 Butterfly 주소를 발행하고 다음 주소 준비
                        bf_cnt_next = bf_cnt_reg + 6'd1;
                    end 
                    1: begin // give rdata to butterfly
                        // Pipeline 변경: 직전 Memory Read 결과를 전달하며 다음 주소를 연속 발행
                        o_rdata_load_en = read_issue_d;
                        if (bf_cnt_reg < 63) begin
                            bf_cnt_next = bf_cnt_reg + 6'd1;
                        end else begin
                            n_step = 0;
                            n_state = ST_CALC;
                        end
                    end
                endcase
            end
            ST_CALC: begin
                o_buf_wb0_sel      = 1'b1;
                o_bf_mem_sel       = o_bf_stage[0];
                o_mem_pingpong_sel = ~o_bf_stage[0];
                
                // Pipeline 변경: 마지막 Read 결과 전달 및 남은 Butterfly 결과 저장
                o_rdata_load_en = read_issue_d;
                o_bf_out_load_en = i_bf_out_valid; // load butterfly data
                // Pipeline 변경: 64번째 Butterfly 결과 저장 완료 시 Stage 전환
                if (stage_write_valid && written_reg == 6'd63) begin
                    n_step = 0;
                    if (bf_stg_reg < 6) begin
                        bf_cnt_next = 0;
                        bf_stg_next = bf_stg_next + 1;
                        written_next = 0;
                        n_state = ST_READ;
                    end else begin
                        bf_cnt_next = 0;
                        written_next = 0;
                        n_state = ST_OUT;
                    end
                end
            end
            ST_OUT: begin
                o_bf_mem_sel = 1;
                case (c_step)
                    0: begin // 초기 memory read
                        o_core_out_en  = 0;
                        out_sel_next   = 0;
                        out_last_next  = 0;
                        n_step         = 1;
                    end 
                    1: begin // 매 클럭 memory data 연속 출력
                        o_core_out_en = 1;
                        if (out_sel_reg == 0) begin // even 출력 (0, 2, 4, ...)
                            out_sel_next = 1;
                            if (bf_cnt_reg < 63) begin
                                bf_cnt_next = bf_cnt_next + 1;
                            end else begin
					            out_last_next = 1'b1;
                            end
                        end else begin // odd 출력 (1, 3, 5, ...)
                            out_sel_next = 0;
                            if (out_last_reg) begin
                                bf_cnt_next = 0;
                                out_last_next = 1'b0;
                                n_step = 0;
                                n_state = ST_DONE;
                            end
                        end
                    end
                endcase
            end
            ST_DONE: begin
                n_step = 0;
                n_state = ST_IDLE;
            end 
        endcase
    end
endmodule