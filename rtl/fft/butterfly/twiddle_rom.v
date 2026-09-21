`timescale 1ns / 1ps


module twiddle_rom #(
    parameter ADDR_WIDTH =6,
    parameter COEFF_WIDTH =16,
    parameter MEM_FILE = "twiddle_128_q14.mem"
)(
    input wire                     clk,
    input wire [ADDR_WIDTH-1:0]    twiddle_addr,
    output reg [2*COEFF_WIDTH-1:0] twiddle_factor
);
// rom save k 2^6

localparam ROM_DEPTH =2 **ADDR_WIDTH;

// 실수부와 허수부를 묶어서 저장
reg [2*COEFF_WIDTH-1:0] rom[0:ROM_DEPTH-1];

initial begin
    $readmemh(MEM_FILE, rom);
    end

always @(posedge clk) begin
    twiddle_factor <= rom[twiddle_addr];

end
endmodule
