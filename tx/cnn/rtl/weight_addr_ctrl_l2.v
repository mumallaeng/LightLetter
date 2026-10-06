`timescale 1ns / 1ps

module weight_addr_ctrl_l2 #(
    parameter OCH = 16
) (
    input                        clk,
    input                        rst_n,
    // ce_ctrl
    input                        mac_start,
    output reg                   mac_done,
    input                        is_ch35,
    // weight rom
    output                       rom_is_ch35,
    output reg [$clog2(OCH)-1:0] out_ch_sel,
    // mac array
    output                       cal_valid_next,  // = WEIGHT_CAL, weight ROM 읽기 enable 용 (주소와 같은 clk)
    output reg                   cal_valid        // BRAM 읽기 지연에 맞춰 1clk 지연된 valid
);
    // ========== FSM ==========
    localparam IDLE = 0;
    localparam WEIGHT_CAL = 1;

    reg c_state, n_state;

    // ----- register -----
    reg [$clog2(OCH)-1:0] out_ch_sel_next;
    reg mac_done_next;

    // ----- State Update Logic -----
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            c_state <= IDLE;
            mac_done <= 0;
            out_ch_sel <= 0;
            cal_valid <= 0;
        end else begin
            c_state <= n_state;
            mac_done <= mac_done_next;
            out_ch_sel <= out_ch_sel_next;
            cal_valid <= cal_valid_next;
        end
    end

    // ----- Next state Logic -----
    always @(*) begin
        n_state         = c_state;
        mac_done_next   = mac_done;
        out_ch_sel_next = out_ch_sel;

        case (c_state)
            IDLE: begin
                out_ch_sel_next = 0;
                mac_done_next   = 0;
                if (mac_start) begin
                    n_state = WEIGHT_CAL;
                end
            end
            WEIGHT_CAL: begin
                out_ch_sel_next = out_ch_sel + 1;
                if (out_ch_sel == OCH - 1) begin
                    out_ch_sel_next = 0;
                    mac_done_next = 1;
                    n_state = IDLE;
                end
            end
        endcase
    end

    // ----- Moore output logic -----
    // weight ROM(BRAM)은 out_ch_sel 주소를 받은 다음 clk에 weight를 출력하므로
    // cal_valid도 1clk 지연시켜 weight_out과 같은 clk에 맞춘다
    assign cal_valid_next = c_state;  // equal to c_state = WEIGHT_CAL

    // ----- is_ch35 pass-through (ce_ctrl -> weight rom) -----
    assign rom_is_ch35 = is_ch35;
endmodule
