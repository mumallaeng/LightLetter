`timescale 1ns / 1ps

module img_preprocess #(
    parameter IN_WIDTH   = 1280,
    parameter IN_HEIGHT  = 720,
    parameter OUT_WIDTH  = 28,
    parameter OUT_HEIGHT = 28
) (
    input  wire         axis_aclk,
    input  wire         axis_aresetn,

    // Capture request
    input  wire         capture_req,

    // AXI4-Stream input
    input  wire [23:0]  s_axis_tdata,
    input  wire         s_axis_tvalid,
    output wire         s_axis_tready,
    input  wire         s_axis_tuser,
    input  wire         s_axis_tlast,

    // AXI4-Stream output
    output reg  [15:0]  m_axis_tdata,
    output reg          m_axis_tvalid,
    input  wire         m_axis_tready,
    output reg          m_axis_tuser,
    output reg          m_axis_tlast
);

    localparam IX_W = $clog2(IN_WIDTH);
    localparam IY_W = $clog2(IN_HEIGHT);
    localparam OX_W = $clog2(OUT_WIDTH);
    localparam OY_W = $clog2(OUT_HEIGHT + 1);

    localparam X_DEN   = 2 * OUT_WIDTH;
    localparam Y_DEN   = 2 * OUT_HEIGHT;
    localparam X_REM_W = $clog2(X_DEN);
    localparam Y_REM_W = $clog2(Y_DEN);

    localparam X_FIRST     = IN_WIDTH / X_DEN;
    localparam Y_FIRST     = IN_HEIGHT / Y_DEN;
    localparam X_FIRST_REM = IN_WIDTH % X_DEN;
    localparam Y_FIRST_REM = IN_HEIGHT % Y_DEN;

    localparam X_STEP_BASE = (2 * IN_WIDTH) / X_DEN;
    localparam Y_STEP_BASE = (2 * IN_HEIGHT) / Y_DEN;
    localparam X_STEP_REM  = (2 * IN_WIDTH) % X_DEN;
    localparam Y_STEP_REM  = (2 * IN_HEIGHT) % Y_DEN;

    // Capture state
    localparam STATE_IDLE    = 2'd0;
    localparam STATE_ARMED   = 2'd1;
    localparam STATE_CAPTURE = 2'd2;

    reg [1:0] capture_state;

    // Capture request edge detection
    reg capture_req_d;
    wire capture_req_rise;

    // Input and output coordinates
    reg [IX_W-1:0] in_x;
    reg [IY_W-1:0] in_y;
    reg [OX_W-1:0] out_x;
    reg [OY_W-1:0] out_y;

    // Next input coordinate to select
    reg [IX_W-1:0] target_x_reg;
    reg [IY_W-1:0] target_y_reg;
    reg [X_REM_W-1:0] x_rem_reg;
    reg [Y_REM_W-1:0] y_rem_reg;

    // Marks the final pixel of the complete 28x28 frame
    reg m_axis_frame_last;

    wire output_slot_free;
    wire input_accept;
    wire output_accept;
    wire capture_now;

    wire [IX_W-1:0] cur_x;
    wire [IY_W-1:0] cur_y;
    wire [OX_W-1:0] cur_out_x;
    wire [OY_W-1:0] cur_out_y;
    wire [IX_W-1:0] cur_target_x;
    wire [IY_W-1:0] cur_target_y;

    wire [X_REM_W:0] next_x_rem_sum;
    wire [Y_REM_W:0] next_y_rem_sum;

    wire selected;

    wire [7:0] pix_r;
    wire [7:0] pix_g;
    wire [7:0] pix_b;
    wire [15:0] luma_sum;
    wire [7:0] luma;

    assign capture_req_rise =
        capture_req & ~capture_req_d;

    assign output_slot_free =
        (~m_axis_tvalid) | m_axis_tready;

    assign s_axis_tready =
        output_slot_free;

    assign input_accept =
        s_axis_tvalid & s_axis_tready;

    assign output_accept =
        m_axis_tvalid & m_axis_tready;

    // Normally capture is enabled only in STATE_CAPTURE.
    // The additional conditions allow capture to begin correctly
    // if capture_req and TUSER arrive on the same clock.
    assign capture_now =
        (capture_state == STATE_CAPTURE) ||
        ((capture_state == STATE_ARMED) &&
         s_axis_tuser) ||
        ((capture_state == STATE_IDLE) &&
         capture_req_rise &&
         s_axis_tuser);

    // TUSER re-synchronizes all coordinates to the start of a frame.
    assign cur_x =
        s_axis_tuser ? 0 : in_x;

    assign cur_y =
        s_axis_tuser ? 0 : in_y;

    assign cur_out_x =
        s_axis_tuser ? 0 : out_x;

    assign cur_out_y =
        s_axis_tuser ? 0 : out_y;

    assign cur_target_x =
        s_axis_tuser ? X_FIRST : target_x_reg;

    assign cur_target_y =
        s_axis_tuser ? Y_FIRST : target_y_reg;

    assign next_x_rem_sum =
        x_rem_reg + X_STEP_REM;

    assign next_y_rem_sum =
        y_rem_reg + Y_STEP_REM;

    // A pixel is selected only while one frame is being captured.
    assign selected =
        capture_now &&
        (cur_out_y < OUT_HEIGHT) &&
        (cur_x == cur_target_x) &&
        (cur_y == cur_target_y);

    // Input pixel order: R-B-G
    assign pix_r = s_axis_tdata[23:16];
    assign pix_b = s_axis_tdata[15:8];
    assign pix_g = s_axis_tdata[7:0];

    // Grayscale:
    // luma = (77*R + 150*G + 29*B + 128) / 256
    assign luma_sum =
        (16'd77  * pix_r) +
        (16'd150 * pix_g) +
        (16'd29  * pix_b) +
        16'd128;

    assign luma =
        luma_sum[15:8];

    always @(posedge axis_aclk or negedge axis_aresetn) begin
        if (!axis_aresetn) begin
            capture_req_d    <= 1'b0;
            capture_state    <= STATE_IDLE;

            in_x             <= 0;
            in_y             <= 0;
            out_x            <= 0;
            out_y            <= 0;

            target_x_reg     <= X_FIRST;
            target_y_reg     <= Y_FIRST;
            x_rem_reg        <= X_FIRST_REM;
            y_rem_reg        <= Y_FIRST_REM;

            m_axis_tdata     <= 16'd0;
            m_axis_tvalid    <= 1'b0;
            m_axis_tuser     <= 1'b0;
            m_axis_tlast     <= 1'b0;
            m_axis_frame_last <= 1'b0;
        end else begin
            // Previous capture_req value for rising-edge detection
            capture_req_d <= capture_req;

            // Capture state machine
            case (capture_state)
                STATE_IDLE: begin
                    if (capture_req_rise) begin
                        if (input_accept && s_axis_tuser)
                            capture_state <= STATE_CAPTURE;
                        else
                            capture_state <= STATE_ARMED;
                    end
                end

                STATE_ARMED: begin
                    if (input_accept && s_axis_tuser)
                        capture_state <= STATE_CAPTURE;
                end

                STATE_CAPTURE: begin
                    // Finish only after the final output pixel
                    // has actually been accepted by the receiver.
                    if (output_accept && m_axis_frame_last)
                        capture_state <= STATE_IDLE;
                end

                default: begin
                    capture_state <= STATE_IDLE;
                end
            endcase

            // Clear output control signals when the output register
            // is empty or the previous output has been accepted.
            if (output_slot_free) begin
                m_axis_tvalid     <= 1'b0;
                m_axis_tuser      <= 1'b0;
                m_axis_tlast      <= 1'b0;
                m_axis_frame_last <= 1'b0;
            end

            // Process only an accepted AXI input pixel.
            if (input_accept) begin
                // Start of a new input frame
                if (s_axis_tuser) begin
                    out_x        <= 0;
                    out_y        <= 0;

                    target_x_reg <= X_FIRST;
                    target_y_reg <= Y_FIRST;

                    x_rem_reg    <= X_FIRST_REM;
                    y_rem_reg    <= Y_FIRST_REM;
                end

                // Output only selected pixels during STATE_CAPTURE.
                if (selected) begin
                    m_axis_tdata  <= {8'd0, luma};
                    m_axis_tvalid <= 1'b1;

                    // First pixel of the 28x28 output frame
                    m_axis_tuser <=
                        (cur_out_x == 0) &&
                        (cur_out_y == 0);

                    // End of each 28-pixel output line
                    m_axis_tlast <=
                        (cur_out_x == OUT_WIDTH - 1);

                    // Final pixel of the complete 28x28 frame
                    m_axis_frame_last <=
                        (cur_out_x == OUT_WIDTH - 1) &&
                        (cur_out_y == OUT_HEIGHT - 1);

                    // Move to the next output coordinate.
                    if (cur_out_x == OUT_WIDTH - 1) begin
                        out_x <= 0;
                        out_y <= cur_out_y + 1'b1;

                        target_x_reg <= X_FIRST;
                        x_rem_reg    <= X_FIRST_REM;

                        if (next_y_rem_sum >= Y_DEN) begin
                            target_y_reg <=
                                cur_target_y +
                                Y_STEP_BASE + 1;

                            y_rem_reg <=
                                next_y_rem_sum - Y_DEN;
                        end else begin
                            target_y_reg <=
                                cur_target_y +
                                Y_STEP_BASE;

                            y_rem_reg <=
                                next_y_rem_sum;
                        end
                    end else begin
                        out_x <= cur_out_x + 1'b1;
                        out_y <= cur_out_y;

                        if (next_x_rem_sum >= X_DEN) begin
                            target_x_reg <=
                                cur_target_x +
                                X_STEP_BASE + 1;

                            x_rem_reg <=
                                next_x_rem_sum - X_DEN;
                        end else begin
                            target_x_reg <=
                                cur_target_x +
                                X_STEP_BASE;

                            x_rem_reg <=
                                next_x_rem_sum;
                        end
                    end
                end

                // Track input coordinates using input TLAST.
                if (s_axis_tlast) begin
                    in_x <= 0;

                    if (cur_y == IN_HEIGHT - 1)
                        in_y <= 0;
                    else
                        in_y <= cur_y + 1'b1;
                end else begin
                    in_x <= cur_x + 1'b1;
                    in_y <= cur_y;
                end
            end
        end
    end

endmodule