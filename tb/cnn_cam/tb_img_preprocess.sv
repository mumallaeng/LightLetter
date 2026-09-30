`timescale 1ns / 1ps

module tb_img_preprocess;

    localparam int IN_W      = 112;
    localparam int IN_H      = 112;
    localparam int OUT_W     = 28;
    localparam int OUT_H     = 28;
    localparam int MARGIN    = 3;
    localparam int THRESHOLD = 160;

    localparam int IN_PIXELS  = IN_W * IN_H;
    localparam int OUT_PIXELS = OUT_W * OUT_H;

    // Synthetic foreground bounding box: 40x80.
    localparam int BOX_X0 = 30;
    localparam int BOX_X1 = 69;
    localparam int BOX_Y0 = 16;
    localparam int BOX_Y1 = 95;

    // 40x80 fitted inside 22x22 becomes 11x22.
    localparam int GLYPH_W = 11;
    localparam int GLYPH_H = 22;
    localparam int OFF_X   = (OUT_W - GLYPH_W) / 2;
    localparam int OFF_Y   = (OUT_H - GLYPH_H) / 2;

    logic clk = 1'b0;
    logic rst_n = 1'b0;
    logic capture_req = 1'b0;

    logic [23:0] s_data = 24'd0;
    logic s_valid = 1'b0;
    wire  s_ready;
    logic s_user = 1'b0;
    logic s_last = 1'b0;

    wire [15:0] m_data;
    wire m_valid;
    logic m_ready = 1'b0;
    wire m_user;
    wire m_last;

    integer out_count = 0;
    integer user_count = 0;
    integer last_count = 0;
    integer final_stall_count = 0;
    integer pgm;

    always #5 clk = ~clk;

    img_preprocess #(
        .IN_WIDTH(IN_W),
        .IN_HEIGHT(IN_H),
        .OUT_WIDTH(OUT_W),
        .OUT_HEIGHT(OUT_H),
        .MARGIN(MARGIN),
        .THRESHOLD(THRESHOLD)
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

    // Random output backpressure plus a deterministic five-cycle stall on
    // the final pixel.
    always @(negedge clk) begin
        if (!rst_n) begin
            m_ready <= 1'b0;
            final_stall_count <= 0;
        end else if (m_valid && m_last && final_stall_count < 5) begin
            m_ready <= 1'b0;
            final_stall_count <= final_stall_count + 1;
        end else begin
            m_ready <= (($urandom % 5) != 0);
        end
    end

    function automatic [23:0] source_pixel(input int x, input int y);
        begin
            // Background white -> inverted value 0.
            if ((x >= BOX_X0) && (x <= BOX_X1) &&
                (y >= BOX_Y0) && (y <= BOX_Y1)) begin
                // Existing project order R-B-G. This produces luma 28 and
                // inverted output 227.
                source_pixel = {8'd20, 8'd40, 8'd30};
            end else begin
                source_pixel = 24'hffffff;
            end
        end
    endfunction

    task automatic send_image;
        integer x;
        integer y;
        begin
            for (y = 0; y < IN_H; y = y + 1) begin
                for (x = 0; x < IN_W; x = x + 1) begin
                    @(negedge clk);
                    s_data  <= source_pixel(x, y);
                    s_valid <= 1'b1;
                    s_user  <= 1'b0;
                    s_last  <= (x == IN_W-1) && (y == IN_H-1);

                    do @(posedge clk); while (!s_ready);
                end
            end

            @(negedge clk);
            s_valid <= 1'b0;
            s_last  <= 1'b0;
            s_data  <= 24'd0;
        end
    endtask

    always @(posedge clk) begin : check_output
        integer ox;
        integer oy;
        integer expected;

        if (rst_n && m_valid && m_ready) begin
            if (out_count >= OUT_PIXELS)
                $fatal(1, "Unexpected output pixel %0d", out_count);

            ox = out_count % OUT_W;
            oy = out_count / OUT_W;
            expected = ((ox >= OFF_X) && (ox < OFF_X + GLYPH_W) &&
                        (oy >= OFF_Y) && (oy < OFF_Y + GLYPH_H)) ? 227 : 0;

            if (m_data !== {8'd0, expected[7:0]})
                $fatal(1,
                    "Pixel %0d (%0d,%0d) mismatch: got %0d expected %0d",
                    out_count, ox, oy, m_data[7:0], expected);

            if (m_user !== (out_count == 0))
                $fatal(1, "TUSER mismatch at output %0d", out_count);

            if (m_last !== (out_count == OUT_PIXELS-1))
                $fatal(1, "TLAST mismatch at output %0d", out_count);

            if (m_user) user_count = user_count + 1;
            if (m_last) last_count = last_count + 1;

            $fwrite(pgm, "%0d%c", m_data[7:0],
                    (ox == OUT_W-1) ? 10 : 32);
            out_count = out_count + 1;
        end
    end

    initial begin : run_test
        integer timeout;

        pgm = $fopen("img_preprocess_bbox_28x28.pgm", "w");
        if (pgm == 0)
            $fatal(1, "Could not open output PGM");
        $fwrite(pgm, "P2\n28 28\n255\n");

        repeat (5) @(posedge clk);
        rst_n <= 1'b1;
        repeat (3) @(posedge clk);

        send_image();

        timeout = 0;
        while ((out_count < OUT_PIXELS) && (timeout < 50000)) begin
            @(posedge clk);
            timeout = timeout + 1;
        end

        $fclose(pgm);

        if (out_count != OUT_PIXELS)
            $fatal(1, "Output count %0d, expected %0d", out_count, OUT_PIXELS);
        if (user_count != 1)
            $fatal(1, "TUSER count %0d, expected 1", user_count);
        if (last_count != 1)
            $fatal(1, "TLAST count %0d, expected 1", last_count);
        if (final_stall_count != 5)
            $fatal(1, "Final stall count %0d, expected 5", final_stall_count);
        if (dut.input_protocol_error !== 1'b0)
            $fatal(1, "Input TLAST did not match the 112x112 packet boundary");

        $display("PASS: 112x112 RGB packet accepted");
        $display("PASS: bbox 40x80 -> glyph 11x22 in 28x28 canvas");
        $display("PASS: 3-pixel minimum margin and 784 output pixels");
        $display("PASS: one TUSER, one frame-end TLAST, backpressure held");
        $finish;
    end

endmodule
