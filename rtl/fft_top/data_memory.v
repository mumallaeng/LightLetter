`timescale 1ns / 1ps

module data_memory #(
    parameter FFT_W = 40
) (
    input  wire               clk,
    input  wire               rst,
    input  wire               w_en,
    input  wire [FFT_W - 1:0] i_wdata_0,
    input  wire [FFT_W - 1:0] i_wdata_1,
    input  wire [   13:0]     i_waddr,
    input  wire [   13:0]     i_raddr,
    output reg  [FFT_W - 1:0] rdata_a,
    output reg  [FFT_W - 1:0] rdata_b
);

    reg [FFT_W - 1:0] data_ram[0:127];

    always @(posedge clk) begin
        if (rst) begin
            rdata_a <= 0;
        end else if (w_en) begin
            data_ram[i_waddr[13:7]] <= i_wdata_0;
        end else begin
            rdata_a <= data_ram[i_raddr[13:7]];
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            rdata_b <= 0;
        end else if (w_en) begin
            data_ram[i_waddr[6:0]] <= i_wdata_1;
        end else begin
            rdata_b <= data_ram[i_raddr[6:0]];
        end
    end

endmodule
