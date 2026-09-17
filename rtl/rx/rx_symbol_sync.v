module rx_symbol_sync #(
    parameter SYNC_MIN_BLOCKS = 6
) (
    input wire clk,
    input wire rst_n,

    // rx_symbol_detector 출력
    input wire [1:0] block_code,
    input wire       block_code_valid,

    // rx_frame_decoder가 프레임 처리를 끝냈다는 피드백
    input wire frame_finish,

    // rx_frame_decoder로 전달
    output wire [1:0] symbol_code,
    output wire       symbol_valid,
    output wire       frame_start,
    output wire       frame_abort

    // 상태 확인용
);
    localparam [1:0] CODE_BIT0 = 2'b00;
    localparam [1:0] CODE_BIT1 = 2'b01;
    localparam [1:0] CODE_SYNC = 2'b10;
    localparam [1:0] CODE_INVALID = 2'b11;

    localparam [2:0] ST_SEARCH_SYNC = 3'd0;
    localparam [2:0] ST_WAIT_FIRST_BIT = 3'd1;
    localparam [2:0] ST_FRAME_START = 3'd2;
    localparam [2:0] ST_EMIT_FIRST = 3'd3;
    localparam [2:0] ST_SKIP_BLOCK = 3'd4;
    localparam [2:0] ST_USE_BLOCK = 3'd5;

    reg [2:0] c_state, n_state;
    reg [2:0] c_sync_count, n_sync_count;
    reg [1:0] c_symbol_code, n_symbol_code;
    reg c_frame_start, n_frame_start;
    reg c_symbol_valid, n_symbol_valid;
    reg c_frame_abort, n_frame_abort;

    assign symbol_code  = c_symbol_code;
    assign symbol_valid = c_symbol_valid;
    assign frame_start  = c_frame_start;
    assign frame_abort  = c_frame_abort;
    always @(posedge clk, negedge rst_n) begin
        if (!rst_n) begin
            c_state <= 0;
            c_sync_count <= 0;
            c_symbol_code <= 0;
            c_frame_start <= 0;
            c_symbol_valid <= 0;
            c_frame_abort <= 0;
        end else begin
            c_state <= n_state;
            c_sync_count <= n_sync_count;
            c_symbol_code <= n_symbol_code;
            c_frame_start <= n_frame_start;
            c_symbol_valid <= n_symbol_valid;
            c_frame_abort <= n_frame_abort;
        end
    end

    always @(*) begin
        n_state = c_state;
        n_sync_count = c_sync_count;
        n_symbol_code = c_symbol_code;
        n_frame_start = 0;
        n_symbol_valid = 0;
        n_frame_abort = 0;
        case (c_state)
            ST_SEARCH_SYNC: begin
                if (block_code_valid) begin
                    if (block_code == CODE_SYNC) begin
                        n_sync_count = c_sync_count + 1;
                        if (c_sync_count == SYNC_MIN_BLOCKS - 1) begin
                            n_state = ST_WAIT_FIRST_BIT;
                            n_sync_count = 0;
                        end
                    end else begin
                        n_sync_count = 0;
                    end
                end
            end
            ST_WAIT_FIRST_BIT: begin
                if (block_code_valid) begin
                    if((block_code==CODE_BIT0)||(block_code==CODE_BIT1)) begin
                        n_state = ST_FRAME_START;
                        n_symbol_code = block_code;
                    end
                end
            end
            ST_FRAME_START: begin
                n_frame_start = 1;
                n_state = ST_EMIT_FIRST;
            end
            ST_EMIT_FIRST: begin
                n_symbol_valid = 1;
                n_state = ST_SKIP_BLOCK;
            end
            ST_SKIP_BLOCK: begin
                if (frame_finish) begin
                    n_state = ST_SEARCH_SYNC;
                    n_sync_count = 0;
                end else if (block_code_valid) begin
                    // FFT block 하나 버림
                    n_state = ST_USE_BLOCK;
                end
            end
            ST_USE_BLOCK: begin
                if (frame_finish) begin
                    n_state = ST_SEARCH_SYNC;
                    n_sync_count = 0;
                end else if (block_code_valid) begin
                    if ((block_code == CODE_BIT0) ||
            (block_code == CODE_BIT1)) begin

                        n_symbol_code = block_code;
                        n_symbol_valid = 1'b1;

                        // 다음 FFT block은 같은 symbol의 두 번째 block이므로 버림
                        n_state = ST_SKIP_BLOCK;
                    end else begin
                        // 예상 위치에서 BIT0/BIT1이 아니면
                        // frame synchronization이 깨졌다고 판단
                        // decoder에 현재 프레임 폐기를 알리는 1클럭 pulse
                        n_frame_abort = 1'b1;
                        n_state = ST_SEARCH_SYNC;
                        n_sync_count = 0;
                    end
                end
            end
        endcase
    end
endmodule
