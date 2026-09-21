`timescale 1ns / 1ps
// Lane Packer: gathers PACK quantized values into one FIFO entry.
//   pack_data = {done, v[PACK-1], ..., v[0]}, first value in [15:0]

module lane_packer #(
    parameter PACK = 3  // 1..3
) (
    input  wire             clk,
    input  wire             rst_n,
    input  wire [15:0]      q_in,
    input  wire             q_valid,
    input  wire             q_done,       // last-pixel flag
    output wire [16*PACK:0] pack_data,
    output wire             pack_valid
);

    localparam CNT_W = (PACK > 1) ? $clog2(PACK) : 1;

    generate
        if (PACK == 1) begin : GEN_PASS
            assign pack_data[15:0]    = q_in;
            assign pack_data[16*PACK] = q_done;
            assign pack_valid         = q_valid;
        end else begin : GEN_PACK
            // registers: reg / reg_next
            reg [CNT_W-1:0] cnt, cnt_next;
            reg [16*(PACK-1)-1:0] hold, hold_next;

            wire last = (cnt == $unsigned(PACK - 1));

            // held values in the low lanes, the incoming value in the top lane
            assign pack_data  = {q_done, q_in, hold};
            assign pack_valid = q_valid & last;

            always @(*) begin
                cnt_next  = cnt;
                hold_next = hold;

                if (q_valid) begin
                    cnt_next = last ? {CNT_W{1'b0}} : cnt + 1'b1;
                    if (!last) hold_next[{cnt, 4'd0}+:16] = q_in;  // lane offset = cnt * 16
                end
            end

            always @(posedge clk) begin
                if (!rst_n) begin
                    cnt  <= {CNT_W{1'b0}};
                    hold <= {(16*(PACK-1)){1'b0}};
                end else begin
                    cnt  <= cnt_next;
                    hold <= hold_next;
                end
            end
        end
    endgenerate

endmodule
