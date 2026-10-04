`timescale 1ns / 1ps

module ce_ctrl_l2 (
    input      clk,
    input      rst_n,
    // pre pool layer
    input      pool_valid,
    output     pool_ready,
    input      pool_ch_done,
    // line buffer array
    input      win_valid,
    output     pixel_valid,
    output reg phase_clear,
    // weight_addr_ctrl
    output     is_ch35,
    output reg mac_start,
    input      mac_done
);
    // ========== FSM ==========
    localparam [2:0] IDLE = 0;
    localparam [2:0] CH02_IMG_IN = 1;
    localparam [2:0] WAIT_MAC_02 = 2;
    localparam [2:0] WAIT_LB_RST = 3;
    localparam [2:0] CH35_IMG_IN = 4;
    localparam [2:0] WAIT_MAC_35 = 5;
    localparam [2:0] STOP = 6;

    reg [2:0] c_state, n_state;

    // ----- register -----
    reg phase_clear_next, mac_start_next;
    reg [1:0] ch_count, ch_count_next;

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
                if (pool_valid) begin
                    n_state = CH02_IMG_IN;
                end
            end
            CH02_IMG_IN: begin
                if (win_valid) begin
                    mac_start_next = 1;
                    n_state        = WAIT_MAC_02;
                end
            end
            WAIT_MAC_02: begin
                if (mac_done) begin
                    if (ch_count == 1) begin
                        phase_clear_next = 1;
                        n_state          = WAIT_LB_RST;
                    end else begin
                        n_state = CH02_IMG_IN;
                    end
                end
            end
            WAIT_LB_RST: begin
                phase_clear_next = 0;
                n_state = CH35_IMG_IN;
            end
            CH35_IMG_IN: begin
                if (win_valid) begin
                    mac_start_next = 1;
                    n_state        = WAIT_MAC_35;
                end
            end
            WAIT_MAC_35: begin
                if (mac_done) begin
                    if (ch_count == 2) begin
                        phase_clear_next = 1;
                        n_state          = STOP;
                    end else begin
                        n_state = CH35_IMG_IN;
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
        end else if (pool_ch_done & pixel_valid) begin
            ch_count_next = ch_count + 1;
        end
    end

    // ========== Output Logic ==========
    assign pixel_valid = pool_valid & pool_ready;
    wire img_in_state = (c_state==CH02_IMG_IN) | (c_state==CH35_IMG_IN);
    assign pool_ready = ((c_state==IDLE) | (img_in_state)) & ~win_valid;
    assign is_ch35 = (c_state == CH35_IMG_IN) | (c_state == WAIT_MAC_35);
endmodule
