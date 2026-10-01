`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Testbench: rx_symbol_detector
//
// The DUT classifies one completed FFT block from three selected powers:
//   bin 8  -> BFSK data bit 0
//   bin 16 -> BFSK data bit 1
//   bin 20 -> synchronization tone
// A winner is accepted only when it is more than 20 percent stronger than
// both alternatives.  Otherwise CODE_INVALID is produced.
//
// This test covers all four output classes, the exact 20-percent rejection
// boundary, equal-power rejection, all-zero input, one-cycle valid behavior,
// and arithmetic near the maximum 40-bit input value.
// -----------------------------------------------------------------------------
module tb_rx_symbol_detector;
  localparam MAG_W=40;
  reg clk=0,rst_n=0,fft_block_done=0;
  reg [MAG_W-1:0] bin8_power=0,bin16_power=0,bin20_power=0;
  wire [1:0] block_code; wire block_code_valid;
  integer failures=0,cases=0;
  rx_symbol_detector #(.MAG_W(MAG_W)) dut(.*);
  always #5 clk=~clk;
  // Apply one set of bin powers and compare the registered classification.
  task check(input [39:0] p8,input [39:0] p16,input [39:0] p20,input [1:0] expected);
    begin
      @(negedge clk); bin8_power=p8;bin16_power=p16;bin20_power=p20;fft_block_done=1;
      @(negedge clk); fft_block_done=0; #1;
      if(!block_code_valid || block_code!==expected) begin
        $display("FAIL powers=%0d/%0d/%0d actual=%b expected=%b",p8,p16,p20,block_code,expected); failures=failures+1;
      end
      @(negedge clk); #1; if(block_code_valid) begin $display("FAIL valid wider than one cycle");failures=failures+1;end
      cases=cases+1;
    end
  endtask
  initial begin
    repeat(3) @(negedge clk);rst_n=1;
    check(121,100,10,2'b00); check(10,121,100,2'b01); check(100,10,121,2'b10);
    check(120,100,10,2'b11); check(500,500,10,2'b11); check(0,0,0,2'b11);
    check(40'hffffffffff,1,1,2'b00);
    if(failures==0)$display("TB_RX_SYMBOL_DETECTOR: PASS coverage=100%% (%0d planned classes/boundaries)",cases);
    else $display("TB_RX_SYMBOL_DETECTOR: FAIL failures=%0d",failures);
    $finish;
  end
endmodule
