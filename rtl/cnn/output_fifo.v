`timescale 1ns / 1ps
// Output FIFO: sync BRAM FIFO, first-word fall-through (dout valid while !empty).

module output_fifo #(
    parameter WIDTH = 49,
    parameter DEPTH = 2048            // power of two
) (
    input  wire             clk,
    input  wire             rst_n,
    input  wire             push,
    input  wire [WIDTH-1:0] din,
    input  wire             rd_en,       // out_ready; pop = rd_en & !empty
    output wire [WIDTH-1:0] dout,
    output wire             empty
);

    localparam AW = $clog2(DEPTH);

    (* ram_style = "block" *)
    reg [WIDTH-1:0] mem[0:DEPTH-1];

    // registers: reg / reg_next
    reg [AW-1:0] wptr, wptr_next;
    reg [AW-1:0] rptr, rptr_next;
    reg [AW:0] count, count_next;  // 0..DEPTH
    reg [WIDTH-1:0] bram_q;  // BRAM read register
    reg byp_en, byp_en_next;  // head was written last cycle, not yet readable
    reg [WIDTH-1:0] byp_data, byp_data_next;

    wire full = count[AW];                     // count == DEPTH (DEPTH is a power of two)
    wire pop  = rd_en & ~empty;
    wire we   = push & (~full | pop);

    // ========== Output Logic ==========
    assign empty = (count == {(AW+1){1'b0}});
    assign dout  = byp_en ? byp_data : bram_q;

    // ========== Pointer / Count Logic ==========
    always @(*) begin
        wptr_next  = we  ? wptr + 1'b1 : wptr;
        rptr_next  = pop ? rptr + 1'b1 : rptr;
        count_next = count + {{AW{1'b0}}, we} - {{AW{1'b0}}, pop};

        // bypass when the next head is the slot being written this cycle
        byp_en_next   = we & (wptr == rptr_next);
        byp_data_next = din;
    end

    // BRAM: sync write, sync read of the next head
    always @(posedge clk) begin
        if (we) mem[wptr] <= din;
        bram_q <= mem[rptr_next];
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wptr     <= {AW{1'b0}};
            rptr     <= {AW{1'b0}};
            count    <= {(AW+1){1'b0}};
            byp_en   <= 1'b0;
            byp_data <= {WIDTH{1'b0}};
        end else begin
            wptr     <= wptr_next;
            rptr     <= rptr_next;
            count    <= count_next;
            byp_en   <= byp_en_next;
            byp_data <= byp_data_next;
        end
    end

endmodule
