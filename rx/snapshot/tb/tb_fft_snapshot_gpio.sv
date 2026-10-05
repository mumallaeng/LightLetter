`timescale 1ns / 1ps
module tb_fft_snapshot_gpio;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst_n = 0;
    reg [39:0] fft_power = 0;
    reg fft_valid = 0;
    reg [31:0] ctrl = 0;
    wire [31:0] status, lo, hi;
    integer k;
    integer reads = 0;
    fft_snapshot_buffer dut (
        .clk(clk), .rst_n(rst_n), .fft_power(fft_power),
        .fft_valid(fft_valid), .gpio_ctrl(ctrl),
        .gpio_status(status), .gpio_power_lo(lo), .gpio_power_hi(hi)
    );

    function automatic [39:0] sample(input integer frame, input integer bin);
        sample = (40'hA100000000 + (frame * 4096)) + bin;
    endfunction

    task automatic cycles(input integer n);
        repeat (n) begin @(posedge clk); #1; end
    endtask

    task automatic command(input [31:0] value);
        @(negedge clk); ctrl = value;
        cycles(1);
    endtask

    task automatic send_range(input integer frame, input integer first,
                              input integer last, input integer gapped);
        integer b;
        begin
            for (b = first; b <= last; b = b + 1) begin
                @(negedge clk);
                fft_valid = 1;
                fft_power = sample(frame, b);
                cycles(1);
                if (gapped && (b % 7 == 3)) begin
                    @(negedge clk); fft_valid = 0;
                    cycles(3);
                end
            end
            @(negedge clk); fft_valid = 0;
        end
    endtask

    task automatic read_check(input integer frame, input integer bin);
        integer timeout;
        reg [39:0] expected;
        reg [31:0] request;
        begin
            expected = sample(frame, bin);
            request = bin << 8;
            command(request);
            if (status[3] !== 0) $fatal(1, "stale done before read");
            command(request | 4);
            timeout = 0;
            while (!status[3] && timeout < 10) begin
                cycles(1); timeout = timeout + 1;
            end
            if (!status[3]) $fatal(1, "read timeout");
            if (status[14:8] !== bin[6:0]) $fatal(1, "wrong returned bin");
            if ({hi[7:0],lo} !== expected || hi[31:8] !== 0)
                $fatal(1, "bad data frame=%0d bin=%0d got=%h%h", frame,bin,hi,lo);
            cycles(9); // Emulate slow PS reads of separate GPIO words.
            if (!status[3] || {hi[7:0],lo} !== expected)
                $fatal(1, "result not retained");
            command(request);
            if (status[3] !== 0) $fatal(1, "done did not clear");
            reads = reads + 1;
        end
    endtask

    initial begin
        cycles(3);
        @(negedge clk); rst_n = 1;
        cycles(1);
        if (status !== 1) $fatal(1, "reset state");
        command(4); cycles(3);
        if (status[3]) $fatal(1, "read accepted before snapshot");
        command(0);

        // Request midway through frame 0: capture exactly frame 1.
        send_range(0,0,36,0);
        command(1); cycles(12);
        if (status[2:0] !== 3'b010) $fatal(1, "not waiting");
        send_range(0,37,127,1);
        if (status[2]) $fatal(1, "captured partial frame");
        send_range(1,0,126,1);
        if (status[2]) $fatal(1, "completed before last bin");
        send_range(1,127,127,0);
        if (status[2:0] !== 3'b100) $fatal(1, "no complete snapshot");

        // Further full FFT blocks must never overwrite the frozen snapshot.
        send_range(2,0,127,0);
        command(0); command(1); // Capture while HOLD is ignored.
        send_range(3,0,127,1);
        for (k = 0; k < 128; k = k + 1) read_check(1,k);
        read_check(1,127); read_check(1,0); read_check(1,0);

        // Release and read collide: release wins, no done is generated.
        command(0); command(6); cycles(3);
        if (status[3:0] !== 4'b0001) $fatal(1, "release priority");
        command(0);
        command(1); send_range(4,0,127,0);
        // Capture held high must not cause a new capture after release.
        command(3); command(1);
        send_range(5,0,127,0);
        if (status[2:0] !== 3'b001) $fatal(1, "held command retriggered");

        // Fresh capture reuses the entire memory.
        command(0); command(1); command(0);
        send_range(6,0,127,1);
        for (k = 0; k < 128; k = k + 1) read_check(6,k);

        // Reset during capture: reset stream source and observer together.
        command(2); command(0); command(1); command(0);
        send_range(7,0,23,0);
        @(negedge clk); rst_n = 0; ctrl = 0;
        cycles(2);
        if (status !== 1 || lo !== 0 || hi !== 0) $fatal(1, "reset cleanup");
        @(negedge clk); rst_n = 1;
        command(1); command(0);
        send_range(8,0,127,1);
        read_check(8,0); read_check(8,127);
        $display("PASS: %0d reads; alignment, gaps, hold, command edges, release, recapture, reset", reads);
        $finish;
    end
    initial begin #1000000; $fatal(1,"watchdog"); end
endmodule
