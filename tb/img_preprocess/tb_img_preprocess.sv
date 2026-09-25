`timescale 1ns / 1ps

module tb_img_preprocess;

    localparam int IN_W  = 1280;
    localparam int IN_H  = 720;
    localparam int OUT_W = 28;
    localparam int OUT_H = 28;

    localparam int TOTAL_OUTPUT_PIXELS =
        OUT_W * OUT_H;

    logic clk   = 1'b0;
    logic rst_n = 1'b0;

    logic capture_req = 1'b0;

    logic [23:0] s_data  = 24'd0;
    logic        s_valid = 1'b0;
    wire         s_ready;
    logic        s_user  = 1'b0;
    logic        s_last  = 1'b0;

    wire [15:0] m_data;
    wire        m_valid;
    logic       m_ready = 1'b0;
    wire        m_user;
    wire        m_last;

    // 1280x720 input image memory
    //
    // Pixel format:
    //   [23:16] = R
    //   [15:8]  = B
    //   [7:0]   = G
    logic [23:0] image_mem [0:IN_W * IN_H - 1];

    integer out_count  = 0;
    integer user_count = 0;
    integer last_count = 0;

    integer final_stall_count = 0;

    integer pgm;

    // 100 MHz clock
    always #5 clk = ~clk;

    img_preprocess #(
        .IN_WIDTH(IN_W),
        .IN_HEIGHT(IN_H),
        .OUT_WIDTH(OUT_W),
        .OUT_HEIGHT(OUT_H)
    ) dut (
        .axis_aclk(clk),
        .axis_aresetn(rst_n),

        .capture_req(capture_req),

        .s_axis_tdata(s_data),
        .s_axis_tvalid(s_valid),
        .s_axis_tready(s_ready),
        .s_axis_tuser(s_user),
        .s_axis_tlast(s_last),

        .m_axis_tdata(m_data),
        .m_axis_tvalid(m_valid),
        .m_axis_tready(m_ready),
        .m_axis_tuser(m_user),
        .m_axis_tlast(m_last)
    );

    // Output TREADY generation
    //
    // Normal output pixels:
    //   Randomly stop the output to verify AXI backpressure.
    //
    // Final output pixel:
    //   Force TREADY low for five cycles to verify that the
    //   final pixel and capture state are held correctly.
    always @(negedge clk) begin
        if (!rst_n) begin
            m_ready <= 1'b0;
            final_stall_count <= 0;
        end else begin
            if (
                m_valid &&
                dut.m_axis_frame_last &&
                (final_stall_count < 5)
            ) begin
                m_ready <= 1'b0;
                final_stall_count <=
                    final_stall_count + 1;
            end else begin
                m_ready <= (($urandom % 5) != 0);
            end
        end
    end

    // Send one input pixel using AXI4-Stream.
    task automatic send_pixel(
        input int x,
        input int y
    );
        begin
            @(negedge clk);

            s_data  <= image_mem[y * IN_W + x];
            s_user  <= (x == 0) && (y == 0);
            s_last  <= (x == IN_W - 1);
            s_valid <= 1'b1;

            // Keep TVALID and input data stable until
            // the DUT asserts TREADY.
            do begin
                @(posedge clk);
            end while (!s_ready);

            @(negedge clk);

            s_valid <= 1'b0;
            s_user  <= 1'b0;
            s_last  <= 1'b0;
        end
    endtask

    // Send one complete 1280x720 input frame.
    //
    // If press_button is 1, capture_req becomes high
    // at input coordinate (100, 100).
    //
    // capture_req stays high after this task so that
    // the testbench can verify rising-edge behavior.
    task automatic send_frame(
        input bit press_button
    );

        integer x;
        integer y;

        begin
            for (y = 0; y < IN_H; y = y + 1) begin
                for (x = 0; x < IN_W; x = x + 1) begin

                    if (
                        press_button &&
                        (x == 100) &&
                        (y == 100)
                    ) begin
                        @(negedge clk);

                        capture_req <= 1'b1;

                        $display(
                            "Capture request asserted at input (%0d, %0d)",
                            x,
                            y
                        );
                    end

                    send_pixel(x, y);
                end
            end
        end
    endtask

    // Send only the first lines of an input frame.
    //
    // This is used to verify that a continuously high
    // capture_req does not trigger another capture.
    task automatic send_partial_frame(
        input int number_of_lines
    );

        integer x;
        integer y;

        begin
            for (
                y = 0;
                y < number_of_lines;
                y = y + 1
            ) begin
                for (x = 0; x < IN_W; x = x + 1) begin
                    send_pixel(x, y);
                end
            end
        end
    endtask

    // Check every transferred output pixel.
    always @(posedge clk) begin : check_output

        integer ox;
        integer oy;

        integer src_x;
        integer src_y;

        logic [23:0] expected_pixel;

        integer expected_r;
        integer expected_g;
        integer expected_b;
        integer expected_gray;

        if (rst_n && m_valid && m_ready) begin

            // No more than one 28x28 frame may be output.
            if (out_count >= TOTAL_OUTPUT_PIXELS) begin
                $fatal(
                    1,
                    "Unexpected extra output pixel after one captured frame"
                );
            end

            // Current output coordinates
            ox = out_count % OUT_W;
            oy = out_count / OUT_W;

            // Nearest-neighbor source coordinates
            src_x =
                ((2 * ox + 1) * IN_W) /
                (2 * OUT_W);

            src_y =
                ((2 * oy + 1) * IN_H) /
                (2 * OUT_H);

            // Read the expected RGB pixel from memory.
            expected_pixel =
                image_mem[src_y * IN_W + src_x];

            // Current project stream order is R-B-G.
            expected_r = expected_pixel[23:16];
            expected_b = expected_pixel[15:8];
            expected_g = expected_pixel[7:0];

            // Same grayscale calculation as img_preprocess.v
            expected_gray =
                (77  * expected_r +
                 150 * expected_g +
                 29  * expected_b +
                 128) >> 8;

            // Check grayscale value.
            if (
                m_data[7:0] !==
                expected_gray[7:0]
            ) begin
                $fatal(
                    1,
                    "Pixel %0d mismatch: got %0d expected %0d",
                    out_count,
                    m_data[7:0],
                    expected_gray
                );
            end

            // Upper 8 bits must be zero.
            if (m_data[15:8] !== 8'd0) begin
                $fatal(
                    1,
                    "Upper bits mismatch at pixel %0d: m_data=%h",
                    out_count,
                    m_data
                );
            end

            // TUSER must occur only on the first output pixel.
            if (m_user !== (out_count == 0)) begin
                $fatal(
                    1,
                    "TUSER mismatch at output pixel %0d",
                    out_count
                );
            end

            // TLAST must occur on every 28th output pixel.
            if (m_last !== (ox == OUT_W - 1)) begin
                $fatal(
                    1,
                    "TLAST mismatch at output pixel %0d",
                    out_count
                );
            end

            if (m_user)
                user_count = user_count + 1;

            if (m_last)
                last_count = last_count + 1;

            // Write the captured 28x28 grayscale output
            // as an ASCII PGM image.
            $fwrite(
                pgm,
                "%0d%c",
                m_data[7:0],
                (ox == OUT_W - 1) ? 10 : 32
            );

            out_count = out_count + 1;
        end
    end

    initial begin : run_test

        integer timeout;
        integer saved_out_count;

        // Load the 1280x720 R-B-G input image.
        $readmemh(
            "3_1280x720_rbg.mem",
            image_mem
        );

        // Open the output grayscale image.
        pgm = $fopen(
            "img_preprocess_capture_28x28.pgm",
            "w"
        );

        if (pgm == 0) begin
            $fatal(
                1,
                "Could not open img_preprocess_capture_28x28.pgm"
            );
        end

        // ASCII PGM header
        $fwrite(
            pgm,
            "P2\n28 28\n255\n"
        );

        // Initially keep every input inactive.
        capture_req <= 1'b0;
        s_data      <= 24'd0;
        s_valid     <= 1'b0;
        s_user      <= 1'b0;
        s_last      <= 1'b0;

        // Keep reset asserted for five clock cycles.
        repeat (5)
            @(posedge clk);

        // Release active-low reset.
        rst_n <= 1'b1;

        repeat (5)
            @(posedge clk);

        // -------------------------------------------------
        // Frame 1
        //
        // Press the capture button in the middle of this
        // frame. No output may be generated from frame 1.
        // -------------------------------------------------
        $display(
            "Sending frame 1: capture request occurs in the middle"
        );

        send_frame(1'b1);

        repeat (10)
            @(posedge clk);

        if (out_count != 0) begin
            $fatal(
                1,
                "Frame 1 produced output before the next TUSER: %0d pixels",
                out_count
            );
        end

        $display(
            "Frame 1 complete: no output, as expected"
        );

        // -------------------------------------------------
        // Frame 2
        //
        // capture_req is still high, but only its original
        // rising edge is used. Capture must begin at the
        // TUSER of frame 2 and produce exactly 784 pixels.
        // -------------------------------------------------
        $display(
            "Sending frame 2: this frame must be captured"
        );

        send_frame(1'b0);

        // Wait for all 784 output pixels.
        timeout = 0;

        while (
            (out_count < TOTAL_OUTPUT_PIXELS) &&
            (timeout < 10000)
        ) begin
            @(posedge clk);

            timeout = timeout + 1;
        end

        if (out_count != TOTAL_OUTPUT_PIXELS) begin
            $fatal(
                1,
                "Captured output count mismatch: got %0d expected %0d",
                out_count,
                TOTAL_OUTPUT_PIXELS
            );
        end

        $display(
            "Frame 2 captured: %0d output pixels",
            out_count
        );

        // -------------------------------------------------
        // Frame 3, partial
        //
        // capture_req is still high. Because no new rising
        // edge occurred, this frame must not be captured.
        //
        // Twenty lines are enough to pass the first selected
        // input row, which is near Y=12.
        // -------------------------------------------------
        saved_out_count = out_count;

        $display(
            "Sending part of frame 3: no additional capture expected"
        );

        send_partial_frame(20);

        repeat (20)
            @(posedge clk);

        if (out_count != saved_out_count) begin
            $fatal(
                1,
                "capture_req high level incorrectly triggered another frame"
            );
        end

        // Release capture_req so a future button press can
        // produce another rising edge.
        @(negedge clk);

        capture_req <= 1'b0;

        repeat (10)
            @(posedge clk);

        $fclose(pgm);

        // Check total output count.
        if (out_count != TOTAL_OUTPUT_PIXELS) begin
            $fatal(
                1,
                "Final output count mismatch: got %0d expected %0d",
                out_count,
                TOTAL_OUTPUT_PIXELS
            );
        end

        // TUSER must occur exactly once.
        if (user_count != 1) begin
            $fatal(
                1,
                "TUSER count mismatch: got %0d expected 1",
                user_count
            );
        end

        // TLAST must occur once per output line.
        if (last_count != OUT_H) begin
            $fatal(
                1,
                "TLAST count mismatch: got %0d expected %0d",
                last_count,
                OUT_H
            );
        end

        // The final output pixel must have been stalled
        // for exactly five negative clock edges.
        if (final_stall_count != 5) begin
            $fatal(
                1,
                "Final pixel backpressure test failed: stall count=%0d",
                final_stall_count
            );
        end

        $display(
            "PASS: one requested frame captured successfully"
        );

        $display(
            "PASS: %0d pixels, %0d TUSER, %0d TLAST",
            out_count,
            user_count,
            last_count
        );

        $display(
            "PASS: final pixel held during %0d TREADY-low cycles",
            final_stall_count
        );

        $display(
            "Output image: img_preprocess_capture_28x28.pgm"
        );

        $finish;
    end

endmodule