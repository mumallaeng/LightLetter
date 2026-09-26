`timescale 1ns / 1ps
// Chunk (outer) / neuron (inner) counters: one mac_en per clock while a calc buffer is full.
// rom_addr leads by one - it addresses the next mac_en,
// so the registered fc_weight_rom output arrives with the value it belongs to.
// chunk_done marks the last neuron of a chunk and hands the calc buffer back to fc_staging.

module fc_ctrl #(
    parameter N_OUT     = 120,  // FC1=120; FC2=84; FC3=36
    parameter NUM_CHUNK = 16    // FC1=16; FC2=12; FC3=17
) (
    input  wire                               clk,
    input  wire                               rst_n,
    input  wire                               calc_full,
    input  wire                               next_full,
    output reg                                mac_en,
    output reg  [$clog2(NUM_CHUNK*N_OUT)-1:0] rom_addr,
    output reg                                chunk_done
);

    localparam NEU_AW = (N_OUT > 1) ? $clog2(N_OUT) : 1;
    localparam CHK_AW = (NUM_CHUNK > 1) ? $clog2(NUM_CHUNK) : 1;
    localparam ROM_AW = $clog2(NUM_CHUNK * N_OUT);
    localparam [31:0] CHUNK_STEP = N_OUT;

    // 32-bit constants, sliced at the use sites so the compares keep the counter width
    localparam [31:0] NEURON_LAST = N_OUT - 1;
    localparam [31:0] CHUNK_LAST = NUM_CHUNK - 1;

    localparam FC_IDLE = 1'b0, FC_RUN = 1'b1;

    // registers: reg / reg_next
    reg state, state_next;
    reg [NEU_AW-1:0] neuron, neuron_next;
    reg [CHK_AW-1:0] chunk, chunk_next;
    reg [ROM_AW-1:0] chunk_base, chunk_base_next;  // = chunk * N_OUT, kept without a multiplier

    wire              run = (state == FC_RUN);
    wire              neuron_last = (neuron == NEURON_LAST[NEU_AW-1:0]);
    wire              chunk_wrap = (chunk == CHUNK_LAST[CHK_AW-1:0]);

    wire [CHK_AW-1:0] next_chunk = chunk_wrap ? {CHK_AW{1'b0}} : chunk + 1'b1;
    wire [ROM_AW-1:0] next_base = chunk_wrap ? {ROM_AW{1'b0}} : chunk_base + CHUNK_STEP[ROM_AW-1:0];

    // neuron of the next mac_en: the one after this, or 0 when this chunk ends
    wire [NEU_AW-1:0] addr_neuron = (run & ~neuron_last) ? neuron + 1'b1 : {NEU_AW{1'b0}};

    // ========== Next State / Counter Logic ==========
    always @(*) begin : fc_ctrl_comb
        state_next      = state;
        neuron_next     = neuron;
        chunk_next      = chunk;
        chunk_base_next = chunk_base;

        if (state == FC_IDLE) begin
            if (calc_full) begin
                state_next  = FC_RUN;
                neuron_next = {NEU_AW{1'b0}};
            end
        end else if (neuron_last) begin
            neuron_next     = {NEU_AW{1'b0}};
            chunk_next      = next_chunk;
            chunk_base_next = next_base;
            // keep running when the next chunk is already in the fill buffer
            state_next      = next_full ? FC_RUN : FC_IDLE;
        end else begin
            neuron_next = neuron + 1'b1;
        end
    end

    // ========== Output Logic ==========
    always @(*) begin : fc_ctrl_out
        mac_en     = run;
        chunk_done = run & neuron_last;
        rom_addr   = (run & neuron_last) ? next_base : chunk_base + {{(ROM_AW - NEU_AW) {1'b0}}, addr_neuron};
    end

    always @(posedge clk) begin : fc_ctrl_seq
        if (!rst_n) begin
            state      <= FC_IDLE;
            neuron     <= {NEU_AW{1'b0}};
            chunk      <= {CHK_AW{1'b0}};
            chunk_base <= {ROM_AW{1'b0}};
        end else begin
            state      <= state_next;
            neuron     <= neuron_next;
            chunk      <= chunk_next;
            chunk_base <= chunk_base_next;
        end
    end

endmodule


