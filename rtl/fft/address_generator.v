`timescale 1ns / 1ps

module address_generator (
    input  wire [2:0] i_bf_stage,  // 0 ~ 6
    input  wire [5:0] i_bf_count,  // 0 ~ 63
    input  wire [2:0] i_sys_state,

    output reg  [6:0] o_addr_a,    // Data Memory Address A
    output reg  [6:0] o_addr_b,    // Data Memory Address B
    output reg  [5:0] o_tw_addr    // Twiddle ROM Address
);

    localparam ST_IDLE  = 3'd0;
    localparam ST_LOAD  = 3'd1;
    localparam ST_READ  = 3'd2;
    localparam ST_CALC  = 3'd3;
    localparam ST_WRITE = 3'd4;
    localparam ST_OUT   = 3'd5;
    localparam ST_DONE  = 3'd6;

    reg [6:0] gap;
    reg [6:0] low_mask;
    reg [6:0] count_ext;
    reg [6:0] base_addr;
    reg [6:0] twiddle_ext;

    function [6:0] bit_reverse7;
        input [6:0] value;
        integer bit_index;
        begin
            for (bit_index = 0; bit_index < 7; bit_index = bit_index + 1) begin
                bit_reverse7[bit_index] = value[6-bit_index];
            end
        end
    endfunction

    always @(*) begin
        o_addr_a  = 7'd0;
        o_addr_b  = 7'd0;
        o_tw_addr = 6'd0;

        gap        = 7'd0;
        low_mask   = 7'd0;
        count_ext  = {1'b0, i_bf_count}; // butterfly count extension (6->7)
        base_addr  = 7'd0;
        twiddle_ext = 7'd0;

        case (i_sys_state)
            ST_LOAD: begin
                o_addr_a = bit_reverse7({i_bf_count, 1'b0});
                o_addr_b = bit_reverse7({i_bf_count, 1'b1});
            end

            ST_READ, ST_CALC, ST_WRITE: begin
                if (i_bf_stage <= 3'd6) begin
                    gap       = 7'd1 << i_bf_stage; // 2^stage
                    low_mask  = gap - 7'd1;

                    // count_ext & ~low_mask -> 그룹 번호
                    // count_ext & low_mask -> j (한 그룹 안에서 몇번째 butterfly 계산인지)
                    // count의 stage 위치에 0을 삽입해 A 주소를 만든다.
                    // 같은 위치를 1로 바꾸면 대응되는 B 주소가 된다.
                    base_addr = ((count_ext & ~low_mask) << 1) | (count_ext & low_mask);

                    o_addr_a = base_addr;
                    o_addr_b = base_addr | gap;

                    // W_128^(j * 64/gap), j = count mod gap
                    twiddle_ext = (count_ext & low_mask) << (3'd6 - i_bf_stage);
                    o_tw_addr = twiddle_ext[5:0];
                end
            end

            ST_OUT: begin
                // 두 개의 연속된 natural-order 결과를 동시에 읽는다.
                o_addr_a  = {i_bf_count, 1'b0};
                o_addr_b  = {i_bf_count, 1'b1};
            end

            default: begin
                // IDLE/DONE에서는 모든 출력을 0으로 유지한다.
            end
        endcase
    end
endmodule
