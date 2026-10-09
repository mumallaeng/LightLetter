`timescale 1ns / 1ps

// FC zero gating (zero skipping)
//
// 각 FC 레이어는 같은 입력 벡터를 group 수만큼 반복해서 읽는다
// (FC1 400x6, FC2 120x5, FC3 84x2). 그래서 group 0 을 전수 스캔(learn)하면서
// feature 가 0 이 아닌 원소의 "원래 인덱스"만 모아 두면, group 1 부터는
// fc_ctrl 이 그 리스트만 돌면 된다 -> MAC 사이클이 비영 개수로 줄어든다.
//
//   0 x weight = 0 이므로 누산 결과는 비트 단위로 동일하다.
//
// FC1 -> FC2 -> FC3 는 순차 실행이라 인스턴스 하나를 clear 로 재사용한다.
//
// 읽기는 비동기(distributed RAM) 2 포트:
//   idx_cur : 이번 사이클 feature 주소용
//   idx_nxt : weight ROM(동기 읽기) 주소 prefetch 용
//
// idx_nxt / nz_cnt_nxt 는 "이번 사이클 적재분"을 조합으로 forwarding 한다.
// group 0 의 마지막 원소가 그 벡터의 첫 비영인 경우, group 1 의 첫 주소를
// prefetch 하는 시점에 아직 메모리에 안 들어가 있기 때문이다.
// (idx_cur / nz_cnt 는 forwarding 하지 않는다. feature -> append -> idx_cur ->
//  feature_raddr -> feature 로 조합 루프가 생긴다.)
module fc_zero_gating #(
    parameter DEPTH = 400,
    parameter AW    = $clog2(DEPTH)
) (
    input               clk,
    input               rst_n,
    input               clear,       // 새 레이어 시작: 리스트 비움
    input               learn,       // group 0 (전수 스캔) 중
    input               valid,       // = mac_en
    input      [  15:0] feature,     // 이번 사이클의 feature
    input      [AW-1:0] src_idx,     // learn 중 현재 원본 인덱스 (= fc_ctrl 의 i)
    input      [AW-1:0] k_cur,
    input      [AW-1:0] k_nxt,
    output     [AW-1:0] idx_cur,
    output     [AW-1:0] idx_nxt,
    output     [  AW:0] nz_cnt,      // 적재된 비영 개수 (레지스터, 적재보다 1 clk 늦게 확정)
    output     [AW-1:0] zero_idx     // 0 인 것이 확실한 원소의 인덱스 (패딩용)
);

    (* ram_style = "distributed" *)
    reg [AW-1:0] idx_mem[0:DEPTH-1];

    reg [AW:0] wr_ptr;

    // feature 는 feature buffer(LUTRAM) 를 거쳐 오므로
    //   i -> feature buffer -> |feature| -> idx_mem WE
    // 가 하나의 조합 경로가 되어 timing 이 깨진다. 판정을 레지스터로 받아 끊는다.
    // (적재가 1 clk 늦어지므로 fc_ctrl 이 그룹 전환에서 2 clk 쉬어 준다)
    reg          app_q, zero_q;
    reg [AW-1:0] src_q;

    always @(posedge clk) begin
        if (!rst_n) begin
            app_q  <= 1'b0;
            zero_q <= 1'b0;
            src_q  <= {AW{1'b0}};
        end else begin
            app_q  <= learn & valid &  (|feature);
            zero_q <= learn & valid & ~(|feature);
            src_q  <= src_idx;
        end
    end

    always @(posedge clk) begin
        if (!rst_n || clear) wr_ptr <= {(AW + 1) {1'b0}};
        else if (app_q)      wr_ptr <= wr_ptr + 1'b1;
    end

    always @(posedge clk) begin
        if (app_q) idx_mem[wr_ptr[AW-1:0]] <= src_q;
    end

    // 패딩 사이클용: 처음 만난 "0 인 feature" 의 인덱스를 기억해 둔다.
    // fc_mac 의 bias 파이프라인이 그룹 길이 >= 2 를 요구하는데, 비영이 0~1 개면
    // 길이를 2 로 늘려야 한다. 그때 남는 사이클이 이 인덱스를 읽어 0 을 곱한다.
    reg [AW-1:0] zero_idx_r;
    reg          has_zero;

    always @(posedge clk) begin
        if (!rst_n || clear) begin
            has_zero   <= 1'b0;
            zero_idx_r <= {AW{1'b0}};
        end else if (zero_q & ~has_zero) begin
            has_zero   <= 1'b1;
            zero_idx_r <= src_q;
        end
    end

    assign zero_idx = zero_idx_r;

    // idx 읽기에는 forwarding 을 쓰지 않는다. feature -> append -> idx_nxt -> weight ROM
    // 주소로 이어지면 LUTRAM 2 단이 직렬로 걸려 timing 이 깨진다.
    // 대신 fc_ctrl 이 group 0 끝에서 nz_cnt_nxt 를 레지스터에 래치하고,
    // 압축 그룹의 첫 사이클은 한 박자 쉬면서 weight 를 prefetch 한다.
    assign idx_cur = idx_mem[k_cur];
    assign idx_nxt = idx_mem[k_nxt];
    assign nz_cnt  = wr_ptr;

endmodule
