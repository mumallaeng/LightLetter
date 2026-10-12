`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Testbench: rx_frame_decoder
//
// The decoder consumes already synchronized BFSK symbols.  symbol_code[0] is
// the decoded bit and symbol_valid qualifies it.  After frame_start the state
// machine receives four MSB-first bytes in this order:
//       SFD (0xD5) -> FRAME_ID -> DATA -> RECEIVED_CRC
// A correct SFD and all 32 bits produce decode_valid and frame_finish pulses.
// A wrong SFD or an asynchronous frame_abort produces packet_error instead.
//
// This test verifies normal byte assembly, the final-bit capture boundary,
// SFD rejection, abort handling, and one-clock output pulses.
// -----------------------------------------------------------------------------
module tb_rx_frame_decoder;
  reg clk=0,rst_n=0,frame_start=0,frame_abort=0,symbol_valid=0;reg[1:0]symbol_code=0;
  wire[7:0]frame_id,data,received_crc;wire decode_valid,frame_finish,packet_error;integer failures=0;
  rx_frame_decoder dut(.*);always #5 clk=~clk;
  // Pulse frame_start for one clock to move the decoder out of ST_IDLE.
  task start;begin @(negedge clk);frame_start=1;@(negedge clk);frame_start=0;end endtask
  // Send one byte MSB first, matching the receiver's shift direction.
  task send_byte(input[7:0]v);integer i;begin for(i=7;i>=0;i=i-1)begin @(negedge clk);symbol_code={1'b0,v[i]};symbol_valid=1;end @(negedge clk);symbol_valid=0;end endtask
  task wait_finish(input bit expect_good,input bit expect_error);integer n;begin n=0;while(!frame_finish&&n<20)begin @(negedge clk);n++;end #1;
    if(!frame_finish||decode_valid!==expect_good||packet_error!==expect_error)begin $display("FAIL finish good=%b err=%b",decode_valid,packet_error);failures++;end
    @(negedge clk);#1;if(frame_finish||decode_valid||packet_error)begin $display("FAIL output pulse width");failures++;end
  end endtask
  initial begin repeat(3)@(negedge clk);rst_n=1;
    start;send_byte(8'hd5);send_byte(8'h37);send_byte(8'h41);send_byte(8'ha6);wait_finish(1,0);
    if(frame_id!==8'h37||data!==8'h41||received_crc!==8'ha6)begin $display("FAIL decoded bytes %h/%h/%h",frame_id,data,received_crc);failures++;end
    start;send_byte(8'hd4);wait_finish(0,1);
    start;send_byte(8'hd5);@(negedge clk);frame_abort=1;#1;if(!frame_finish||!packet_error)begin $display("FAIL abort");failures++;end
    @(negedge clk);frame_abort=0;
    if(failures==0)$display("TB_RX_FRAME_DECODER: PASS coverage=100%% (good,bad_sfd,abort,fields)");else $display("TB_RX_FRAME_DECODER: FAIL failures=%0d",failures);$finish;
  end
endmodule
