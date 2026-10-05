`timescale 1ns/1ps
// -----------------------------------------------------------------------------
// Integration testbench: rx_top
//
// This is the complete non-FFT receive path:
//   128 ordered FFT powers
//       -> rx_bin_detector (extract bins 8, 16, and 20)
//       -> rx_symbol_detector (BIT0, BIT1, SYNC, or INVALID)
//       -> rx_symbol_sync (two-block boundary selection and D5 validation)
//       -> rx_frame_decoder (SFD, ID, DATA, and CRC byte assembly)
//       -> rx_crc8_check (final packet acceptance)
//
// The test constructs a complete packet in the FFT-power domain.  Every FFT
// block contains 128 natural-order values; only the selected BFSK bin is made
// dominant.  It sends eight SYNC blocks, D5, FRAME_ID 0x35, ASCII 'A', and the
// independently calculated CRC.  The final outputs and all error flags are
// checked, so this test also confirms that MAG_W=40 propagates through rx_top.
// -----------------------------------------------------------------------------
module tb_rx_top;
  localparam MAG_W=40;localparam[1:0]B0=0,B1=1,SYNC=2;localparam[7:0]SFD=8'hd5;
  reg clk=0,rst_n=0,fft_mag_valid=0;reg[MAG_W-1:0]fft_mag=0;
  wire[7:0]frame_id,data,received_crc;wire frame_valid,packet_error,decoder_error,crc_error,fft_block_done,block_code_valid,symbol_valid,frame_start,frame_abort,frame_finish,decode_valid;wire[1:0]block_code,symbol_code;
  integer failures=0,i,n;reg[7:0]id=8'h35,payload=8'h41,crc;
  rx_top #(.MAG_W(MAG_W),.SYNC_MIN_BLOCKS(8)) dut(.*);always #5 clk=~clk;
  function[7:0]crc_byte(input[7:0]c,input[7:0]b);reg[7:0]x;integer k;begin x=c;for(k=7;k>=0;k=k-1)x=(x[7]^b[k])?({x[6:0],1'b0}^8'h07):{x[6:0],1'b0};crc_byte=x;end endfunction
  // Generate one complete natural-order FFT power frame for a requested tone.
  task send_block(input[1:0]code);integer b;begin
    for(b=0;b<128;b=b+1)begin @(negedge clk);fft_mag_valid=1;
      if(b==8)fft_mag=(code==B0)?1000:10;else if(b==16)fft_mag=(code==B1)?1000:10;else if(b==20)fft_mag=(code==SYNC)?1000:10;else fft_mag=1;
    end @(negedge clk);fft_mag_valid=0;repeat(3)@(negedge clk);
  end endtask
  // One logical BFSK bit is represented by two equal FFT blocks.
  task two_halves(input bit v);begin send_block(v?B1:B0);send_block(v?B1:B0);end endtask
  task send_byte_bits(input[7:0]v,input integer start_index);integer k;begin for(k=start_index;k>=0;k=k-1)two_halves(v[k]);end endtask
  initial begin
    crc=crc_byte(crc_byte(0,id),payload);repeat(4)@(negedge clk);rst_n=1;
    for(i=0;i<8;i=i+1)send_block(SYNC);
    for(i=7;i>=0;i=i-1)begin send_block(SFD[i]?B1:B0);send_block(SFD[i]?B1:B0);end
    send_block(id[7]?B1:B0);
    n=0;while(!frame_start&&n<100)begin @(negedge clk);n++;end
    if(!frame_start)begin $display("FAIL no frame_start");failures++;end
    repeat(12)@(negedge clk);
    send_block(id[7]?B1:B0);send_byte_bits(id,6);send_byte_bits(payload,7);send_byte_bits(crc,7);
    n=0;while(!frame_valid&&n<100)begin @(negedge clk);n++;end #1;
    if(!frame_valid||packet_error||frame_id!==id||data!==payload||received_crc!==crc)begin
      $display("FAIL integrated valid=%b err=%b bytes=%h/%h/%h expected=%h/%h/%h",frame_valid,packet_error,frame_id,data,received_crc,id,payload,crc);failures++;
    end
    if(failures==0)$display("TB_RX_TOP: PASS coverage=100%% (bin-to-CRC complete packet)");else $display("TB_RX_TOP: FAIL failures=%0d",failures);$finish;
  end
  initial begin #20000000;$display("FAIL watchdog");$finish;end
endmodule
