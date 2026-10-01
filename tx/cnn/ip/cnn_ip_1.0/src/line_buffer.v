`timescale 1ns / 1ps

module line_buffer #(
    parameter IMG_WIDTH = 28   
)(
    input  wire         clk,
    input  wire         rst_n,          
    input  wire [15:0]  pixel_in,       
    input  wire         wr_en,          
    input  wire         phase_clear,    
    output reg  [143:0] win_out,        
    output reg          win_valid       
);

// ---------------------------------------------------------------
// [저장소] 물리적으로 고정된 3개의 "그릇" 레지스터.
//
// row0_buf라는 이름이 항상 "논리적으로 제일 오래된 줄"을
// 의미하는 것은 아님.
//
// 3개의 버퍼를 원형으로 돌려 쓰며,
// 현재 어느 버퍼가 어떤 논리적 행 역할을 하는지는 cur_row를
// 기준으로 아래 row*_sel에서 결정함.
//
// 1픽셀 = 16bit
// IMG_WIDTH=28 → 28*16 = 448bit
// ---------------------------------------------------------------
reg [IMG_WIDTH*16-1:0] row0_buf;
reg [IMG_WIDTH*16-1:0] row1_buf;
reg [IMG_WIDTH*16-1:0] row2_buf;

reg [1:0] cur_row;        

reg [1:0] rows_started;   

reg [$clog2(IMG_WIDTH)-1:0] write_col;  

reg [IMG_WIDTH*16-1:0] row0_sel;
reg [IMG_WIDTH*16-1:0] row1_sel;
reg [IMG_WIDTH*16-1:0] row2_sel;


always @(*) begin
    case (cur_row)
        2'd0: begin
            row0_sel = row1_buf;
            row1_sel = row2_buf;
            row2_sel = row0_buf;
        end
        2'd1: begin
            row0_sel = row2_buf;
            row1_sel = row0_buf;
            row2_sel = row1_buf;
        end
        default: begin
            row0_sel = row0_buf;
            row1_sel = row1_buf;
            row2_sel = row2_buf;
        end
    endcase
end


// ---------------------------------------------------------------
// [2단계: "이번 클록에 쓴다면 나올 값" 미리 계산]
//
// 조건:
//   rows_started == 2
//       → 최소 3개의 행이 준비됨
//
//   write_col >= 2
//       → 현재 픽셀을 포함하여 가로 3칸 확보
//
// +: 연산자
//   base +: width  = [base+width-1 : base]
// ---------------------------------------------------------------

reg [143:0] win_out_fresh;
reg         win_valid_fresh;

always @(*) begin
    win_valid_fresh = 1'b0;
    win_out_fresh   = 144'd0;

    // 3x3 window를 만들 수 있는 경우
    if (rows_started == 2'd2 && write_col >= 2) begin

        // -------------------------------------------------------
        // 논리적 row0
        // -------------------------------------------------------
        win_out_fresh[15:0]    = row0_sel[(write_col-2)*16 +: 16];
        win_out_fresh[31:16]   = row0_sel[(write_col-1)*16 +: 16];
        win_out_fresh[47:32]   = row0_sel[(write_col  )*16 +: 16];

        // -------------------------------------------------------
        // 논리적 row1
        // -------------------------------------------------------
        win_out_fresh[63:48]   = row1_sel[(write_col-2)*16 +: 16];
        win_out_fresh[79:64]   = row1_sel[(write_col-1)*16 +: 16];
        win_out_fresh[95:80]   = row1_sel[(write_col  )*16 +: 16];

        // -------------------------------------------------------
        // 논리적 row2
        // -------------------------------------------------------
        win_out_fresh[111:96]  = row2_sel[(write_col-2)*16 +: 16];
        win_out_fresh[127:112] = row2_sel[(write_col-1)*16 +: 16];
        win_out_fresh[143:128] = pixel_in;

        win_valid_fresh = 1'b1;
    end
end


// ---------------------------------------------------------------
// [3단계: 진짜 레지스터 갱신]
//
// win_out / win_valid를 포함한 상태 레지스터는 여기서만 갱신.
//
// rst_n=0 또는 phase_clear=1:
//   → 상태 초기화
//
// wr_en=1:
//   → 이번 클록에 계산된 win_out_fresh를 저장
//
// wr_en=0:
//   → 새로운 Window가 없으므로 win_valid를 0으로 만듦
//   → win_out은 이전 값 유지
// ---------------------------------------------------------------

always @(posedge clk or negedge rst_n) begin

    if (!rst_n || phase_clear) begin
        cur_row      <= 2'd0;
        rows_started <= 2'd0;
        write_col    <= {$clog2(IMG_WIDTH){1'b0}};

        row0_buf     <= {(IMG_WIDTH*16){1'b0}};
        row1_buf     <= {(IMG_WIDTH*16){1'b0}};
        row2_buf     <= {(IMG_WIDTH*16){1'b0}};

        win_out      <= 144'd0;
        win_valid    <= 1'b0;
    end

    else if (wr_en) begin
        win_out   <= win_out_fresh;
        win_valid <= win_valid_fresh;

        case (cur_row)
            2'd0:
                row0_buf[write_col*16 +: 16] <= pixel_in;
            2'd1:
                row1_buf[write_col*16 +: 16] <= pixel_in;
            default:
                row2_buf[write_col*16 +: 16] <= pixel_in;
        endcase


        // -------------------------------------------------------
        // 현재 줄의 마지막 픽셀인지 확인
        // -------------------------------------------------------
        if (write_col == IMG_WIDTH-1) begin

            // 다음 줄의 첫 번째 픽셀부터 다시 시작
            write_col <= {$clog2(IMG_WIDTH){1'b0}};

            // 물리 버퍼 순환 (0 → 1 → 2 → 0)
            cur_row <= (cur_row == 2'd2) ? 2'd0 : cur_row + 2'd1;

            // warm-up 카운터
            // 0 → 1 → 2에서 포화
            if (rows_started != 2'd2)
                rows_started <= rows_started + 2'd1;
        end else begin

            // 아직 현재 줄이 끝나지 않았으면
            // 다음 column으로 이동
            write_col <= write_col + 1'b1;

        end
    end else begin
        // 새로운 window가 없으면 valid를 0으로 만듦
        win_valid <= 1'b0;
    end
end

endmodule