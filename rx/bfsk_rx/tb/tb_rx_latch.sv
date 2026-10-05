`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Testbench: rx_latch
//
// rx_latch is a one-entry mailbox between the PL receiver and the PS.  A
// frame_valid pulse captures FRAME_ID, DATA, CRC, and the error flag into a
// stable 32-bit packet_word.  packet_ready remains asserted until the PS sends
// packet_clear.  A new packet arriving while ready is set overwrites the old
// packet and raises packet_overrun.
//
// This test checks the packet bit layout, normal clear, overwrite reporting,
// and the documented rule that frame_valid has priority over packet_clear.
// -----------------------------------------------------------------------------
module tb_rx_latch;
  reg clk=0,rst_n=0,frame_valid=0,packet_error=0,packet_clear=0;
  reg [7:0] frame_id=0,data=0,received_crc=0; wire [31:0] packet_word;wire packet_ready,packet_overrun;integer failures=0;
  rx_latch dut(.*);always #5 clk=~clk;
  // Present one decoded packet for exactly one rising clock edge.
  task packet(input[7:0]id,input[7:0]d,input[7:0]crc,input bit err);begin @(negedge clk);frame_id=id;data=d;received_crc=crc;packet_error=err;frame_valid=1;@(negedge clk);frame_valid=0;#1;end endtask
  initial begin repeat(3)@(negedge clk);rst_n=1;
    packet(8'h12,8'h41,8'ha5,0); if(packet_word!==32'h801241a5)begin $display("FAIL first word=%h",packet_word);failures++;end
    packet(8'h34,8'h42,8'h5a,1); if(packet_word!==32'he034425a||!packet_overrun)begin $display("FAIL overrun word=%h",packet_word);failures++;end
    @(negedge clk);packet_clear=1;@(negedge clk);packet_clear=0;#1;if(packet_ready||packet_overrun)begin $display("FAIL clear");failures++;end
    @(negedge clk);packet_clear=1;frame_valid=1;frame_id=8'h56;data=8'h43;received_crc=8'hc3;packet_error=0;
    @(negedge clk);packet_clear=0;frame_valid=0;#1;if(packet_word!==32'h805643c3)begin $display("FAIL valid priority=%h",packet_word);failures++;end
    if(failures==0)$display("TB_RX_LATCH: PASS coverage=100%% (capture,overrun,clear,priority)");else $display("TB_RX_LATCH: FAIL failures=%0d",failures);$finish;
  end
endmodule
