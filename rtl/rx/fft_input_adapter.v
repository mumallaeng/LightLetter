`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// XADC Wizard M_AXIS -> 128-point FFT S_AXIS_DATA / S_AXIS_CONFIG
// 단일 채널 unipolar XADC: 12bit unsigned sample = TDATA[15:4].
// ADC code 2048 is the assumed DC center. Re is signed 12bit; Im is zero.
// XADC Wizard's existing FIFO holds samples while FFT applies backpressure.
// One elastic output register preserves TDATA/TLAST while TREADY is low.
// FFT: 12bit fixed-point, Scaled, fixed transform length 128, no cyclic prefix.
// FFT_ARCH: 0=Pipelined Streaming I/O; 1=Radix-2/Lite Burst I/O.
// Conservative scaling totals 8 right shifts (=1/256) in either architecture.
//////////////////////////////////////////////////////////////////////////////////
module fft_input_adapter #(
    parameter FFT_ARCH = 0,
    parameter CONFIG_W = 16,
    parameter [CONFIG_W-1:0] CONFIG_DATA = (FFT_ARCH==0) ? 16'h00D7 : 16'h2AAD
) (
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk CLK",
       X_INTERFACE_PARAMETER="ASSOCIATED_BUSIF S_AXIS_ADC:M_AXIS_DATA:M_AXIS_CONFIG, ASSOCIATED_RESET rst_n" *)
    input wire clk,
    (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 rst_n RST", X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *)
    input wire rst_n,

    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 S_AXIS_ADC TDATA" *)
    input wire [15:0] s_axis_adc_tdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 S_AXIS_ADC TID" *)
    input wire [4:0] s_axis_adc_tid,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 S_AXIS_ADC TVALID" *)
    input wire s_axis_adc_tvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 S_AXIS_ADC TREADY" *)
    output wire s_axis_adc_tready,

    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_DATA TDATA" *)
    output reg [31:0] m_axis_data_tdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_DATA TVALID" *)
    output reg m_axis_data_tvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_DATA TREADY" *)
    input wire m_axis_data_tready,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_DATA TLAST" *)
    output reg m_axis_data_tlast,

    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_CONFIG TDATA" *)
    output wire [CONFIG_W-1:0] m_axis_config_tdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_CONFIG TVALID" *)
    output wire m_axis_config_tvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:axis:1.0 M_AXIS_CONFIG TREADY" *)
    input wire m_axis_config_tready,
    output reg configured
);
    reg [6:0] sample_count;
    wire slot_available = !m_axis_data_tvalid || m_axis_data_tready;
    assign s_axis_adc_tready = rst_n && configured && slot_available;
    assign m_axis_config_tdata = CONFIG_DATA;
    assign m_axis_config_tvalid = rst_n && !configured;
    // For unipolar ADC, flipping the sign bit is exactly (unsigned code - 2048).
    wire [11:0] centered_sample = s_axis_adc_tdata[15:4] ^ 12'h800;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            configured <= 0;
            sample_count <= 0;
            m_axis_data_tdata <= 0;
            m_axis_data_tvalid <= 0;
            m_axis_data_tlast <= 0;
        end else begin
            if (m_axis_config_tvalid && m_axis_config_tready)
                configured <= 1;
            if (configured && slot_available) begin
                m_axis_data_tvalid <= s_axis_adc_tvalid;
                m_axis_data_tlast <= s_axis_adc_tvalid && (sample_count==127);
                if (s_axis_adc_tvalid) begin
                    m_axis_data_tdata <= {20'b0, centered_sample};
                    // Count samples accepted into the ordered output register.
                    // It cannot accept another until this sample is transferred.
                    sample_count <= sample_count + 1'b1;
                end
            end
        end
    end
    // TID is intentionally unused: this adapter requires Single Channel mode.
endmodule
