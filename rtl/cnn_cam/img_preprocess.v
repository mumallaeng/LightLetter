`timescale 1ns / 1ps

module img_preprocess #(
    parameter IN_WIDTH   = 112,
    parameter IN_HEIGHT  = 112,
    parameter OUT_WIDTH  = 28,
    parameter OUT_HEIGHT = 28,
    parameter MARGIN     = 3,
    parameter THRESHOLD  = 160
) (
    input  wire         axis_aclk,
    input  wire         axis_aresetn,

    // Retained for block-design compatibility. A DMA transfer itself starts
    // a capture, so this input is intentionally not used by the new datapath.
    input  wire         capture_req,

    // AXI4-Stream input: one tightly packed 112x112 R-B-G image per packet.
    input  wire [23:0]  s_axis_tdata,
    input  wire         s_axis_tvalid,
    output wire         s_axis_tready,
    input  wire         s_axis_tuser,
    input  wire         s_axis_tlast,

    // AXI4-Stream output: one 28x28 CNN image per packet.
    output reg  [15:0]  m_axis_tdata,
    output reg          m_axis_tvalid,
    input  wire         m_axis_tready,
    output reg          m_axis_tuser,
    output reg          m_axis_tlast
);

    localparam integer IN_PIXELS   = IN_WIDTH * IN_HEIGHT;
    localparam integer OUT_PIXELS  = OUT_WIDTH * OUT_HEIGHT;
    localparam integer GLYPH_LIMIT = OUT_WIDTH - (2 * MARGIN);
    localparam integer IN_ADDR_W   = $clog2(IN_PIXELS);
    localparam integer OUT_X_W     = $clog2(OUT_WIDTH);
    localparam integer OUT_Y_W     = $clog2(OUT_HEIGHT);

    localparam [3:0] ST_RECV      = 4'd0;
    localparam [3:0] ST_PREP      = 4'd1;
    localparam [3:0] ST_FIT       = 4'd2;
    localparam [3:0] ST_X_STORE   = 4'd3;
    localparam [3:0] ST_X_ADV     = 4'd4;
    localparam [3:0] ST_Y_STORE   = 4'd5;
    localparam [3:0] ST_Y_ADV     = 4'd6;
    localparam [3:0] ST_OUT_PREP  = 4'd7;
    localparam [3:0] ST_MEM_READ  = 4'd8;
    localparam [3:0] ST_OUT_LOAD  = 4'd9;
    localparam [3:0] ST_OUT_WAIT  = 4'd10;
    localparam [3:0] ST_FIT_DONE  = 4'd11;

    reg [3:0] state;

    // The input is converted immediately; only the processed Gray8 frame is
    // buffered. This maps to about 12.25 KiB for a 112x112 input.
    reg [7:0] pixel_mem [0:IN_PIXELS-1];

    reg [IN_ADDR_W-1:0] input_count;
    reg [6:0] input_x;
    reg [6:0] input_y;
    reg       input_frame_full;

    // Two input pipeline stages break the DMA/width-converter path before
    // grayscale arithmetic and again before threshold/bounding-box updates.
    // Both stages sustain one accepted pixel per clock.
    reg                    in_pipe0_valid;
    reg [7:0]              in_pipe0_r;
    reg [7:0]              in_pipe0_g;
    reg [7:0]              in_pipe0_b;
    reg [IN_ADDR_W-1:0]    in_pipe0_count;
    reg [6:0]              in_pipe0_x;
    reg [6:0]              in_pipe0_y;
    reg                    in_pipe0_last;

    reg                    in_pipe1_valid;
    reg [15:0]             in_pipe1_luma_sum;
    reg [IN_ADDR_W-1:0]    in_pipe1_count;
    reg [6:0]              in_pipe1_x;
    reg [6:0]              in_pipe1_y;
    reg                    in_pipe1_last;

    reg       foreground_found;
    reg [6:0] bbox_min_x;
    reg [6:0] bbox_max_x;
    reg [6:0] bbox_min_y;
    reg [6:0] bbox_max_y;
    reg [7:0] bbox_width;
    reg [7:0] bbox_height;

    reg [5:0] glyph_width;
    reg [5:0] glyph_height;
    reg [4:0] offset_x;
    reg [4:0] offset_y;

    // Aspect-ratio fitting is intentionally iterative.  The old function
    // expanded 22 dependent comparisons into one combinational timing path.
    reg [7:0] fit_short_side_reg;
    reg [7:0] fit_long_side_reg;
    reg [5:0] fit_candidate;
    reg [5:0] fit_best;
    reg [5:0] fit_final_size;
    reg       fit_is_wide;

    // Destination-to-source maps avoid a variable hardware divider.
    reg [6:0] x_map [0:GLYPH_LIMIT-1];
    reg [6:0] y_map [0:GLYPH_LIMIT-1];
    reg [5:0] map_index;
    reg [7:0] map_source;
    reg [15:0] map_accum;

    reg [OUT_X_W-1:0] out_x;
    reg [OUT_Y_W-1:0] out_y;
    reg [IN_ADDR_W-1:0] read_addr;
    reg [7:0] read_data;

    // Internal diagnostic flag: asserted if TLAST does not coincide with the
    // fixed 112x112 packet boundary. Processing still completes by count.
    reg input_protocol_error;

    wire input_accept = s_axis_tvalid && s_axis_tready;
    wire output_accept = m_axis_tvalid && m_axis_tready;

    wire [7:0] input_luma = in_pipe1_luma_sum[15:8];
    wire [7:0] input_inverted = 8'd255 - input_luma;
    wire [7:0] input_processed =
        (input_inverted < THRESHOLD) ? 8'd0 : input_inverted;

    wire fit_candidate_ok =
        ((fit_candidate * fit_long_side_reg) <=
         (fit_short_side_reg * GLYPH_LIMIT));
    wire [5:0] fit_result = fit_candidate_ok ? fit_candidate : fit_best;

    assign s_axis_tready = (state == ST_RECV) && !input_frame_full;

    // Streaming input pipeline.  Metadata follows each pixel so memory and
    // bounding-box updates still refer to the exact accepted input sample.
    always @(posedge axis_aclk or negedge axis_aresetn) begin
        if (!axis_aresetn) begin
            in_pipe0_valid    <= 1'b0;
            in_pipe0_r        <= 0;
            in_pipe0_g        <= 0;
            in_pipe0_b        <= 0;
            in_pipe0_count    <= 0;
            in_pipe0_x        <= 0;
            in_pipe0_y        <= 0;
            in_pipe0_last     <= 1'b0;
            in_pipe1_valid    <= 1'b0;
            in_pipe1_luma_sum <= 0;
            in_pipe1_count    <= 0;
            in_pipe1_x        <= 0;
            in_pipe1_y        <= 0;
            in_pipe1_last     <= 1'b0;
        end else begin
            in_pipe0_valid <= input_accept;
            if (input_accept) begin
                // Existing project byte order: R-B-G.
                in_pipe0_r     <= s_axis_tdata[23:16];
                in_pipe0_b     <= s_axis_tdata[15:8];
                in_pipe0_g     <= s_axis_tdata[7:0];
                in_pipe0_count <= input_count;
                in_pipe0_x     <= input_x;
                in_pipe0_y     <= input_y;
                in_pipe0_last  <= s_axis_tlast;
            end

            in_pipe1_valid <= in_pipe0_valid;
            if (in_pipe0_valid) begin
                in_pipe1_luma_sum <=
                    (16'd77  * in_pipe0_r) +
                    (16'd150 * in_pipe0_g) +
                    (16'd29  * in_pipe0_b) +
                    16'd128;
                in_pipe1_count <= in_pipe0_count;
                in_pipe1_x     <= in_pipe0_x;
                in_pipe1_y     <= in_pipe0_y;
                in_pipe1_last  <= in_pipe0_last;
            end
        end
    end

    // Keep the frame memory in a reset-free synchronous process so Vivado
    // can infer block RAM. The controller never reads it before a complete
    // frame has been written.
    always @(posedge axis_aclk) begin
        if (in_pipe1_valid)
            pixel_mem[in_pipe1_count] <= input_processed;

        if (state == ST_MEM_READ)
            read_data <= pixel_mem[read_addr];
    end

    always @(posedge axis_aclk or negedge axis_aresetn) begin
        if (!axis_aresetn) begin
            state                <= ST_RECV;
            input_count          <= 0;
            input_x              <= 0;
            input_y              <= 0;
            input_frame_full     <= 1'b0;
            foreground_found     <= 1'b0;
            bbox_min_x           <= IN_WIDTH - 1;
            bbox_max_x           <= 0;
            bbox_min_y           <= IN_HEIGHT - 1;
            bbox_max_y           <= 0;
            bbox_width           <= 0;
            bbox_height          <= 0;
            glyph_width          <= 0;
            glyph_height         <= 0;
            offset_x             <= 0;
            offset_y             <= 0;
            fit_short_side_reg   <= 0;
            fit_long_side_reg    <= 0;
            fit_candidate        <= 0;
            fit_best             <= 0;
            fit_final_size       <= 0;
            fit_is_wide          <= 1'b0;
            map_index            <= 0;
            map_source           <= 0;
            map_accum            <= 0;
            out_x                <= 0;
            out_y                <= 0;
            read_addr            <= 0;
            input_protocol_error <= 1'b0;
            m_axis_tdata         <= 16'd0;
            m_axis_tvalid        <= 1'b0;
            m_axis_tuser         <= 1'b0;
            m_axis_tlast         <= 1'b0;
        end else begin
            case (state)
                ST_RECV: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tuser  <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (in_pipe1_valid) begin
                        if (input_processed != 0) begin
                            foreground_found <= 1'b1;
                            if (!foreground_found || (in_pipe1_x < bbox_min_x))
                                bbox_min_x <= in_pipe1_x;
                            if (!foreground_found || (in_pipe1_x > bbox_max_x))
                                bbox_max_x <= in_pipe1_x;
                            if (!foreground_found || (in_pipe1_y < bbox_min_y))
                                bbox_min_y <= in_pipe1_y;
                            if (!foreground_found || (in_pipe1_y > bbox_max_y))
                                bbox_max_y <= in_pipe1_y;
                        end

                        if (in_pipe1_last != (in_pipe1_count == IN_PIXELS - 1))
                            input_protocol_error <= 1'b1;

                        if (in_pipe1_count == IN_PIXELS - 1) begin
                            state       <= ST_PREP;
                        end
                    end

                    if (input_accept) begin
                        if (input_count == IN_PIXELS - 1) begin
                            input_count      <= 0;
                            input_x          <= 0;
                            input_y          <= 0;
                            input_frame_full <= 1'b1;
                        end else begin
                            input_count <= input_count + 1'b1;
                            if (input_x == IN_WIDTH - 1) begin
                                input_x <= 0;
                                input_y <= input_y + 1'b1;
                            end else begin
                                input_x <= input_x + 1'b1;
                            end
                        end
                    end
                end

                ST_PREP: begin
                    out_x <= 0;
                    out_y <= 0;

                    if (!foreground_found) begin
                        bbox_width   <= 0;
                        bbox_height  <= 0;
                        glyph_width  <= 0;
                        glyph_height <= 0;
                        offset_x     <= 0;
                        offset_y     <= 0;
                        state        <= ST_OUT_PREP;
                    end else begin
                        bbox_width  <= bbox_max_x - bbox_min_x + 1'b1;
                        bbox_height <= bbox_max_y - bbox_min_y + 1'b1;

                        fit_is_wide <=
                            ((bbox_max_x - bbox_min_x) >=
                             (bbox_max_y - bbox_min_y));
                        if ((bbox_max_x - bbox_min_x) >=
                            (bbox_max_y - bbox_min_y)) begin
                            fit_short_side_reg <=
                                bbox_max_y - bbox_min_y + 1'b1;
                            fit_long_side_reg <=
                                bbox_max_x - bbox_min_x + 1'b1;
                        end else begin
                            fit_short_side_reg <=
                                bbox_max_x - bbox_min_x + 1'b1;
                            fit_long_side_reg <=
                                bbox_max_y - bbox_min_y + 1'b1;
                        end
                        fit_candidate <= 1;
                        fit_best      <= 1;
                        state         <= ST_FIT;
                    end
                end

                ST_FIT: begin
                    if (fit_candidate_ok)
                        fit_best <= fit_candidate;

                    if (fit_candidate == GLYPH_LIMIT) begin
                        // Register the final comparison result before doing
                        // size/offset arithmetic in the following cycle.
                        fit_final_size <= fit_result;
                        state          <= ST_FIT_DONE;
                    end else begin
                        fit_candidate <= fit_candidate + 1'b1;
                    end
                end

                ST_FIT_DONE: begin
                        if (fit_is_wide) begin
                            glyph_width  <= GLYPH_LIMIT;
                            glyph_height <= fit_final_size;
                            offset_x     <= (OUT_WIDTH - GLYPH_LIMIT) / 2;
                            offset_y     <= (OUT_HEIGHT - fit_final_size) / 2;
                        end else begin
                            glyph_width  <= fit_final_size;
                            glyph_height <= GLYPH_LIMIT;
                            offset_x     <= (OUT_WIDTH - fit_final_size) / 2;
                            offset_y     <= (OUT_HEIGHT - GLYPH_LIMIT) / 2;
                        end
                        map_index  <= 0;
                        map_source <= 0;
                        map_accum  <= 0;
                        state      <= ST_X_STORE;
                end

                ST_X_STORE: begin
                    x_map[map_index] <= bbox_min_x + map_source;
                    if (map_index == glyph_width - 1'b1) begin
                        map_index  <= 0;
                        map_source <= 0;
                        map_accum  <= 0;
                        state      <= ST_Y_STORE;
                    end else begin
                        map_accum <= map_accum + bbox_width;
                        state     <= ST_X_ADV;
                    end
                end

                ST_X_ADV: begin
                    if (map_accum >= glyph_width) begin
                        map_accum  <= map_accum - glyph_width;
                        map_source <= map_source + 1'b1;
                    end else begin
                        map_index <= map_index + 1'b1;
                        state     <= ST_X_STORE;
                    end
                end

                ST_Y_STORE: begin
                    y_map[map_index] <= bbox_min_y + map_source;
                    if (map_index == glyph_height - 1'b1) begin
                        state <= ST_OUT_PREP;
                    end else begin
                        map_accum <= map_accum + bbox_height;
                        state     <= ST_Y_ADV;
                    end
                end

                ST_Y_ADV: begin
                    if (map_accum >= glyph_height) begin
                        map_accum  <= map_accum - glyph_height;
                        map_source <= map_source + 1'b1;
                    end else begin
                        map_index <= map_index + 1'b1;
                        state     <= ST_Y_STORE;
                    end
                end

                ST_OUT_PREP: begin
                    if (!foreground_found ||
                        (out_x < offset_x) ||
                        (out_x >= offset_x + glyph_width) ||
                        (out_y < offset_y) ||
                        (out_y >= offset_y + glyph_height)) begin
                        m_axis_tdata  <= 16'd0;
                        m_axis_tuser  <= (out_x == 0) && (out_y == 0);
                        m_axis_tlast  <= (out_x == OUT_WIDTH - 1) &&
                                         (out_y == OUT_HEIGHT - 1);
                        m_axis_tvalid <= 1'b1;
                        state         <= ST_OUT_WAIT;
                    end else begin
                        read_addr <=
                            (y_map[out_y - offset_y] * IN_WIDTH) +
                             x_map[out_x - offset_x];
                        state <= ST_MEM_READ;
                    end
                end

                ST_MEM_READ: begin
                    state     <= ST_OUT_LOAD;
                end

                ST_OUT_LOAD: begin
                    m_axis_tdata  <= {8'd0, read_data};
                    m_axis_tuser  <= (out_x == 0) && (out_y == 0);
                    m_axis_tlast  <= (out_x == OUT_WIDTH - 1) &&
                                     (out_y == OUT_HEIGHT - 1);
                    m_axis_tvalid <= 1'b1;
                    state         <= ST_OUT_WAIT;
                end

                ST_OUT_WAIT: begin
                    if (output_accept) begin
                        m_axis_tvalid <= 1'b0;
                        m_axis_tuser  <= 1'b0;
                        m_axis_tlast  <= 1'b0;

                        if ((out_x == OUT_WIDTH - 1) &&
                            (out_y == OUT_HEIGHT - 1)) begin
                            foreground_found     <= 1'b0;
                            bbox_min_x           <= IN_WIDTH - 1;
                            bbox_max_x           <= 0;
                            bbox_min_y           <= IN_HEIGHT - 1;
                            bbox_max_y           <= 0;
                            input_protocol_error <= 1'b0;
                            input_frame_full     <= 1'b0;
                            out_x                 <= 0;
                            out_y                 <= 0;
                            state                 <= ST_RECV;
                        end else if (out_x == OUT_WIDTH - 1) begin
                            out_x <= 0;
                            out_y <= out_y + 1'b1;
                            state <= ST_OUT_PREP;
                        end else begin
                            out_x <= out_x + 1'b1;
                            state <= ST_OUT_PREP;
                        end
                    end
                end

                default: begin
                    state <= ST_RECV;
                end
            endcase
        end
    end

    // Keep compatibility-only inputs visible to lint without changing logic.
    wire unused_inputs = capture_req ^ s_axis_tuser;

endmodule
