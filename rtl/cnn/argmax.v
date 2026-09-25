`timescale 1ns / 1ps

module argmax #(
    parameter NUM_CLASS = 36
) (
    input                                 clk,
    input                                 rst_n,
    // post fc layer
    input  signed [                 15:0] logit_data,
    input                                 logit_valid,
    output                                logit_ready,
    // Output
    output        [$clog2(NUM_CLASS)-1:0] cnn_result,
    output reg                            cnn_done
);
    // Handshake
    wire argmax_valid;
    assign argmax_valid = logit_valid & logit_ready;

    assign logit_ready = 1'b1; // actually always ready to get data from post layer

    // Index count Logic
    // -------------------------------------
    reg [$clog2(NUM_CLASS)-1:0] idx_cnt, idx_cnt_next;

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            idx_cnt <= 0;
        end else begin
            idx_cnt <= idx_cnt_next;
        end
    end

    always @(*) begin
        idx_cnt_next = idx_cnt;
        if (argmax_valid) begin
            idx_cnt_next = idx_cnt + 1;
            if (idx_cnt == NUM_CLASS - 1) begin
                idx_cnt_next = 0;
            end
        end
    end

    // Argmax Logic
    // -------------------------------------
    reg signed [15:0] max_data_reg, max_data_next;
    reg [$clog2(NUM_CLASS)-1:0] max_idx, max_idx_next;
    wire diff_logic = (logit_data > max_data_reg) ? 1'b1 : 1'b0;

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            max_data_reg <= 0;
            max_idx      <= 0;
        end else begin
            max_data_reg <= max_data_next;
            max_idx      <= max_idx_next;
        end
    end

    always @(*) begin
        max_data_next = max_data_reg;
        max_idx_next  = max_idx;

        if (argmax_valid) begin
            if (idx_cnt == 0) begin
                max_data_next = logit_data;
                max_idx_next  = 0;
            end else if (diff_logic) begin
                max_data_next = logit_data;
                max_idx_next  = idx_cnt;
            end
        end
    end

    // Output Logic
    // -------------------------------------
    reg cnn_done_next;

    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            cnn_done <= 1'b0;
        end else begin
            cnn_done <= cnn_done_next;
        end
    end

    always @(*) begin
        cnn_done_next = 1'b0;  // 1 clk pulse signal

        if (argmax_valid & (idx_cnt == NUM_CLASS - 1)) begin
            cnn_done_next = 1'b1;
        end
    end

    assign cnn_result = max_idx;
endmodule
