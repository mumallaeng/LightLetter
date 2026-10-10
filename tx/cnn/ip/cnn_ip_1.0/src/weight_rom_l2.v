`timescale 1ns / 1ps

// conv2 weight ROM (Block RAM, 입력채널 bank 3개)
//   bank k : rom[{och, is_ch35}] = 144bit (입력채널 lane k 의 3x3 tap x INT16)
//   l2_weight_ic{k}.mem : 32 rows, row = och*2 + is_ch35
//                         (= rtl_ref/conv2_weight.mem 의 [144k +: 144])
//
// 동기 읽기: out_ch_sel 을 받은 다음 clk 에 weight_out 이 나온다
//   -> weight_addr_ctrl_l2 의 cal_valid 가 1clk 지연되어 이 출력과 맞물린다
//
// bank 별 읽기 enable (zero gating)
//   rom_en[k] = 0 이면 bank k 를 읽지 않고 출력 레지스터가 이전 값을 유지
//   rom_en 은 주소(out_ch_sel)와 같은 clk 에 들어와야 한다
module weight_rom_l2 #(
    parameter OCH = 16
) (
    input                        clk,
    input                        is_ch35,
    input      [$clog2(OCH)-1:0] out_ch_sel,
    input      [            2:0] rom_en,
    output     [          431:0] weight_out
);
    wire [$clog2(OCH):0] addr = {out_ch_sel, is_ch35};

    genvar k;
    generate
        for (k = 0; k < 3; k = k + 1) begin : g_bank
            in_ch_weight #(
                .DEPTH   (2 * OCH),
                // l2_weight_ic0.mem - l2_weight_ic2.mem (8'd48 = '0')
                .MEM_FILE({"l2_weight_ic", 8'd48 + k, ".mem"})
            ) U_IC_BANK (
                .clk       (clk),
                .en        (rom_en[k]),
                .addr      (addr),
                .weight_out(weight_out[k*144+:144])
            );
        end
    endgenerate
endmodule

module in_ch_weight #(
    parameter DEPTH    = 32,
    parameter MEM_FILE = "l2_weight_ic0.mem"
) (
    input                          clk,
    input                          en,
    input      [$clog2(DEPTH)-1:0] addr,
    output reg [            143:0] weight_out
);
    (* rom_style = "block" *) reg [143:0] rom[0:DEPTH-1];

    initial begin
        $readmemh(MEM_FILE, rom);
    end

    always @(posedge clk) begin
        if (en) weight_out <= rom[addr];
    end
endmodule
