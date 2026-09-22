`timescale 1ns / 1ps
// Output reorder buffer: BRAM holding one layer's packed entries, written in arrival order
// (pixel-major) and read group-major: for each group of PACK channels, every pixel.
//   conv1 (2 groups) : channels 0-2 for all pixels, then channels 3-5
//   conv2 (16 groups): channel 0 for all pixels, ..., channel 15
// First-word fall-through: dout is valid while avail.

module out_reorder #(
    parameter WIDTH  = 48,
    parameter N      = 121,           // pixels per frame
    parameter GROUPS = 16             // C_OUT / PACK
) (
    input  wire             clk,
    input  wire             rst_n,
    input  wire             push,
    input  wire [WIDTH-1:0] din,
    input  wire             rd_en,       // out_ready; pop = rd_en & avail
    output wire [WIDTH-1:0] dout,
    output wire             avail,       // entry at the read address has been written
    output wire             last_pixel   // read address is at pixel N-1
);

    localparam DEPTH  = N * GROUPS;
    localparam AW     = $clog2(DEPTH + 1);
    localparam PIX_AW = (N > 1) ? $clog2(N) : 1;
    localparam GRP_AW = (GROUPS > 1) ? $clog2(GROUPS) : 1;

    (* ram_style = "block" *)
    reg [WIDTH-1:0] mem [0:DEPTH-1];

    // registers: reg / reg_next
    reg [AW-1:0]     wr_cnt,   wr_cnt_next;     // entries written this frame; also the write address
    reg [GRP_AW-1:0] rd_grp,   rd_grp_next;
    reg [PIX_AW-1:0] rd_pix,   rd_pix_next;
    reg [AW-1:0]     rd_addr,  rd_addr_next;    // = rd_pix * GROUPS + rd_grp
    reg [WIDTH-1:0]  bram_q;                    // BRAM read register
    reg              byp_en,   byp_en_next;     // slot was written last cycle, not yet readable
    reg [WIDTH-1:0]  byp_data, byp_data_next;

    wire pop        = rd_en & avail;
    wire pix_last   = (rd_pix == $unsigned(N - 1));
    wire grp_last   = (rd_grp == $unsigned(GROUPS - 1));
    wire rd_done    = pop & pix_last & grp_last;                    // last entry of the frame leaves
    wire frame_full = (wr_cnt == $unsigned(DEPTH));                 // slots are released when the frame is read out
    wire we         = push & ~frame_full;

    // ========== Output Logic ==========
    assign avail      = (wr_cnt > rd_addr);
    assign last_pixel = pix_last;
    assign dout       = byp_en ? byp_data : bram_q;

    // ========== Read Pointer Logic ==========
    always @(*) begin
        rd_grp_next  = rd_grp;
        rd_pix_next  = rd_pix;
        rd_addr_next = rd_addr;
        if (pop) begin
            if (pix_last) begin
                rd_pix_next  = {PIX_AW{1'b0}};
                rd_grp_next  = grp_last ? {GRP_AW{1'b0}} : rd_grp + 1'b1;
                rd_addr_next = grp_last ? {AW{1'b0}} : {{(AW-GRP_AW){1'b0}}, rd_grp} + 1'b1;   // pixel 0 of the next group
            end else begin
                rd_pix_next  = rd_pix + 1'b1;
                rd_addr_next = rd_addr + $unsigned(GROUPS);
            end
        end
    end

    // ========== Write Logic ==========
    always @(*) begin
        wr_cnt_next   = rd_done ? {AW{1'b0}} : (we ? wr_cnt + 1'b1 : wr_cnt);
        // bypass when the next read address is the slot being written this cycle
        byp_en_next   = we & (wr_cnt == rd_addr_next);
        byp_data_next = din;
    end

    // BRAM: sync write, sync read of the next address
    always @(posedge clk) begin
        if (we)
            mem[wr_cnt] <= din;
        bram_q <= mem[rd_addr_next];
    end

    always @(posedge clk) begin
        if (!rst_n) begin
            wr_cnt   <= {AW{1'b0}};
            rd_grp   <= {GRP_AW{1'b0}};
            rd_pix   <= {PIX_AW{1'b0}};
            rd_addr  <= {AW{1'b0}};
            byp_en   <= 1'b0;
            byp_data <= {WIDTH{1'b0}};
        end else begin
            wr_cnt   <= wr_cnt_next;
            rd_grp   <= rd_grp_next;
            rd_pix   <= rd_pix_next;
            rd_addr  <= rd_addr_next;
            byp_en   <= byp_en_next;
            byp_data <= byp_data_next;
        end
    end

endmodule
