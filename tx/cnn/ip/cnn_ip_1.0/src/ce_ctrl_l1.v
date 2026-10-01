`timescale 1ns / 1ps

module ce_ctrl_l1 (
    input      clk,
    input      rst_n,
    // axis
    input      s_axis_tvalid,
    output     s_axis_tready,
    input      s_axis_tuser,
    input      s_axis_tlast,
    // line buffer array
    input      win_valid,
    output     pixel_valid,
    output reg phase_clear,
    // weight_addr_ctrl
    output reg mac_start,
    input      mac_done
);
    // ========== FSM ==========
    localparam [1:0] IDLE = 0;
    localparam [1:0] IMG_IN = 1;
    localparam [1:0] WAIT_MAC = 2;
    localparam [1:0] STOP = 3;

    reg [1:0] c_state, n_state;

    // ----- register -----
    reg phase_clear_next, mac_start_next;
    reg ch_count, ch_count_next;

    // ----- State Update logic -----
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            c_state     <= IDLE;
            phase_clear <= 0;
            mac_start   <= 0;
            ch_count    <= 0;
        end else begin
            c_state     <= n_state;
            phase_clear <= phase_clear_next;
            mac_start   <= mac_start_next;
            ch_count    <= ch_count_next;
        end
    end

    // ----- Next state logic -----
    always @(*) begin
        n_state          = c_state;
        phase_clear_next = phase_clear;
        mac_start_next   = 0;

        case (c_state)
            IDLE: begin
                mac_start_next   = 0;
                phase_clear_next = 0;
                if (s_axis_tvalid) begin
                    n_state = IMG_IN;
                end
            end
            IMG_IN: begin
                if (win_valid) begin
                    mac_start_next = 1;
                    n_state        = WAIT_MAC;
                end
            end
            WAIT_MAC: begin
                if (mac_done) begin
                    if (ch_count == 1) begin
                        phase_clear_next = 1;
                        n_state          = STOP;
                    end else begin
                        n_state = IMG_IN;
                    end
                end
            end
            STOP: begin
                phase_clear_next = 0;
                n_state          = IDLE;
            end
        endcase
    end

    // ----- channel count logic -----
    always @(*) begin
        ch_count_next = ch_count;
        if (c_state == STOP) begin
            ch_count_next = 0;
        end else if (s_axis_tlast & pixel_valid) begin
            ch_count_next = ch_count + 1;
        end
    end


    // ========== Output Logic ==========
    assign pixel_valid = s_axis_tvalid & s_axis_tready;
    assign s_axis_tready = ((c_state==IDLE) | (c_state==IMG_IN)) & ~win_valid;
endmodule
