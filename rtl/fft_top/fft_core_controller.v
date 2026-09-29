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
    parameter ST_READ = 3'd2, ST_CALC = 3'd3, ST_WRITE = 3'd4;
    parameter ST_OUT  = 3'd5, ST_DONE = 3'd6;

    reg [2:0] c_state, n_state;
    reg [5:0] bf_cnt_reg, bf_cnt_next;
    reg [2:0] bf_stg_reg, bf_stg_next;
    reg       c_step, n_step;
    reg       out_sel_reg, out_sel_next;
    reg       out_last_reg, out_last_next;

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
        end else begin
            c_state      <= n_state;
            bf_cnt_reg   <= bf_cnt_next;
            bf_stg_reg   <= bf_stg_next;
            c_step       <= n_step;
            out_sel_reg  <= out_sel_next;
            out_last_reg <= out_last_next;
        end
    end

    always @(*) begin
        n_state     = c_state;
        bf_cnt_next = bf_cnt_reg;
        bf_stg_next = bf_stg_reg;
        n_step      = c_step;
        out_sel_next = out_sel_reg;
        out_last_next = out_last_reg;

        o_frame_ready      = 1'b0;
        
		o_buf_data_load_en = 1'b0;
		o_bf_out_load_en   = 1'b0;
		o_core_out_en      = 1'b0;
		o_rdata_load_en    = 1'b0;

		o_buf_wb0_sel      = 1'b0;
		o_bf_mem_sel       = 1'b0;
		o_mem_pingpong_sel = 1'b0;

        case (c_state)
            ST_IDLE: begin
                o_frame_ready = 1;
                n_step = 0;
                out_sel_next = 0;
                if (i_frame_valid && o_frame_ready) begin
                    bf_cnt_next = 0;
                    bf_stg_next = 0;
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
                case (c_step)
                    0: begin // read data of memory
                        o_rdata_load_en  = 0;
                        n_step = 1;
                    end 
                    1: begin // give rdata to butterfly
                        o_rdata_load_en  = 1;
                        n_step = 0;
                        n_state = ST_CALC;
                    end
                endcase
            end
            ST_CALC: begin
                o_buf_wb0_sel      = 1'b1;
                o_bf_mem_sel       = o_bf_stage[0];
                o_mem_pingpong_sel = ~o_bf_stage[0];
                if (i_bf_out_valid) begin
                    n_step  = 0;
                    n_state = ST_WRITE;
                end
            end
            ST_WRITE: begin
                o_buf_wb0_sel      = 1'b1;
                o_bf_mem_sel       = o_bf_stage[0];
                o_mem_pingpong_sel = ~o_bf_stage[0];
                case (c_step)
                    0: begin // load butterfly data
                        o_bf_out_load_en = 1;
                        n_step = 1;
                    end 
                    1: begin // save butterfly data in memory
                        o_bf_out_load_en = 0;
                        if (((!o_mem_pingpong_sel) && i_wdata_valid_0) 
                            || (o_mem_pingpong_sel && i_wdata_valid_1)) begin
                            n_step = 0;
                            if (bf_cnt_reg < 63) begin
                                bf_cnt_next = bf_cnt_next + 1;
                                n_state = ST_READ;
                            end else begin
                                if (bf_stg_reg < 6) begin
                                    bf_cnt_next = 0;
                                    bf_stg_next = bf_stg_next + 1;
                                    n_state = ST_READ;
                                end else begin
                                    bf_cnt_next = 0;
                                    n_state = ST_OUT;
                                end
                            end
                        end
                    end
                endcase
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