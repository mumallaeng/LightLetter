`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: axi_lite_tx_wrapper
//
// 설명:
//   Zynq PS의 AXI4-Lite Register 접근을 기존 optical_tx_top의
//   char_id / char_valid / char_ready / tx_busy 인터페이스로 변환한다.
//
// Register Map:
//   0x00 TX_DATA   R/W : [7:0] Character ID
//   0x04 TX_CTRL   W   : [0] START
//   0x08 TX_STATUS R   : [0] READY, [1] BUSY
//
// START 정책:
//   char_ready=1일 때만 char_valid를 1 Clock Pulse로 발생시킨다.
//   char_ready=0이면 START 요청은 무시한다.
//////////////////////////////////////////////////////////////////////////////////

module axi_lite_tx_wrapper #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 4
)(
    input  wire                              s_axi_aclk,
    input  wire                              s_axi_aresetn,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_awaddr,
    input  wire                              s_axi_awvalid,
    output wire                              s_axi_awready,

    input  wire [C_S_AXI_DATA_WIDTH-1:0]     s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                              s_axi_wvalid,
    output wire                              s_axi_wready,

    output reg  [1:0]                        s_axi_bresp,
    output reg                               s_axi_bvalid,
    input  wire                              s_axi_bready,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_araddr,
    input  wire                              s_axi_arvalid,
    output wire                              s_axi_arready,

    output reg  [C_S_AXI_DATA_WIDTH-1:0]     s_axi_rdata,
    output reg  [1:0]                        s_axi_rresp,
    output reg                               s_axi_rvalid,
    input  wire                              s_axi_rready,

    output reg  [7:0]                        char_id,
    output reg                               char_valid,
    input  wire                              char_ready,
    input  wire                              tx_busy
);

    localparam [C_S_AXI_ADDR_WIDTH-1:0] ADDR_TX_DATA   = 4'h0;
    localparam [C_S_AXI_ADDR_WIDTH-1:0] ADDR_TX_CTRL   = 4'h4;
    localparam [C_S_AXI_ADDR_WIDTH-1:0] ADDR_TX_STATUS = 4'h8;

    localparam [1:0] AXI_RESP_OKAY = 2'b00;

    reg [C_S_AXI_ADDR_WIDTH-1:0]     awaddr_reg;
    reg                              aw_pending;
    reg [C_S_AXI_DATA_WIDTH-1:0]     wdata_reg;
    reg [(C_S_AXI_DATA_WIDTH/8)-1:0] wstrb_reg;
    reg                              w_pending;

    assign s_axi_awready = ~aw_pending && ~s_axi_bvalid;
    assign s_axi_wready  = ~w_pending  && ~s_axi_bvalid;
    assign s_axi_arready = ~s_axi_rvalid;

    // ============================================================
    // AXI Write Channel
    // ============================================================
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            awaddr_reg   <= {C_S_AXI_ADDR_WIDTH{1'b0}};
            aw_pending   <= 1'b0;
            wdata_reg    <= {C_S_AXI_DATA_WIDTH{1'b0}};
            wstrb_reg    <= {(C_S_AXI_DATA_WIDTH/8){1'b0}};
            w_pending    <= 1'b0;
            s_axi_bresp  <= AXI_RESP_OKAY;
            s_axi_bvalid <= 1'b0;
            char_id      <= 8'h00;
            char_valid   <= 1'b0;
        end else begin
            char_valid <= 1'b0;

            if (s_axi_awvalid && s_axi_awready) begin
                awaddr_reg <= s_axi_awaddr;
                aw_pending <= 1'b1;
            end

            if (s_axi_wvalid && s_axi_wready) begin
                wdata_reg <= s_axi_wdata;
                wstrb_reg <= s_axi_wstrb;
                w_pending <= 1'b1;
            end

            if (aw_pending && w_pending && !s_axi_bvalid) begin
                case (awaddr_reg)
                    ADDR_TX_DATA: begin
                        if (wstrb_reg[0])
                            char_id <= wdata_reg[7:0];
                    end

                    ADDR_TX_CTRL: begin
                        if (wstrb_reg[0] && wdata_reg[0] && char_ready)
                            char_valid <= 1'b1;
                    end

                    default: begin
                    end
                endcase

                s_axi_bresp  <= AXI_RESP_OKAY;
                s_axi_bvalid <= 1'b1;
                aw_pending   <= 1'b0;
                w_pending    <= 1'b0;
            end

            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
        end
    end

    // ============================================================
    // AXI Read Channel
    // ============================================================
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            s_axi_rdata  <= {C_S_AXI_DATA_WIDTH{1'b0}};
            s_axi_rresp  <= AXI_RESP_OKAY;
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_arvalid && s_axi_arready) begin
                case (s_axi_araddr)
                    ADDR_TX_DATA: begin
                        s_axi_rdata <= {
                            {(C_S_AXI_DATA_WIDTH-8){1'b0}},
                            char_id
                        };
                    end

                    ADDR_TX_CTRL: begin
                        s_axi_rdata <= {C_S_AXI_DATA_WIDTH{1'b0}};
                    end

                    ADDR_TX_STATUS: begin
                        s_axi_rdata <= {
                            {(C_S_AXI_DATA_WIDTH-2){1'b0}},
                            tx_busy,
                            char_ready
                        };
                    end

                    default: begin
                        s_axi_rdata <= {C_S_AXI_DATA_WIDTH{1'b0}};
                    end
                endcase

                s_axi_rresp  <= AXI_RESP_OKAY;
                s_axi_rvalid <= 1'b1;
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
        end
    end

endmodule
