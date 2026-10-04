`timescale 1ns / 1ps

// One frozen 128-bin spectrum, with a direct AXI GPIO-facing interface.
// All ports use clk. Reset together with fft_top; never reset this tracker
// alone during a running FFT stream. First valid after reset must be bin 0,
// followed by exactly 128 valid samples per FFT block in natural order.
// This observer has no backpressure connection to the receiver.
module fft_snapshot_buffer (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [39:0] fft_power,
    input  wire        fft_valid,

    // [0] capture, [1] release, [2] read: rising-edge commands.
    // [14:8] read bin. Use one command at a time; lower it before reuse.
    // Keep read high until status[3], then read both data words before
    // lowering read. Before each read, lower read and wait for status[3]=0.
    input  wire [31:0] gpio_ctrl,

    // [0] ready, [1] waiting/capturing, [2] snapshot valid,
    // [3] read done (held while read command is high), [14:8] returned bin.
    // Data outputs are stable until the next completed read/capture/release.
    output wire [31:0] gpio_status,
    output wire [31:0] gpio_power_lo,
    output wire [31:0] gpio_power_hi
);

    localparam [1:0] ST_IDLE    = 2'd0;
    localparam [1:0] ST_WAIT    = 2'd1;
    localparam [1:0] ST_CAPTURE = 2'd2;
    localparam [1:0] ST_HOLD    = 2'd3;

    reg [1:0] state;
    reg [6:0] stream_bin;
    reg [2:0] cmd_prev;

    reg [39:0] power_mem [0:127];
    reg [39:0] ram_read_data;
    reg [6:0]  pending_bin;
    reg        read_pending;
    reg [39:0] read_result;
    reg [6:0]  read_bin;
    reg        read_done;

    wire capture_edge = gpio_ctrl[0] & ~cmd_prev[0];
    wire release_edge = gpio_ctrl[1] & ~cmd_prev[1];
    wire read_edge    = gpio_ctrl[2] & ~cmd_prev[2];

    // Release wins over all commands in HOLD. Commands while busy are
    // ignored, not queued. Capture is accepted only in IDLE.
    wire capture_accept = rst_n && (state == ST_IDLE) &&
                          capture_edge && !gpio_ctrl[1];
    wire release_accept = rst_n && (state == ST_HOLD) && release_edge;
    wire read_accept = rst_n && (state == ST_HOLD) &&
                       read_edge && !gpio_ctrl[1];
    wire capture_write = rst_n && fft_valid &&
                         ((state == ST_CAPTURE) ||
                          ((state == ST_WAIT) && (stream_bin == 7'd0)));

    assign gpio_status = {17'd0, read_bin, 4'd0,
                          read_done, (state == ST_HOLD),
                          ((state == ST_WAIT) || (state == ST_CAPTURE)),
                          (state == ST_IDLE)};
    assign gpio_power_lo = read_result[31:0];
    assign gpio_power_hi = {24'd0, read_result[39:32]};

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state      <= ST_IDLE;
            stream_bin <= 7'd0;
            cmd_prev   <= 3'd0;
        end else begin
            cmd_prev <= gpio_ctrl[2:0];
            if (fft_valid)
                stream_bin <= stream_bin + 7'd1;

            case (state)
                ST_IDLE:
                    if (capture_accept)
                        state <= ST_WAIT;
                ST_WAIT:
                    if (fft_valid && stream_bin == 7'd0)
                        state <= ST_CAPTURE;
                ST_CAPTURE:
                    if (fft_valid && stream_bin == 7'd127)
                        state <= ST_HOLD;
                ST_HOLD:
                    if (release_accept)
                        state <= ST_IDLE;
                default: state <= ST_IDLE;
            endcase
        end
    end

    // No RAM reset: all 128 entries are overwritten before HOLD is set.
    always @(posedge clk) begin
        if (capture_write)
            power_mem[stream_bin] <= fft_power;
        if (read_accept)
            ram_read_data <= power_mem[gpio_ctrl[14:8]];
    end

    // Pipeline the synchronous RAM read and retain its result for the PS.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            read_pending <= 1'b0;
            pending_bin  <= 7'd0;
            read_result  <= 40'd0;
            read_bin     <= 7'd0;
            read_done    <= 1'b0;
        end else begin
            read_pending <= read_accept;
            if (read_accept)
                pending_bin <= gpio_ctrl[14:8];

            if (capture_accept || release_accept) begin
                read_pending <= 1'b0;
                read_result  <= 40'd0;
                read_bin     <= 7'd0;
                read_done    <= 1'b0;
            end else if (!gpio_ctrl[2]) begin
                read_done <= 1'b0;
            end else if (read_accept) begin
                read_done <= 1'b0;
            end else if (read_pending && (state == ST_HOLD)) begin
                read_result <= ram_read_data;
                read_bin    <= pending_bin;
                read_done   <= 1'b1;
            end
        end
    end
endmodule
