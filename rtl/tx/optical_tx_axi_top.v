`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: optical_tx_axi_top
//
// 설명:
//   TX-7에서 검증한 AXI4-Lite Slave Wrapper와
//   TX-6에서 검증한 optical_tx_top을 연결하는 TX-8 통합 Top이다.
//
// 전체 구조:
//   Zynq PS / AXI4-Lite Master
//            ↓
//   axi_lite_tx_wrapper
//            ↓
//   char_id / char_valid / char_ready / tx_busy
//            ↓
//   optical_tx_top
//            ↓
//   optical_tx / tx_enable
//
// Register Map:
//   0x00 TX_DATA   R/W : [7:0] Character ID
//   0x04 TX_CTRL   W   : [0] START
//   0x08 TX_STATUS R   : [0] READY, [1] BUSY
//
// Reset:
//   AXI4-Lite의 active-low Reset(s_axi_aresetn)을 사용한다.
//   기존 optical_tx_top은 active-high rst이므로 내부에서 반전하여 연결한다.
//
// 설계 원칙:
//   - TX-1 ~ TX-7의 PASS RTL 내부 동작은 변경하지 않는다.
//   - AXI Wrapper와 optical_tx_top 사이의 기존 Character Handshake만 연결한다.
//   - AXI Clock과 TX Clock은 동일 Clock Domain으로 사용한다.
//////////////////////////////////////////////////////////////////////////////////

module optical_tx_axi_top #(
    // AXI4-Lite Parameter
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 4,

    // TX Clock / Symbol Parameter
    parameter integer CLK_FREQ_HZ        = 100_000_000,
    parameter integer FS_HZ              = 160_000,
    parameter integer SYMBOL_SAMPLES     = 256,

    // BFSK Frequency Parameter
    parameter integer F0_HZ              = 10_000,
    parameter integer F1_HZ              = 20_000,
    parameter integer FSYNC_HZ           = 25_000,

    // Frame Parameter
    parameter integer PREAMBLE_SYMBOLS   = 4,
    parameter [7:0]   SFD                = 8'hD5
)(
    // ============================================================
    // AXI4-Lite Slave Interface
    // ============================================================
    input  wire                              s_axi_aclk,
    input  wire                              s_axi_aresetn,

    // Write Address Channel
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_awaddr,
    input  wire                              s_axi_awvalid,
    output wire                              s_axi_awready,

    // Write Data Channel
    input  wire [C_S_AXI_DATA_WIDTH-1:0]     s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                              s_axi_wvalid,
    output wire                              s_axi_wready,

    // Write Response Channel
    output wire [1:0]                        s_axi_bresp,
    output wire                              s_axi_bvalid,
    input  wire                              s_axi_bready,

    // Read Address Channel
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_araddr,
    input  wire                              s_axi_arvalid,
    output wire                              s_axi_arready,

    // Read Data Channel
    output wire [C_S_AXI_DATA_WIDTH-1:0]     s_axi_rdata,
    output wire [1:0]                        s_axi_rresp,
    output wire                              s_axi_rvalid,
    input  wire                              s_axi_rready,

    // ============================================================
    // Optical TX Output
    // ============================================================
    output wire                              optical_tx,
    output wire                              tx_enable
);

    // ============================================================
    // AXI Wrapper <-> Optical TX Top
    // ============================================================
    wire [7:0] char_id;
    wire       char_valid;
    wire       char_ready;
    wire       tx_busy;

    // optical_tx_top의 Reset은 active-high
    wire       tx_rst;

    assign tx_rst = ~s_axi_aresetn;

    // ============================================================
    // AXI4-Lite Slave Wrapper
    // ============================================================
    axi_lite_tx_wrapper #(
        .C_S_AXI_DATA_WIDTH(C_S_AXI_DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH(C_S_AXI_ADDR_WIDTH)
    ) U_AXI_LITE_TX_WRAPPER (
        .s_axi_aclk    (s_axi_aclk),
        .s_axi_aresetn (s_axi_aresetn),

        .s_axi_awaddr  (s_axi_awaddr),
        .s_axi_awvalid (s_axi_awvalid),
        .s_axi_awready (s_axi_awready),

        .s_axi_wdata   (s_axi_wdata),
        .s_axi_wstrb   (s_axi_wstrb),
        .s_axi_wvalid  (s_axi_wvalid),
        .s_axi_wready  (s_axi_wready),

        .s_axi_bresp   (s_axi_bresp),
        .s_axi_bvalid  (s_axi_bvalid),
        .s_axi_bready  (s_axi_bready),

        .s_axi_araddr  (s_axi_araddr),
        .s_axi_arvalid (s_axi_arvalid),
        .s_axi_arready (s_axi_arready),

        .s_axi_rdata   (s_axi_rdata),
        .s_axi_rresp   (s_axi_rresp),
        .s_axi_rvalid  (s_axi_rvalid),
        .s_axi_rready  (s_axi_rready),

        .char_id       (char_id),
        .char_valid    (char_valid),
        .char_ready    (char_ready),
        .tx_busy       (tx_busy)
    );

    // ============================================================
    // Optical TX Core
    // ============================================================
    optical_tx_top #(
        .CLK_FREQ_HZ      (CLK_FREQ_HZ),
        .FS_HZ            (FS_HZ),
        .SYMBOL_SAMPLES   (SYMBOL_SAMPLES),

        .F0_HZ            (F0_HZ),
        .F1_HZ            (F1_HZ),
        .FSYNC_HZ         (FSYNC_HZ),

        .PREAMBLE_SYMBOLS (PREAMBLE_SYMBOLS),
        .SFD              (SFD)
    ) U_OPTICAL_TX_TOP (
        .clk        (s_axi_aclk),
        .rst        (tx_rst),

        .char_id    (char_id),
        .char_valid (char_valid),
        .char_ready (char_ready),
        .tx_busy    (tx_busy),

        .optical_tx (optical_tx),
        .tx_enable  (tx_enable)
    );

endmodule
