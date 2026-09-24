`timescale 1ns / 1ps

module max_logic #(
    parameter IF_W   = 26,
    parameter ADDR_W = $clog2(IF_W / 2)
) (
    input               clk,
    input               rst_n,
    input               prev_we,
    input  [      15:0] pool_in,
    input  [ADDR_W-1:0] pool_addr,
    input               pool_we,
    output [      15:0] pool_data
);
    // max logic
    reg [15:0] prev_reg, prev_next;
    wire [15:0] pair, pool_rdata;

    assign pair = (pool_in >= prev_reg) ? pool_in : prev_reg;

    // Previous data logic
    always @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            prev_reg <= 0;
        end else begin
            prev_reg <= prev_next;
        end
    end

    always @(*) begin
        prev_next = prev_we ? pool_in : prev_reg;
    end

    // pooling buffer
    // - store max data
    pool_buf #(
        .IF_W(IF_W)
    ) U_POOL_BUF (
        .clk       (clk),
        .pool_wdata(pair),
        .pool_addr (pool_addr),
        .pool_we   (pool_we),
        .pool_rdata(pool_rdata)
    );

    // Output Logic
    assign pool_data = (pool_rdata >= pair) ? pool_rdata : pair;
endmodule

module pool_buf #(
    parameter IF_W     = 26,
    parameter MEM_SIZE = IF_W / 2,
    parameter ADDR_W   = $clog2(MEM_SIZE)
) (
    input               clk,
    input  [      15:0] pool_wdata,
    input  [ADDR_W-1:0] pool_addr,
    input               pool_we,
    output [      15:0] pool_rdata
);
    // lutram
    reg [15:0] ram[0:MEM_SIZE-1];

    always @(posedge clk) begin
        if (pool_we) begin
            ram[pool_addr] <= pool_wdata;
        end
    end

    assign pool_rdata = ram[pool_addr];
endmodule
