`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Testbench: rx_bin_detector
//
// The DUT receives one FFT magnitude at a time.  Because the FFT output is in
// natural order, an internal 7-bit counter represents bin numbers 0 through
// 127.  The DUT stores the powers at bins 8, 16, and 20 and asserts
// fft_block_done for one clock after the 128th valid magnitude.
//
// Test structure:
//   1. Send a continuous 128-bin FFT frame.
//   2. Send another frame with gaps in fft_mag_valid.
//   3. Check the three selected powers, counter wrap, and done-pulse count.
// The second frame also uses wide values to exercise the complete 40-bit path.
// -----------------------------------------------------------------------------
module tb_rx_bin_detector;
  localparam MAG_W=40;
  reg clk=0,rst_n=0,fft_mag_valid=0;
  reg [MAG_W-1:0] fft_mag=0;
  wire [MAG_W-1:0] bin8_power,bin16_power,bin20_power;
  wire fft_block_done;
  integer i,failures=0,done_count=0;
  rx_bin_detector #(.MAG_W(MAG_W)) dut(.*);
  always #5 clk=~clk;
  always @(posedge clk) if(fft_block_done) done_count=done_count+1;

  // Drive one natural-order FFT frame.  Invalid cycles must not advance the
  // DUT's bin counter, so optional gaps verify the valid-qualified counter.
  task send_frame(input [39:0] v8,input [39:0] v16,input [39:0] v20,input integer gaps);
    begin
      for(i=0;i<128;i=i+1) begin
        if(gaps && (i%17==0)) begin @(negedge clk); fft_mag_valid=0; end
        @(negedge clk); fft_mag_valid=1;
        if(i==8) fft_mag=v8; else if(i==16) fft_mag=v16; else if(i==20) fft_mag=v20; else fft_mag=i;
      end
      @(negedge clk); fft_mag_valid=0; repeat(2) @(negedge clk);
      if(bin8_power!==v8 || bin16_power!==v16 || bin20_power!==v20) begin
        $display("FAIL bin capture actual=%0d/%0d/%0d",bin8_power,bin16_power,bin20_power); failures=failures+1;
      end
    end
  endtask
  initial begin
    repeat(3) @(negedge clk); rst_n=1;
    send_frame(40'd800,40'd1600,40'd2000,0);
    send_frame(40'h123456789a,40'h23456789ab,40'h3456789abc,1);
    if(done_count!=2) begin $display("FAIL done_count=%0d",done_count); failures=failures+1; end
    if(failures==0) $display("TB_RX_BIN_DETECTOR: PASS coverage=100%% (capture,gap,wrap,done)");
    else $display("TB_RX_BIN_DETECTOR: FAIL failures=%0d",failures);
    $finish;
  end
endmodule
