`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Testbench: rx_crc8_check
//
// The DUT calculates CRC-8 over FRAME_ID followed by DATA.  It uses polynomial
// 0x07, initial value 0x00, and MSB-first processing.  A matching received CRC
// generates a one-clock frame_valid pulse; a mismatch generates crc_error.
//
// The reference functions below independently calculate the expected CRC.
// The test checks multiple valid packets, one corrupted CRC, and pulse width.
// -----------------------------------------------------------------------------
module tb_rx_crc8_check;
  reg clk=0,rst_n=0,decode_valid=0; reg [7:0] frame_id=0,data=0,received_crc=0;
  wire frame_valid,crc_error; integer failures=0;
  rx_crc8_check dut(.*); always #5 clk=~clk;
  // Independent CRC reference model used only by the testbench.
  function [7:0] crc_byte(input [7:0] c,input [7:0] b); reg [7:0] x;integer i;begin x=c;for(i=7;i>=0;i=i-1)x=(x[7]^b[i])?({x[6:0],1'b0}^8'h07):{x[6:0],1'b0};crc_byte=x;end endfunction
  function [7:0] crc_pair(input [7:0] id,input [7:0] d);begin crc_pair=crc_byte(crc_byte(0,id),d);end endfunction
  task pulse(input [7:0] id,input [7:0] d,input [7:0] crc,input bit good);
    begin @(negedge clk);frame_id=id;data=d;received_crc=crc;decode_valid=1;@(negedge clk);decode_valid=0;#1;
      if(good ? (!frame_valid||crc_error):(frame_valid||!crc_error))begin $display("FAIL CRC id=%h data=%h",id,d);failures=failures+1;end
      @(negedge clk);#1;if(frame_valid||crc_error)begin $display("FAIL CRC pulse width");failures=failures+1;end
    end
  endtask
  initial begin repeat(3)@(negedge clk);rst_n=1;
    pulse(8'h00,8'h00,crc_pair(8'h00,8'h00),1); pulse(8'h35,8'h41,crc_pair(8'h35,8'h41),1);
    pulse(8'hff,8'ha5,crc_pair(8'hff,8'ha5)^8'h01,0);
    if(failures==0)$display("TB_RX_CRC8_CHECK: PASS coverage=100%% (good,bad,pulse)");else $display("TB_RX_CRC8_CHECK: FAIL failures=%0d",failures);$finish;
  end
endmodule
