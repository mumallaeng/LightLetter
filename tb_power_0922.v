`timescale 1ns / 1ps

module tb_power_0922();

   parameter ADC_FILE    = "fft_input_20000hz.mem";
parameter OUTPUT_FILE = "power_result_bit1.csv";

    reg clk=0, rst=1, i_read_data_valid=0;
    wire o_bf_out_valid;

    always #10 clk = ~clk;  // 50 MHz

    reg signed [19:0] a_re=0, a_im=0;
    reg signed [19:0] b_re=0, b_im=0;
    reg [5:0] twiddle_addr=0;

    wire [31:0] twiddle_factor;
    wire signed [19:0] y0_re, y0_im, y1_re, y1_im;

    reg [39:0] adc_samples [0:127];
    reg [39:0] fft_mem     [0:127];

    reg [39:0] saved_y0, saved_y1;
    reg [39:0] i_fft_core_data=0;
    reg i_fft_core_valid=0;

    wire [39:0] power_result;
    wire fft_mag_valid;
    wire signed [19:0] power_input_re = $signed(i_fft_core_data[39:20]);
    wire signed [19:0] power_input_im = $signed(i_fft_core_data[19:0]);
    reg expected_valid=0;
    reg [39:0] expected_power=0;
    reg signed [19:0] captured_re=0, captured_im=0;

    // Independent wide-integer reference for the Power input.
    function [39:0] reference_power;
        input [39:0] data;
        reg signed [63:0] re_value, im_value;
        begin
            re_value = $signed(data[39:20]);
            im_value = $signed(data[19:0]);
            reference_power = re_value*re_value + im_value*im_value;
        end
    endfunction

    integer stage, half_size, group_base, j, n, a_addr, b_addr;
    integer supplied=0, received=0, fout;

    twiddle_rom u_twiddle_rom (
        .clk(clk),
        .twiddle_addr(twiddle_addr),
        .twiddle_factor(twiddle_factor)
    );

    butterfly u_butterfly (
        .clk(clk),
        .rst(rst),
        .i_read_data_valid(i_read_data_valid),
        .o_bf_out_valid(o_bf_out_valid),
        .a_re(a_re), .a_im(a_im),
        .b_re(b_re), .b_im(b_im),
        .twiddle_factor(twiddle_factor),
        .y0_re(y0_re), .y0_im(y0_im),
        .y1_re(y1_re), .y1_im(y1_im)
    );

    power u_power (
        .clk(clk),
        .rst(rst),
        .i_fft_core_data(i_fft_core_data),
        .i_fft_core_valid(i_fft_core_valid),
        .power_result(power_result),
        .fft_mag_valid(fft_mag_valid)
    );

    // Bit-reversed sample address
    function [6:0] reverse7;
        input [6:0] x;
        begin
            reverse7 = {x[0], x[1], x[2], x[3], x[4], x[5], x[6]};
        end
    endfunction

    // Compare the previous input with the current pre-edge output.
    always @(posedge clk) begin
        if (rst) begin
            expected_valid = 0;
            expected_power = 0;
        end else begin
            if (fft_mag_valid !== expected_valid)
                $fatal(1, "Output valid delay mismatch");
            if (fft_mag_valid === 1'b1) begin
                if (received >= supplied || received >= 128)
                    $fatal(1, "Unexpected Power output");
                if (power_result !== expected_power)
                    $fatal(1, "Power mismatch index=%0d expected=%0d got=%0d",
                           received, expected_power, power_result);
                $fdisplay(fout, "%0d,%0d,%0d,%0d,%0d", received,
                          captured_re, captured_im, expected_power, power_result);
                received = received + 1;
            end
            // Update reference AFTER checking the preceding transaction.
            expected_valid = i_fft_core_valid;
            if (i_fft_core_valid) begin
                if ((^i_fft_core_data) === 1'bx)
                    $fatal(1, "Unknown Power input");
                expected_power = reference_power(i_fft_core_data);
                captured_re = power_input_re;
                captured_im = power_input_im;
            end
        end
    end

    // Run one Butterfly operation; hold A/B/W through both writes
    task run_pair;
        begin
            twiddle_addr <= j * (64 / half_size);

            @(posedge clk);
            {a_re, a_im} <= fft_mem[a_addr];
            {b_re, b_im} <= fft_mem[b_addr];
            i_read_data_valid <= 1'b1;

            @(posedge clk);
            i_read_data_valid <= 1'b0;

            @(posedge clk);
            while (o_bf_out_valid !== 1'b1)
                @(posedge clk);

            saved_y0 = {y0_re, y0_im};
            saved_y1 = {y1_re, y1_im};

            @(posedge clk); fft_mem[a_addr] = saved_y0;
            @(posedge clk); fft_mem[b_addr] = saved_y1;
        end
    endtask

    initial begin
        // Load and reorder ADC samples
        $readmemh(ADC_FILE, adc_samples);
        for (n = 0; n < 128; n = n + 1) begin
            if ((^adc_samples[n]) === 1'bx)
                $fatal(1, "Missing ADC sample %0d", n);
            fft_mem[n] = adc_samples[reverse7(n)];
        end

        fout = $fopen(OUTPUT_FILE, "w");
        if (!fout) $fatal(1, "Cannot open output CSV");
        $fdisplay(fout, "output_index,re,im,expected_power,power_result");

        // Reset
        repeat (3) @(posedge clk);
        @(negedge clk); rst = 1'b0;
        @(posedge clk);

        // Seven FFT stages, 64 Butterfly operations per stage
        for (stage = 0; stage < 7; stage = stage + 1) begin
            half_size = 1 << stage;

            for (group_base = 0; group_base < 128;
                 group_base = group_base + 2 * half_size) begin
                for (j = 0; j < half_size; j = j + 1) begin
                    a_addr = group_base + j;
                    b_addr = a_addr + half_size;
                    run_pair;
                end
            end

            if (stage < 6) @(posedge clk);
        end

        // Supply FFT results; output collection runs concurrently
        for (n = 0; n < 128; n = n + 1) begin
            @(negedge clk);
            i_fft_core_data  = fft_mem[n];
            i_fft_core_valid = 1'b1;
            @(posedge clk); #1; supplied = supplied + 1;
        end
        @(negedge clk); i_fft_core_valid = 1'b0;

        // Receive the final registered output
        while (received < 128) begin
            @(posedge clk); #1;
        end

        // Check for unwanted output after collection
        repeat (3) begin
            @(posedge clk); #1;
            if (fft_mag_valid !== 1'b0)
                $fatal(1, "Extra or unknown output valid");
        end

        $fclose(fout);
        $display(
            "TB_POWER_0922 PASS: supplied=%0d received=%0d; file=%s",
            supplied, received, OUTPUT_FILE
        );
        $display(
            "Power arithmetic and registered valid checked; FFT arithmetic and RX decoding not checked."
        );
        $finish;
    end

    // Global timeout
    initial begin
        #500000;
        $fatal(1, "Timeout: supplied=%0d received=%0d",
               supplied, received);
    end

endmodule