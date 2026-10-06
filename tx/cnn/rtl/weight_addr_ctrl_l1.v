`timescale 1ns / 1ps

module weight_addr_ctrl_l1 #(
    parameter OCH = 6
) (
    input                        clk,
    input                        rst_n,
    // ce_ctrl
    input                        mac_start,
    output reg                   mac_done,
    // zero gating
    input                        win_zero,   // 3x3 window 가 전부 0
    // weight rom
    output     [$clog2(OCH)-1:0] out_ch_sel,
    // mac array
    output                       cal_valid
);
    // ========== FSM ==========
    localparam IDLE = 0;
    localparam WEIGHT_CAL = 1;

    reg c_state, n_state;

    // ----- register -----
    reg [$clog2(OCH)-1:0] out_ch_cnt, out_ch_cnt_next;
    reg mac_done_next;

    // ----- State Update Logic -----
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            c_state <= IDLE;
            mac_done <= 0;
            out_ch_cnt <= 0;
        end else begin
            c_state <= n_state;
            mac_done <= mac_done_next;
            out_ch_cnt <= out_ch_cnt_next;
        end
    end

    // ----- Next state Logic -----
    always @(*) begin
        n_state         = c_state;
        mac_done_next   = mac_done;
        out_ch_cnt_next = out_ch_cnt;

        case (c_state)
            IDLE: begin
                out_ch_cnt_next = 0;
                mac_done_next = 0;
                if (mac_start) begin
                    n_state = WEIGHT_CAL;
                end
            end
            WEIGHT_CAL: begin
                out_ch_cnt_next = out_ch_cnt + 1;
                if (out_ch_cnt == OCH-1) begin
                    out_ch_cnt_next = 0;
                    mac_done_next = 1;
                    n_state = IDLE;
                end
            end
        endcase
    end

    // ----- Moore output logic -----
    assign cal_valid = c_state;  // equal to c_state = WEIGHT_CAL

    // ----- zero gating -----
    // weight_rom_l1 은 조합 case mux 라 select 를 고정하면 mux 가 토글하지 않는다
    // (window 가 전부 0 이면 곱 결과는 mul_en 으로 어차피 0 이 된다)
    assign out_ch_sel = win_zero ? {$clog2(OCH){1'b0}} : out_ch_cnt;
endmodule
