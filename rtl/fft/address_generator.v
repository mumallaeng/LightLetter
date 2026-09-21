`timescale 1ns / 1ps

module address_generator (
    input  wire [2:0] i_bf_stage,       // 0 ~ 6
    input  wire [5:0] i_bf_count,    // 0 ~ 63
    input  wire [2:0] i_sys_state,

    output reg  [6:0] o_addr_a,      // Data Memory Address A
    output reg  [6:0] o_addr_b,      // Data Memory Address B
    output reg  [5:0] o_tw_addr      // Twiddle ROM Address
);

    parameter ST_IDLE = 3'd0, ST_LOAD = 3'd1;
    parameter ST_READ = 3'd2, ST_CALC = 3'd3, ST_WRITE = 3'd4;
    parameter ST_OUT = 3'd5, ST_DONE = 3'd6;

    always @(*) begin
        o_addr_a  = 7'd0;
        o_addr_b  = 7'd0;
        o_tw_addr = 6'd0;
        case (i_sys_state)
            ST_LOAD: begin
                // i_wdata_0 = x[2*p]의 저장 주소
	            o_addr_a = {
	            	1'b0,
	            	i_bf_count[0], i_bf_count[1], i_bf_count[2],
	            	i_bf_count[3], i_bf_count[4], i_bf_count[5]
	            };

	            // i_wdata_1 = x[2*p + 1]의 저장 주소
	            o_addr_b = {
	            	1'b1,
	            	i_bf_count[0], i_bf_count[1], i_bf_count[2],
	            	i_bf_count[3], i_bf_count[4], i_bf_count[5]
	            };
	            // LOAD에서는 Twiddle을 사용하지 않음
	            o_tw_addr = 6'd0;
            end
            ST_READ, ST_CALC, ST_WRITE: begin
                case (i_bf_stage)
                    // -------------------------------------------------
                    // Stage 0
                    // gap = 1
                    // Twiddle : W^0
                    // -------------------------------------------------
                    3'd0: begin
                        o_addr_a  = {i_bf_count, 1'b0};
                        o_addr_b  = {i_bf_count, 1'b1};
                        o_tw_addr = 6'd0;
                    end

                    // -------------------------------------------------
                    // Stage 1
                    // gap = 2
                    // Twiddle : W^0, W^32
                    // -------------------------------------------------
                    3'd1: begin
                        o_addr_a  = {i_bf_count[5:1], 1'b0, i_bf_count[0]};
                        o_addr_b  = {i_bf_count[5:1], 1'b1, i_bf_count[0]};
                        o_tw_addr = {i_bf_count[0], 5'b00000};
                    end

                    // -------------------------------------------------
                    // Stage 2
                    // gap = 4
                    // Twiddle : W^0, W^16, W^32, W^48
                    // -------------------------------------------------
                    3'd2: begin
                        o_addr_a  = {i_bf_count[5:2], 1'b0, i_bf_count[1:0]};
                        o_addr_b  = {i_bf_count[5:2], 1'b1, i_bf_count[1:0]};
                        o_tw_addr = {i_bf_count[1:0], 4'b0000};
                    end

                    // -------------------------------------------------
                    // Stage 3
                    // gap = 8
                    // -------------------------------------------------
                    3'd3: begin
                        o_addr_a  = {i_bf_count[5:3], 1'b0, i_bf_count[2:0]};
                        o_addr_b  = {i_bf_count[5:3], 1'b1, i_bf_count[2:0]};
                        o_tw_addr = {i_bf_count[2:0], 3'b000};
                    end

                    // -------------------------------------------------
                    // Stage 4
                    // gap = 16
                    // -------------------------------------------------
                    3'd4: begin
                        o_addr_a  = {i_bf_count[5:4], 1'b0, i_bf_count[3:0]};
                        o_addr_b  = {i_bf_count[5:4], 1'b1, i_bf_count[3:0]};
                        o_tw_addr = {i_bf_count[3:0], 2'b00};
                    end

                    // -------------------------------------------------
                    // Stage 5
                    // gap = 32
                    // -------------------------------------------------
                    3'd5: begin
                        o_addr_a  = {i_bf_count[5], 1'b0, i_bf_count[4:0]};
                        o_addr_b  = {i_bf_count[5], 1'b1, i_bf_count[4:0]};
                        o_tw_addr = {i_bf_count[4:0], 1'b0};
                    end

                    // -------------------------------------------------
                    // Stage 6
                    // gap = 64
                    // Twiddle : W^0 ~ W^63
                    // -------------------------------------------------
                    3'd6: begin
                        o_addr_a  = {1'b0, i_bf_count};
                        o_addr_b  = {1'b1, i_bf_count};
                        o_tw_addr = i_bf_count;
                    end
                endcase 
            end
            ST_OUT: begin
                o_addr_a  = {i_bf_count, 1'b0};
	            o_addr_b  = {i_bf_count, 1'b1};
	            o_tw_addr = 6'd0;
            end
        endcase
    end
endmodule