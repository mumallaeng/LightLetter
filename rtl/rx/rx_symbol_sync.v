`timescale 1ns / 1ps

// Each BFSK symbol spans two consecutive 128-point FFT blocks.
// After the SYNC preamble, this module waits for the first data-frequency
// block, evaluates both possible block phases with the D5 SFD, verifies that
// the selected eight bits are exactly D5, and then forwards the frame bits.
module rx_symbol_sync #(
    parameter MAG_W = 40,
    parameter SYNC_MIN_BLOCKS = 8,
    parameter integer SCORE_MARGIN = 0
) (
    input  wire                 clk,
    input  wire                 rst_n,
    input  wire [1:0]           block_code,
    input  wire                 block_code_valid,
    input  wire [MAG_W-1:0]     bin8_power,
    input  wire [MAG_W-1:0]     bin16_power,
    input  wire [MAG_W-1:0]     bin20_power,
    input  wire                 fft_block_done,
    input  wire                 frame_finish,
    output wire [1:0]           symbol_code,
    output wire                 symbol_valid,
    output wire                 frame_start,
    output wire                 frame_abort
);
    localparam [1:0] CODE_BIT0    = 2'b00;
    localparam [1:0] CODE_BIT1    = 2'b01;
    localparam [1:0] CODE_SYNC    = 2'b10;
    localparam [1:0] CODE_INVALID = 2'b11;
    localparam [7:0] SFD_PATTERN  = 8'hD5;

    localparam [3:0] ST_SEARCH        = 4'd0;
    localparam [3:0] ST_WAIT_NON_SYNC = 4'd1;
    localparam [3:0] ST_COLLECT       = 4'd2;
    localparam [3:0] ST_SCORE         = 4'd3;
    localparam [3:0] ST_SELECT        = 4'd4;
    localparam [3:0] ST_CHECK_CAPTURE = 4'd5;
    localparam [3:0] ST_CHECK_SFD     = 4'd6;
    localparam [3:0] ST_REPLAY        = 4'd7;
    localparam [3:0] ST_FIRST         = 4'd8;
    localparam [3:0] ST_SECOND        = 4'd9;

    reg [3:0] c_state, n_state;
    reg [3:0] c_sync_count, n_sync_count;
    reg [4:0] c_collect_count, n_collect_count;
    reg [3:0] c_score_count, n_score_count;
    reg [3:0] c_check_count, n_check_count;
    reg [3:0] c_replay_count, n_replay_count;
    reg       c_phase_b, n_phase_b;
    reg [7:0] c_sfd_shift, n_sfd_shift;

    // Candidate A: [0,1], [2,3], ... [14,15]
    // Candidate B: [1,2], [3,4], ... [15,16]
    reg [MAG_W-1:0] p8_mem  [0:16];
    reg [MAG_W-1:0] p16_mem [0:16];
    reg [MAG_W-1:0] p20_mem [0:16];

    reg [MAG_W-1:0] c_first8, n_first8;
    reg [MAG_W-1:0] c_first16, n_first16;
    reg [MAG_W-1:0] c_first20, n_first20;
    reg [1:0] c_symbol_code, n_symbol_code;
    reg c_symbol_valid, n_symbol_valid;
    reg c_frame_start, n_frame_start;
    reg c_frame_abort, n_frame_abort;

    reg [MAG_W+4:0] c_score_a, n_score_a;
    reg [MAG_W+4:0] c_score_b, n_score_b;
    reg [MAG_W:0] c_check_p8, n_check_p8;
    reg [MAG_W:0] c_check_p16, n_check_p16;
    reg [MAG_W:0] c_check_p20, n_check_p20;
    reg [MAG_W:0] score_term_a, score_term_b;
    reg [MAG_W:0] check_p8, check_p16, check_p20;
    reg [MAG_W+4:0] check8_x10, check16_x10, check20_x10;
    reg [MAG_W+4:0] check8_x12, check16_x12, check20_x12;
    reg [1:0] check_code;

    // Payload power sums.  The leading zero preserves the carry bit.
    wire [MAG_W:0] payload_p8 =
        {1'b0, c_first8} + {1'b0, bin8_power};
    wire [MAG_W:0] payload_p16 =
        {1'b0, c_first16} + {1'b0, bin16_power};
    wire [MAG_W:0] payload_p20 =
        {1'b0, c_first20} + {1'b0, bin20_power};

    wire [MAG_W+4:0] payload8_ext  = {4'b0, payload_p8};
    wire [MAG_W+4:0] payload16_ext = {4'b0, payload_p16};
    wire [MAG_W+4:0] payload20_ext = {4'b0, payload_p20};
    wire [MAG_W+4:0] payload8_x10 =
        (payload8_ext << 3) + (payload8_ext << 1);
    wire [MAG_W+4:0] payload16_x10 =
        (payload16_ext << 3) + (payload16_ext << 1);
    wire [MAG_W+4:0] payload20_x10 =
        (payload20_ext << 3) + (payload20_ext << 1);
    wire [MAG_W+4:0] payload8_x12 =
        (payload8_ext << 3) + (payload8_ext << 2);
    wire [MAG_W+4:0] payload16_x12 =
        (payload16_ext << 3) + (payload16_ext << 2);
    wire [MAG_W+4:0] payload20_x12 =
        (payload20_ext << 3) + (payload20_ext << 2);

    assign symbol_code  = c_symbol_code;
    assign symbol_valid = c_symbol_valid;
    assign frame_start  = c_frame_start;
    assign frame_abort  = c_frame_abort;

    // Select one expected-bin term for each candidate.  ST_SCORE accumulates
    // one D5 bit per clock, avoiding an eight-adder combinational chain.
    always @(*) begin
        score_term_a = 0;
        score_term_b = 0;
        if (SFD_PATTERN[7-c_score_count]) begin
            score_term_a = {1'b0, p16_mem[2*c_score_count]} +
                           {1'b0, p16_mem[2*c_score_count+1]};
            score_term_b = {1'b0, p16_mem[2*c_score_count+1]} +
                           {1'b0, p16_mem[2*c_score_count+2]};
        end else begin
            score_term_a = {1'b0, p8_mem[2*c_score_count]} +
                           {1'b0, p8_mem[2*c_score_count+1]};
            score_term_b = {1'b0, p8_mem[2*c_score_count+1]} +
                           {1'b0, p8_mem[2*c_score_count+2]};
        end

        // Classify one selected SFD pair with ordinary comparisons.
        check_p8  = 0;
        check_p16 = 0;
        check_p20 = 0;
        if (c_phase_b) begin
            check_p8 = {1'b0, p8_mem[2*c_check_count+1]} +
                       {1'b0, p8_mem[2*c_check_count+2]};
            check_p16 = {1'b0, p16_mem[2*c_check_count+1]} +
                        {1'b0, p16_mem[2*c_check_count+2]};
            check_p20 = {1'b0, p20_mem[2*c_check_count+1]} +
                        {1'b0, p20_mem[2*c_check_count+2]};
        end else begin
            check_p8 = {1'b0, p8_mem[2*c_check_count]} +
                       {1'b0, p8_mem[2*c_check_count+1]};
            check_p16 = {1'b0, p16_mem[2*c_check_count]} +
                        {1'b0, p16_mem[2*c_check_count+1]};
            check_p20 = {1'b0, p20_mem[2*c_check_count]} +
                        {1'b0, p20_mem[2*c_check_count+1]};
        end

        // The selected pair sums are registered in ST_CHECK_CAPTURE.  The
        // following clock performs only the threshold comparison.
        check8_x10  = ({4'b0, c_check_p8}  << 3) +
                      ({4'b0, c_check_p8}  << 1);
        check16_x10 = ({4'b0, c_check_p16} << 3) +
                      ({4'b0, c_check_p16} << 1);
        check20_x10 = ({4'b0, c_check_p20} << 3) +
                      ({4'b0, c_check_p20} << 1);
        check8_x12  = ({4'b0, c_check_p8}  << 3) +
                      ({4'b0, c_check_p8}  << 2);
        check16_x12 = ({4'b0, c_check_p16} << 3) +
                      ({4'b0, c_check_p16} << 2);
        check20_x12 = ({4'b0, c_check_p20} << 3) +
                      ({4'b0, c_check_p20} << 2);

        if ((check8_x10 > check16_x12) &&
            (check8_x10 > check20_x12))
            check_code = CODE_BIT0;
        else if ((check16_x10 > check8_x12) &&
                 (check16_x10 > check20_x12))
            check_code = CODE_BIT1;
        else if ((check20_x10 > check8_x12) &&
                 (check20_x10 > check16_x12))
            check_code = CODE_SYNC;
        else
            check_code = CODE_INVALID;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            c_state         <= ST_SEARCH;
            c_sync_count    <= 0;
            c_collect_count <= 0;
            c_score_count   <= 0;
            c_score_a       <= 0;
            c_score_b       <= 0;
            c_check_p8      <= 0;
            c_check_p16     <= 0;
            c_check_p20     <= 0;
            c_check_count   <= 0;
            c_replay_count  <= 0;
            c_phase_b       <= 0;
            c_sfd_shift     <= 0;
            c_first8        <= 0;
            c_first16       <= 0;
            c_first20       <= 0;
            c_symbol_code   <= CODE_INVALID;
            c_symbol_valid  <= 0;
            c_frame_start   <= 0;
            c_frame_abort   <= 0;
        end else begin
            c_state         <= n_state;
            c_sync_count    <= n_sync_count;
            c_collect_count <= n_collect_count;
            c_score_count   <= n_score_count;
            c_score_a       <= n_score_a;
            c_score_b       <= n_score_b;
            c_check_p8      <= n_check_p8;
            c_check_p16     <= n_check_p16;
            c_check_p20     <= n_check_p20;
            c_check_count   <= n_check_count;
            c_replay_count  <= n_replay_count;
            c_phase_b       <= n_phase_b;
            c_sfd_shift     <= n_sfd_shift;
            c_first8        <= n_first8;
            c_first16       <= n_first16;
            c_first20       <= n_first20;
            c_symbol_code   <= n_symbol_code;
            c_symbol_valid  <= n_symbol_valid;
            c_frame_start   <= n_frame_start;
            c_frame_abort   <= n_frame_abort;

            // The first non-SYNC block becomes candidate block zero.
            if ((c_state == ST_WAIT_NON_SYNC) && block_code_valid &&
                ((block_code == CODE_BIT0) || (block_code == CODE_BIT1))) begin
                p8_mem[0]  <= bin8_power;
                p16_mem[0] <= bin16_power;
                p20_mem[0] <= bin20_power;
            end

            // Store candidate blocks one through sixteen.
            if ((c_state == ST_COLLECT) && fft_block_done) begin
                p8_mem[c_collect_count]  <= bin8_power;
                p16_mem[c_collect_count] <= bin16_power;
                p20_mem[c_collect_count] <= bin20_power;
            end
        end
    end

    always @(*) begin
        n_state         = c_state;
        n_sync_count    = c_sync_count;
        n_collect_count = c_collect_count;
        n_score_count   = c_score_count;
        n_score_a       = c_score_a;
        n_score_b       = c_score_b;
        n_check_p8      = c_check_p8;
        n_check_p16     = c_check_p16;
        n_check_p20     = c_check_p20;
        n_check_count   = c_check_count;
        n_replay_count  = c_replay_count;
        n_phase_b       = c_phase_b;
        n_sfd_shift     = c_sfd_shift;
        n_first8        = c_first8;
        n_first16       = c_first16;
        n_first20       = c_first20;
        n_symbol_code   = c_symbol_code;
        n_symbol_valid  = 0;
        n_frame_start   = 0;
        n_frame_abort   = 0;

        // frame_finish can arrive while this module is waiting for the first
        // half of the next symbol, so it has priority over every active state.
        if (frame_finish) begin
            n_state         = ST_SEARCH;
            n_sync_count    = 0;
            n_collect_count = 0;
            n_score_count   = 0;
            n_score_a       = 0;
            n_score_b       = 0;
            n_check_count   = 0;
            n_replay_count  = 0;
        end else begin
            case (c_state)
                ST_SEARCH: begin
                    if (block_code_valid) begin
                        if (block_code == CODE_SYNC) begin
                            if (c_sync_count == SYNC_MIN_BLOCKS-1) begin
                                n_state      = ST_WAIT_NON_SYNC;
                                n_sync_count = 0;
                            end else begin
                                n_sync_count = c_sync_count + 1'b1;
                            end
                        end else begin
                            n_sync_count = 0;
                        end
                    end
                end

                // Keep waiting while the transmitter continues the SYNC tone.
                // Start the 17-block window at the first detected data tone.
                ST_WAIT_NON_SYNC: begin
                    if (block_code_valid &&
                        ((block_code == CODE_BIT0) ||
                         (block_code == CODE_BIT1))) begin
                        n_collect_count = 1;
                        n_state         = ST_COLLECT;
                    end
                end

                ST_COLLECT: begin
                    if (fft_block_done) begin
                        if (c_collect_count == 16) begin
                            n_score_count = 0;
                            n_score_a     = 0;
                            n_score_b     = 0;
                            n_state       = ST_SCORE;
                        end
                        else
                            n_collect_count = c_collect_count + 1'b1;
                    end
                end

                // Accumulate one expected D5-bin pair per clock.  Eight
                // clocks are available well before the next FFT block.
                ST_SCORE: begin
                    n_score_a = c_score_a + score_term_a;
                    n_score_b = c_score_b + score_term_b;
                    if (c_score_count == 7) begin
                        n_state = ST_SELECT;
                    end else begin
                        n_score_count = c_score_count + 1'b1;
                    end
                end

                // Select the phase first.  Do not assert frame_start yet.
                ST_SELECT: begin
                    n_check_count = 0;
                    n_sfd_shift   = 0;
                    if (c_score_a > c_score_b + SCORE_MARGIN) begin
                        n_phase_b = 0;
                        n_state   = ST_CHECK_CAPTURE;
                    end else if (c_score_b > c_score_a + SCORE_MARGIN) begin
                        n_phase_b = 1;
                        n_state   = ST_CHECK_CAPTURE;
                    end else begin
                        n_state = ST_SEARCH;
                    end
                end

                // First register the three selected-bin pair sums.  This
                // breaks the memory-address/add path from the threshold path.
                ST_CHECK_CAPTURE: begin
                    n_check_p8  = check_p8;
                    n_check_p16 = check_p16;
                    n_check_p20 = check_p20;
                    n_state     = ST_CHECK_SFD;
                end

                // Classify the registered pair and build the actual SFD byte.
                // Reject any non-data symbol.
                ST_CHECK_SFD: begin
                    if ((check_code == CODE_BIT0) ||
                        (check_code == CODE_BIT1)) begin
                        n_sfd_shift = {c_sfd_shift[6:0], check_code[0]};
                        if (c_check_count == 7) begin
                            if ({c_sfd_shift[6:0], check_code[0]} ==
                                SFD_PATTERN) begin
                                n_replay_count = 0;
                                n_frame_start  = 1;
                                n_state        = ST_REPLAY;
                            end else begin
                                n_state = ST_SEARCH;
                            end
                        end else begin
                            n_check_count = c_check_count + 1'b1;
                            n_state       = ST_CHECK_CAPTURE;
                        end
                    end else begin
                        n_state = ST_SEARCH;
                    end
                end

                // The received SFD was already verified. Replay it to the
                // frame decoder after frame_start, one bit per clock.
                ST_REPLAY: begin
                    n_symbol_code =
                        SFD_PATTERN[7-c_replay_count] ? CODE_BIT1 : CODE_BIT0;
                    n_symbol_valid = 1;
                    if (c_replay_count == 7) begin
                        if (c_phase_b) begin
                            n_state = ST_FIRST;
                        end else begin
                            // In phase A, block 16 is already the first half
                            // of the first payload bit.
                            n_first8  = p8_mem[16];
                            n_first16 = p16_mem[16];
                            n_first20 = p20_mem[16];
                            n_state   = ST_SECOND;
                        end
                    end else begin
                        n_replay_count = c_replay_count + 1'b1;
                    end
                end

                ST_FIRST: begin
                    if (fft_block_done) begin
                        n_first8  = bin8_power;
                        n_first16 = bin16_power;
                        n_first20 = bin20_power;
                        n_state   = ST_SECOND;
                    end
                end

                // Inline two-block classifier for ID, DATA, and CRC bits.
                ST_SECOND: begin
                    if (fft_block_done) begin
                        if ((payload8_x10 > payload16_x12) &&
                            (payload8_x10 > payload20_x12)) begin
                            n_symbol_code  = CODE_BIT0;
                            n_symbol_valid = 1;
                            n_state        = ST_FIRST;
                        end else if ((payload16_x10 > payload8_x12) &&
                                     (payload16_x10 > payload20_x12)) begin
                            n_symbol_code  = CODE_BIT1;
                            n_symbol_valid = 1;
                            n_state        = ST_FIRST;
                        end else begin
                            n_frame_abort = 1;
                            n_state       = ST_SEARCH;
                        end
                    end
                end

                default: begin
                    n_state = ST_SEARCH;
                end
            endcase
        end
    end
endmodule
