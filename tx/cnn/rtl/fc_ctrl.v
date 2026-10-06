`timescale 1ns / 1ps
// Controller of the shared engine: layer / group / input counters (IDLE -> RUN -> FLUSH).
module fc_ctrl #(
    parameter N_IN1      = 400,
    parameter N_IN2      = 120,
    parameter N_IN3      = 84,
    parameter GROUPS1    = 6,
    parameter GROUPS2    = 5,
    parameter GROUPS3    = 2,
    parameter ROM_BASE1  = 0,
    parameter ROM_BASE2  = 2400,
    parameter ROM_BASE3  = 3000,
    parameter BIAS_BASE1 = 0,
    parameter BIAS_BASE2 = 6,
    parameter BIAS_BASE3 = 11,
    parameter ROM_AW     = 12,
    parameter BIAS_AW    = 4,
    parameter IN_AW      = 9
) (
    input  wire               clk,
    input  wire               rst_n,
    input  wire               fc_in_valid,
    input  wire [      15:0]  feature,     // zero gating 판정용 (fc_top 의 feature mux)
    input  wire               mac_busy,
    input  wire               hold_free,
    output wire               fc_in_ready,
    output wire               fc1_in_we,
    output wire [ IN_AW-1:0]  fc1_in_waddr,
    output wire [       1:0]  layer,      // 1..3, 0 in IDLE
    output wire [       2:0]  group,
    output wire [ IN_AW-1:0]  feature_raddr,
    output wire [ROM_AW-1:0]  weight_addr, // prefetch for the next cycle
    output reg  [BIAS_AW-1:0] bias_addr,
    output reg                first,
    output reg                mac_en,
    output reg                last
);

    localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_FLUSH = 2'd2;

    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] FILL_FULL = N_IN1;

    reg [       1:0] state, state_next;
    reg [       1:0] layer_r, layer_next;
    reg [       2:0] group_r, group_next;
    reg [ IN_AW-1:0] i, i_next;
    reg [ IN_AW-1:0] fill_cnt, fill_cnt_next;
    reg [ROM_AW-1:0] grp_base, grp_base_next;

    localparam [31:0] N_IN1_C = N_IN1, N_IN2_C = N_IN2, N_IN3_C = N_IN3;
    localparam [31:0] GROUPS1_C = GROUPS1, GROUPS2_C = GROUPS2, GROUPS3_C = GROUPS3;
    reg [ IN_AW-1:0] n_in;
    reg [       3:0] n_groups;
    always @(*) begin
        case (layer_r)
            2'd1:    begin n_in = N_IN1_C[IN_AW-1:0]; n_groups = GROUPS1_C[3:0]; end
            2'd2:    begin n_in = N_IN2_C[IN_AW-1:0]; n_groups = GROUPS2_C[3:0]; end
            default: begin n_in = N_IN3_C[IN_AW-1:0]; n_groups = GROUPS3_C[3:0]; end
        endcase
    end

    // ========== zero gating ==========
    // group 0 : 전수 스캔(learn) 하며 비영 인덱스를 적재
    // group 1~: 적재된 리스트만 순회 -> MAC 사이클이 비영 개수로 줄어든다
    wire comp      = (group_r  != 3'd0);
    wire comp_next = (group_next != 3'd0);

    wire [ IN_AW-1:0] zg_idx_cur, zg_idx_nxt, zg_zero_idx;
    wire [ IN_AW:0]   zg_nz;

    localparam [IN_AW:0] MIN_ELEM = 2;

    // can 은 아래에서 assign 한다 (lastin_q 가 can 을 읽으므로 선언을 먼저 둔다)
    wire can;

    // 그룹 / 레이어 전환 직후 2 clk 쉰다.
    //   (1) 압축 그룹 첫 원소의 weight prefetch : can=0 이면 i_next == i 라
    //       weight_addr 가 "현재 원소"의 주소가 된다
    //   (2) fc_zero_gating 의 적재가 1 clk 늦으므로 zg_nz 가 확정될 시간
    reg [1:0] bub;
    wire grp_adv = (group_next != group_r) | (layer_next != layer_r);
    always @(posedge clk) begin
        if (!rst_n)       bub <= 2'd0;
        else if (grp_adv) bub <= 2'd2;
        else if (|bub)    bub <= bub - 1'b1;
    end
    wire bubble = |bub;

    // 압축 모드의 원소 개수.
    //   fc_mac 은 bias 를 1 단만 거쳐 stage2 에 쓰는데 first 는 2 단이라,
    //   first 다음 clk 까지 group_r 이 유지돼야 bias 가 맞는다 -> 그룹 길이 최소 2.
    //   비영이 0~1 개면 길이를 2 로 늘리고, 남는 사이클은 0 인 원소를 읽어 0 을 곱한다.
    wire pad = (zg_nz < MIN_ELEM);

    reg [IN_AW-1:0] n_elem;
    always @(*) begin
        if (!comp)    n_elem = n_in;
        else if (pad) n_elem = MIN_ELEM[IN_AW-1:0];
        else          n_elem = zg_nz[IN_AW-1:0];
    end

    // 실제 feature / weight 주소 오프셋 (압축 모드면 원래 인덱스로 되돌린다)
    //   i >= nz_cnt 인 패딩 사이클은 0 인 것이 확실한 인덱스를 읽는다
    wire use_pad = comp & ({1'b0, i} >= zg_nz);
    wire [IN_AW-1:0] eff_idx = !comp ? i : (use_pad ? zg_zero_idx : zg_idx_cur);

    // ---- weight ROM 주소용 offset 을 레지스터로 미리 읽어 둔다 ----
    // rp 로 idx_mem 을 한 발 앞서 읽어 wofs_q 에 담는다.
    //   읽기 경로 : rp(FF) -> idx_mem -> wofs_q(FF)      (FF 사이라 여유 있음)
    //   주소 경로 : wofs_q(FF) -> mux -> 덧셈 -> ROM     (baseline 과 같은 깊이)
    // 불변식: 어떤 사이클이든 wofs_q = "다음 사이클에 쓸 원소"의 offset
    reg [IN_AW-1:0] rp, wofs_q;

    wire [IN_AW-1:0] raw_rp = ({1'b0, rp} >= zg_nz) ? zg_zero_idx : zg_idx_nxt;

    always @(posedge clk) begin
        if (!rst_n) begin
            rp     <= {IN_AW{1'b0}};
            wofs_q <= {IN_AW{1'b0}};
        end else if (grp_adv) begin
            rp     <= {IN_AW{1'b0}};          // 그룹 전환: 읽기 포인터 초기화
        end else if (bubble | can) begin
            wofs_q <= raw_rp;
            rp     <= rp + 1'b1;
        end
    end

    // 직전 그룹의 결과가 MAC 파이프라인(3 단)에 떠 있는 동안에는 hold_free 가 아직 1 이다.
    // 이때 다음 그룹의 last 를 내보내면 fc_quant_out 의 hold 가 덮여 출력이 사라진다.
    // 원래는 그룹 길이가 n_in(>=84) 이라 문제가 없었지만, 압축하면 그룹이 짧아져 드러난다.
    reg [2:0] res_pend;
    always @(posedge clk) begin
        if (!rst_n)              res_pend <= 3'd0;
        else if (mac_en & last)  res_pend <= 3'd3;
        else if (|res_pend)      res_pend <= res_pend - 1'b1;
    end
    wire result_pending = |res_pend;

    // lastin 을 레지스터로 둔다
    //   zg_nz -> n_elem -> can -> FSM -> i_next -> idx_mem -> weight ROM 주소
    //   로 이어지는 긴 조합 경로를 끊기 위함
    reg lastin_q;
    always @(posedge clk) begin
        if (!rst_n)           lastin_q <= 1'b0;
        else if (bub == 2'd1) lastin_q <= (n_elem == {{(IN_AW - 1) {1'b0}}, 1'b1});
        else if (can)         lastin_q <= (i_next == n_elem - 1'b1);
    end

    wire lastin = lastin_q;
    wire avail  = ~((layer_r == 2'd1) & (group_r == 3'd0)) | (i < fill_cnt);
    assign can  = (state == S_RUN) & avail & ~bubble & (~lastin | (hold_free & ~result_pending));

    fc_zero_gating #(
        .DEPTH(N_IN1),
        .AW   (IN_AW)
    ) u_zero_gating (
        .clk       (clk),
        .rst_n     (rst_n),
        .clear     ((state != S_RUN) & (state_next == S_RUN)),  // 레이어 시작
        .learn     (~comp),
        .valid     (mac_en),
        .feature   (feature),
        .src_idx   (i),
        .k_cur     (i),
        .k_nxt     (rp),
        .idx_cur   (zg_idx_cur),
        .idx_nxt   (zg_idx_nxt),
        .nz_cnt    (zg_nz),
        .zero_idx  (zg_zero_idx)
    );

    // ========== input fill (independent of the FSM) ==========
    assign fc_in_ready = (fill_cnt < FILL_FULL[IN_AW-1:0]);
    assign fc1_in_we    = fc_in_valid & fc_in_ready;
    assign fc1_in_waddr = fill_cnt;

    // ========== Next State Logic ==========
    always @(*) begin : fc_ctrl_comb
        state_next    = state;
        layer_next    = layer_r;
        group_next    = group_r;
        i_next        = i;
        fill_cnt_next = fc1_in_we ? fill_cnt + 1'b1 : fill_cnt;
        grp_base_next = grp_base;
        first         = 1'b0;
        mac_en        = 1'b0;
        last          = 1'b0;

        case (state)
            S_IDLE: begin
                if (fill_cnt != {IN_AW{1'b0}}) begin
                    state_next    = S_RUN;
                    layer_next    = 2'd1;
                    group_next    = 3'd0;
                    i_next        = {IN_AW{1'b0}};
                    grp_base_next = ROM_BASE1;
                end
            end
            S_RUN: begin
                if (can) begin
                    mac_en = 1'b1;
                    first  = (i == {IN_AW{1'b0}});
                    last   = lastin;
                    if (!lastin) begin
                        i_next = i + 1'b1;
                    end else begin
                        i_next = {IN_AW{1'b0}};
                        if ({1'b0, group_r} + 1'b1 < n_groups) begin
                            group_next    = group_r + 1'b1;
                            grp_base_next = grp_base + {{(ROM_AW - IN_AW) {1'b0}}, n_in};
                            state_next    = S_RUN;
                        end else begin
                            state_next = S_FLUSH;
                        end
                    end
                end
            end
            default: begin
                if (!mac_busy & hold_free) begin
                    if (layer_r == 2'd1) fill_cnt_next = fc1_in_we ? {{(IN_AW - 1) {1'b0}}, 1'b1} : {IN_AW{1'b0}};
                    if (layer_r != 2'd3) begin
                        layer_next    = layer_r + 1'b1;
                        group_next    = 3'd0;
                        i_next        = {IN_AW{1'b0}};
                        grp_base_next = (layer_r == 2'd1) ? ROM_BASE2 : ROM_BASE3;
                        state_next    = S_RUN;
                    end else begin
                        layer_next = 2'd0;
                        state_next = S_IDLE;
                    end
                end
            end
        endcase

        // ========== Output Logic ==========
        case (layer_r)
            2'd1:    bias_addr = BIAS_BASE1 + group_r;
            2'd2:    bias_addr = BIAS_BASE2 + group_r;
            2'd3:    bias_addr = BIAS_BASE3 + group_r;
            default: bias_addr = {BIAS_AW{1'b0}};
        endcase
    end

    // weight ROM row = grp_base(layer, group) + 원본 입력 인덱스
    wire [IN_AW-1:0] rom_ofs = comp_next ? wofs_q : i_next;
    assign weight_addr = grp_base_next + {{(ROM_AW - IN_AW) {1'b0}}, rom_ofs};

    assign layer   = layer_r;
    assign group   = group_r;
    assign feature_raddr = eff_idx;

    always @(posedge clk) begin
        if (!rst_n) begin
            state    <= S_IDLE;
            layer_r  <= 2'd0;
            group_r  <= 3'd0;
            i        <= {IN_AW{1'b0}};
            fill_cnt <= {IN_AW{1'b0}};
            grp_base <= {ROM_AW{1'b0}};
        end else begin
            state    <= state_next;
            layer_r  <= layer_next;
            group_r  <= group_next;
            i        <= i_next;
            fill_cnt <= fill_cnt_next;
            grp_base <= grp_base_next;
        end
    end

endmodule
