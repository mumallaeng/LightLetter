`timescale 1ns / 1ps
// 100 MHz / 625 = 160 kHz. ADC/FFT backpressure must not change sample spacing.
module xadc_sample_trigger #(
    parameter PERIOD_CYCLES = 625,
    parameter PULSE_CYCLES = 4
) (
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk CLK", X_INTERFACE_PARAMETER="ASSOCIATED_RESET rst_n" *)
    input wire clk,
    (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 rst_n RST", X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *)
    input wire rst_n,
    input wire enable,
    output reg convst
);
    localparam COUNTER_W = (PERIOD_CYCLES>1) ? $clog2(PERIOD_CYCLES) : 1;
    reg [COUNTER_W-1:0] count;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n)begin count<=0;convst<=0;end
        else if(!enable)begin count<=0;convst<=0;end
        else begin
            if(count==PERIOD_CYCLES-1)count<=0;
            else count<=count+1'b1;
            // First trigger occurs after a full period, then repeats every period.
            convst <= (count==PERIOD_CYCLES-1) || (count<PULSE_CYCLES-1 && convst);
        end
    end
endmodule
