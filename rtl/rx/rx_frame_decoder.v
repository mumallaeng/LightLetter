module rx_frame_decoder (
    input wire clk,
    input wire rst_n,

    input wire       frame_start,
    input wire       frame_abort,
    input wire [1:0] symbol_code,
    input wire       symbol_valid,

    output wire [7:0] frame_id,
    output wire [7:0] data,
    output wire [7:0] received_crc,

    output reg decode_valid,
    output reg frame_finish,
    output reg packet_error
);
    localparam [3:0]
    ST_IDLE      = 4'd0,
    ST_READ_SFD  = 4'd1,
    ST_CHECK_SFD = 4'd2,
    ST_READ_ID   = 4'd3,
    ST_READ_DATA = 4'd4,
    ST_READ_CRC  = 4'd5,
    ST_DONE      = 4'd6,
    ST_ERROR     = 4'd7;

    reg [3:0] c_state, n_state;
    reg [7:0] c_shift_reg, n_shift_reg;
    reg [2:0] c_bit_count, n_bit_count;
    reg [7:0] c_frame_id, n_frame_id;
    reg [7:0] c_data, n_data;
    reg [7:0] c_received_crc, n_received_crc;
    wire sym_bit;

    assign sym_bit      = symbol_code[0];
    assign frame_id     = c_frame_id;
    assign data         = c_data;
    assign received_crc = c_received_crc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            c_state        <= ST_IDLE;
            c_shift_reg    <= 8'h00;
            c_bit_count    <= 3'd0;
            c_frame_id     <= 8'h00;
            c_data         <= 8'h00;
            c_received_crc <= 8'h00;
        end else begin
            c_state        <= n_state;
            c_shift_reg    <= n_shift_reg;
            c_bit_count    <= n_bit_count;
            c_frame_id     <= n_frame_id;
            c_data         <= n_data;
            c_received_crc <= n_received_crc;
        end
    end
    always @(*) begin
        n_state        = c_state;
        n_shift_reg    = c_shift_reg;
        n_bit_count    = c_bit_count;
        n_frame_id     = c_frame_id;
        n_data         = c_data;
        n_received_crc = c_received_crc;
        decode_valid   = 1'b0;
        frame_finish   = 1'b0;
        packet_error   = 1'b0;

        if (frame_abort) begin
            // 오류/종료 신호를 여기서 출력하므로 ERROR를 다시 거치지 않는다.
            n_state      = ST_IDLE;
            n_shift_reg  = 8'h00;
            n_bit_count  = 3'd0;
            frame_finish = 1'b1;
            packet_error = 1'b1;
        end else begin
            case (c_state)
                ST_IDLE: begin
                    if (frame_start) begin
                        n_state = ST_READ_SFD;
                        n_shift_reg = 0;
                        n_bit_count = 0;
                    end
                end
                ST_READ_SFD: begin
                    if (symbol_valid) begin
                        n_shift_reg = {c_shift_reg[6:0], sym_bit};
                        n_bit_count = c_bit_count + 1;
                        if (c_bit_count == 7) begin
                            n_state = ST_CHECK_SFD;
                            n_bit_count = 0;
                        end
                    end
                end
                ST_CHECK_SFD: begin
                    if (c_shift_reg == 8'hD5) n_state = ST_READ_ID;
                    else n_state = ST_ERROR;
                end
                ST_READ_ID: begin
                    if (symbol_valid) begin
                        n_shift_reg = {c_shift_reg[6:0], sym_bit};
                        n_bit_count = c_bit_count + 1;
                        if (c_bit_count == 7) begin
                            n_state = ST_READ_DATA;
                            n_bit_count = 0;
                            n_frame_id = n_shift_reg;
                        end
                    end
                end
                ST_READ_DATA: begin
                    if (symbol_valid) begin
                        n_shift_reg = {c_shift_reg[6:0], sym_bit};
                        n_bit_count = c_bit_count + 1;
                        if (c_bit_count == 7) begin
                            n_state = ST_READ_CRC;
                            n_bit_count = 0;
                            n_data = n_shift_reg;
                        end
                    end
                end
                ST_READ_CRC: begin
                    if (symbol_valid) begin
                        n_shift_reg = {c_shift_reg[6:0], sym_bit};
                        n_bit_count = c_bit_count + 1;
                        if (c_bit_count == 7) begin
                            n_state = ST_DONE;
                            n_bit_count = 0;
                            n_received_crc = n_shift_reg;
                        end
                    end
                end
                ST_DONE: begin
                    decode_valid = 1'b1;
                    frame_finish = 1'b1;
                    n_state      = ST_IDLE;
                end

                ST_ERROR: begin
                    frame_finish = 1'b1;
                    packet_error = 1'b1;
                    n_shift_reg  = 8'h00;
                    n_bit_count  = 3'd0;
                    n_state      = ST_IDLE;
                end

                default: begin
                    n_state = ST_ERROR;
                end
            endcase
        end

    end
endmodule
