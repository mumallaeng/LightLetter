`timescale 1ns / 1ps

// cnn_top 에 test/ 의 실제 EMNIST 이미지를 한 장씩 넣고, cnn_result 가 폴더 label 과 같은지 본다.
//
// 실행: 이 폴더에서  ./run_sim.sh img [N_PER_CLASS]   (gen_img_vectors.py 가 build/ 에 아래 파일을 만든다)
//
//   img_stim.mem    16bit x N_IMG*784  pixel_in = round(p / 255 * 2^14), 28x28 raster
//   img_label.mem    8bit x N_IMG      정답 (test/<label>/ 폴더 번호, 0-9 A-Z)
//   img_class.mem    8bit x N_IMG      bit-exact 정수 모델 (gen_img_vectors.py) 의 class
//   img_logit.mem   16bit x N_IMG*36   정수 모델의 logit
//   img_list.txt                        index label 글자 파일명
//
// 판정
//   PASS / FAIL : cnn_result == label  (모델 정확도 문제라 all pass 일 필요는 없다)
//   RTL==MODEL  : cnn_result 와 36 개 logit 이 정수 모델과 같은지 (다르면 RTL 문제)
//
// 입력 방식: 이미지 사이 리셋 없이, 다음 이미지는 앞 이미지의 cnn_done 뒤에 넣는다 (버튼 한 번에 한 장).

module tb_cnn_top_img;

    parameter REPORT_FILE = "tb_cnn_top_img_report.txt";
    parameter N_IMG      = 70;
    parameter VALID_PCT  = 100;
    parameter SEED       = 1;
    parameter MAX_CYCLES = 40000 * N_IMG;

    localparam NUM_CLASS = 36;
    localparam N_PIX     = 28 * 28;

    // ---------------- DUT ----------------
    reg         clk;
    reg         rst_n;
    reg  [15:0] s_axis_tdata;
    reg         s_axis_tvalid;
    wire        s_axis_tready;
    reg         s_axis_tuser;
    reg         s_axis_tlast;
    wire [$clog2(NUM_CLASS)-1:0] cnn_result;
    wire        cnn_done;

    cnn_top #(
        .NUM_CLASS(NUM_CLASS)
    ) dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_axis_tdata (s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .s_axis_tuser (s_axis_tuser),
        .s_axis_tlast (s_axis_tlast),
        .cnn_result   (cnn_result),
        .cnn_done     (cnn_done)
    );

    always #5 clk = ~clk;                                 // 100 MHz

    // ---------------- vectors ----------------
    reg [15:0] stim     [0:N_IMG*N_PIX-1];
    reg [ 7:0] label    [0:N_IMG-1];
    reg [ 7:0] mdl_cls  [0:N_IMG-1];
    reg [15:0] mdl_lg   [0:N_IMG*NUM_CLASS-1];
    reg [8*40-1:0] name [0:N_IMG-1];

    reg [15:0] got_lg   [0:N_IMG*NUM_CLASS-1];
    reg [ 7:0] got_cls  [0:N_IMG-1];
    integer    done_cyc [0:N_IMG-1];

    integer pix, n_lg, n_done, cyc, seed, i, k;
    integer n_pass, n_same, lg_err;
    reg     running;
    reg [31:0] roll_v;

    function [7:0] cls_chr;                               // 0-9 A-Z
        input [7:0] c;
        cls_chr = (c < 10) ? ("0" + c) : ("A" + c - 10);
    endfunction

    function lg_same;                                     // 이미지 n 의 logit 36 개가 모델과 같은지 (x 는 불일치)
        input integer n;
        integer j;
        begin
            lg_same = 1'b1;
            for (j = 0; j < NUM_CLASS; j = j + 1)
                if (got_lg[n*NUM_CLASS+j] !== mdl_lg[n*NUM_CLASS+j] || ^got_lg[n*NUM_CLASS+j] === 1'bx)
                    lg_same = 1'b0;
        end
    endfunction

    // ---------------- AXIS source ----------------
    wire pix_fire = s_axis_tvalid && s_axis_tready;

    always @(*) begin
        s_axis_tdata = (pix < N_IMG * N_PIX) ? stim[pix] : 16'd0;
        s_axis_tuser = (pix % N_PIX == 0);
        s_axis_tlast = (pix % N_PIX == N_PIX - 1);
    end

    // 이미지 n 은 cnn_done 이 n 번 나온 뒤에 넣는다
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pix           <= 0;
            s_axis_tvalid <= 1'b0;
        end else if (running) begin
            roll_v = $unsigned($random(seed)) % 100;
            if (pix_fire) pix <= pix + 1;
            if (!s_axis_tvalid || s_axis_tready)
                s_axis_tvalid <= ((pix + pix_fire) < N_IMG * N_PIX) &&
                                 ((pix + pix_fire) / N_PIX <= n_done) && (roll_v < VALID_PCT);
        end
    end

    always @(posedge clk) if (rst_n) cyc = cyc + 1;

    // ---------------- logit / result capture ----------------
    always @(posedge clk) begin
        if (rst_n && dut.logit_valid && dut.logit_ready) begin
            if (n_lg < N_IMG * NUM_CLASS) got_lg[n_lg] = dut.logit_data;
            n_lg = n_lg + 1;
        end
    end

    always @(posedge clk) begin
        if (rst_n && cnn_done) begin
            if (n_done < N_IMG) begin
                got_cls[n_done]  = cnn_result;
                done_cyc[n_done] = cyc;
                if (cnn_result === label[n_done]) n_pass = n_pass + 1;
                if (cnn_result === mdl_cls[n_done] && lg_same(n_done)) n_same = n_same + 1;
                else lg_err = lg_err + 1;
                $display("  img %3d  label %2d (%s)  rtl %2d (%s)  model %2d  %-4s  %0s  %0s", n_done,
                         label[n_done], cls_chr(label[n_done]), cnn_result, cls_chr(cnn_result), mdl_cls[n_done],
                         (cnn_result === label[n_done]) ? "PASS" : "FAIL",
                         (cnn_result === mdl_cls[n_done] && lg_same(n_done)) ? "rtl==model" : "RTL!=MODEL",
                         name[n_done]);
            end
            n_done = n_done + 1;
        end
    end

    // ---------------- report ----------------
    task write_report;
        integer fd, n, j, mx;
        begin
            fd = $fopen(REPORT_FILE, "w");
            $fdisplay(fd, "cnn_top image test  (N_IMG %0d, VALID_PCT %0d, SEED %0d)", N_IMG, VALID_PCT, SEED);
            $fdisplay(fd, "  PASS/FAIL  : cnn_result == label (folder)");
            $fdisplay(fd, "  rtl==model : cnn_result and all 36 logits equal the bit-exact integer model");
            $fdisplay(fd, "");
            $fdisplay(fd, "  img  label  rtl  model  logit[rtl]  logit[label]  cycle     result  rtl==model  file");
            for (n = 0; n < N_IMG; n = n + 1) begin
                if (n >= n_done) begin
                    $fdisplay(fd, "  %3d  %2d %s   --    %2d     no cnn_done", n, label[n], cls_chr(label[n]), mdl_cls[n]);
                end else begin
                    $fdisplay(fd, "  %3d  %2d %s  %2d %s  %2d %s  %10d  %12d  %8d  %-6s  %-10s  %0s", n,
                              label[n], cls_chr(label[n]), got_cls[n], cls_chr(got_cls[n]),
                              mdl_cls[n], cls_chr(mdl_cls[n]),
                              $signed(got_lg[n*NUM_CLASS+got_cls[n]]), $signed(got_lg[n*NUM_CLASS+label[n]]),
                              done_cyc[n], (got_cls[n] === label[n]) ? "PASS" : "FAIL",
                              (got_cls[n] === mdl_cls[n] && lg_same(n)) ? "yes" : "NO", name[n]);
                end
            end
            $fdisplay(fd, "");
            $fdisplay(fd, "  per class (label: pass / images)");
            for (j = 0; j < NUM_CLASS; j = j + 1) begin
                mx = 0; k = 0;
                for (n = 0; n < n_done && n < N_IMG; n = n + 1)
                    if (label[n] == j) begin
                        mx = mx + 1;
                        if (got_cls[n] === label[n]) k = k + 1;
                    end
                if (mx > 0) $fdisplay(fd, "    %2d %s : %0d / %0d", j, cls_chr(j), k, mx);
            end
            $fdisplay(fd, "");
            $fdisplay(fd, "  accuracy   : %0d / %0d", n_pass, N_IMG);
            $fdisplay(fd, "  rtl==model : %0d / %0d", n_same, N_IMG);
            $fclose(fd);
        end
    endtask

    // ---------------- main ----------------
    integer fl, r, idx, lab;
    reg [7:0]      ch;
    reg [8*40-1:0] fname;

    initial begin
        clk = 0; rst_n = 0; running = 0;
        pix = 0; n_lg = 0; n_done = 0; cyc = 0; seed = SEED;
        n_pass = 0; n_same = 0; lg_err = 0;

        for (i = 0; i < N_IMG; i = i + 1) label[i] = 8'hxx;
        $readmemh("img_stim.mem",  stim);
        $readmemh("img_label.mem", label);
        $readmemh("img_class.mem", mdl_cls);
        $readmemh("img_logit.mem", mdl_lg);
        for (i = 0; i < N_IMG; i = i + 1) name[i] = "";
        fl = $fopen("img_list.txt", "r");
        if (fl != 0) begin
            for (i = 0; i < N_IMG; i = i + 1) begin
                r = $fscanf(fl, "%d %d %s %s\n", idx, lab, ch, fname);
                if (r == 4 && idx < N_IMG) name[idx] = fname;
            end
            $fclose(fl);
        end
        if (^stim[0] === 1'bx || ^label[N_IMG-1] === 1'bx || ^mdl_lg[0] === 1'bx) begin
            $display("[FAIL] img_*.mem not loaded (or N_IMG larger than the vectors) - run ./run_sim.sh img");
            $finish;
        end

        $display("cnn_top image test: %0d images, VALID_PCT %0d", N_IMG, VALID_PCT);
        repeat (5) @(posedge clk);
        rst_n = 1;
        @(posedge clk);
        running = 1;

        while (n_done < N_IMG && cyc < MAX_CYCLES) @(posedge clk);
        repeat (20) @(posedge clk);
        running = 0;

        write_report;
        $display("");
        $display("  accuracy   : %0d / %0d  (cnn_result == label)", n_pass, N_IMG);
        $display("  rtl==model : %0d / %0d  (cnn_result + 36 logits == bit-exact integer model)", n_same, N_IMG);
        if (n_done < N_IMG)
            $display("[FAIL] cnn_top img : only %0d / %0d cnn_done in %0d cycles", n_done, N_IMG, cyc);
        else if (n_same != N_IMG || n_lg != N_IMG * NUM_CLASS)
            $display("[FAIL] cnn_top img : RTL differs from the integer model (%0d images, %0d logits)", lg_err, n_lg);
        else
            $display("[PASS] cnn_top img : RTL == model on all %0d images, accuracy %0d / %0d", N_IMG, n_pass, N_IMG);
        $finish;
    end

endmodule
