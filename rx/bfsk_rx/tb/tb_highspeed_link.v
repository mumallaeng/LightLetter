`timescale 1ns/1ps
// 실제 TX 파형을 104클록마다 샘플링하여 버퍼, FFT, RX, CRC까지 검증한다.
module tb_highspeed_link;
reg clk=0; always #5 clk=~clk;
reg rst=1, char_valid=0; reg [7:0] char_id=0;
wire ready,busy,optical,enable;
optical_tx_top tx(.clk(clk),.rst(rst),.char_id(char_id),.char_valid(char_valid),.char_ready(ready),.tx_busy(busy),.optical_tx(optical),.tx_enable(enable));
reg [11:0] adc=0; reg adc_valid=0; integer sample_count=0;
always @(negedge clk) begin
  adc_valid=0;
  if(rst) sample_count=0;
  else if(sample_count==103) begin
    sample_count=0; adc=optical?12'd2500:12'd1500; adc_valid=1;
  end else sample_count=sample_count+1;
end
wire fv,fr,bv,br,overflow; wire [39:0] d0,d1,core_data,power;
wire cv,pv;
fft_buffer buf_dut(.clk(clk),.rst(rst),.i_adc_data(adc),.i_adc_data_valid(adc_valid),.o_frame_valid(fv),.i_frame_ready(fr),.o_buf_data_valid(bv),.i_buf_data_ready(br),.o_buf_data0(d0),.o_buf_data1(d1),.o_overflow(overflow));
fft_core core_dut(.clk(clk),.rst(rst),.i_frame_valid(fv),.o_frame_ready(fr),.i_buf_data_valid(bv),.o_buf_data_ready(br),.i_buf_data0(d0),.i_buf_data1(d1),.o_fft_core_data(core_data),.o_fft_core_valid(cv));
fft_power power_dut(.clk(clk),.rst(rst),.i_fft_core_data(core_data),.i_fft_core_valid(cv),.power_result(power),.fft_mag_valid(pv));
wire [7:0] fid,data,crc; wire valid,error,block_done;
rx_top rx(.clk(clk),.rst_n(!rst),.fft_mag(power),.fft_mag_valid(pv),.frame_id(fid),.data(data),.received_crc(crc),.frame_valid(valid),.packet_error(error),.fft_block_done(block_done));
integer received=0,blocks=0,errors=0; reg [7:0] expected[0:2];
always @(posedge clk) if(!rst) begin
  if(overflow) $fatal(1,"BUFFER_OVERFLOW");
  if(error) begin errors=errors+1; $display("RX_ERROR time=%t",$time); end
  if(block_done) blocks=blocks+1;
  if(valid) begin
    if(received>=3) $fatal(1,"DUPLICATE");
    if(data!==expected[received] || fid!==received) $fatal(1,"DATA_MISMATCH got=%h id=%h expected=%h",data,fid,expected[received]);
    $display("FRAME_PASS id=%h data=%h crc=%h blocks=%0d",fid,data,crc,blocks);
    received=received+1;
  end
end
// 별도 Carrier 입력으로 심볼 길이와 모든 에지 간격을 측정한다.
reg [1:0] sym=0; reg sv=0,ss=0; wire done,co,ce;
bfsk_carrier_gen carrier(.clk(clk),.rst(rst),.symbol_type(sym),.symbol_valid(sv),.symbol_start(ss),.symbol_done(done),.optical_tx(co),.tx_enable(ce));
integer j,n,last_edge,edges,half; reg prev;
task check_tone;
input [1:0] code; input integer h; input integer exp_edges;
begin
 @(negedge clk);sym=code;sv=1;ss=1;
 @(posedge clk);#1;
 @(negedge clk);ss=0;sv=0;
 last_edge=0;edges=0;prev=0;
 for(n=1;n<=26624;n=n+1) begin
   @(posedge clk);#1;
   if(co!=prev) begin
     if(n-last_edge!=h) $fatal(1,"HALF_PERIOD code=%d clocks=%d",code,n-last_edge);
     last_edge=n; edges=edges+1;prev=co;
   end
   if(n<26624 && done) $fatal(1,"EARLY_DONE");
 end
 if(!done || ce || co || edges!=exp_edges) $fatal(1,"TONE_END code=%d edges=%d",code,edges);
 $display("TONE_PASS code=%d half=%d symbol=26624 edges=%d",code,h,edges);
end
endtask
initial begin
 expected[0]=8'h41;expected[1]=8'h00;expected[2]=8'hff;
 repeat(10) @(negedge clk);rst=0;
 check_tone(1,832,32); check_tone(2,512,52); check_tone(3,416,64);
 for(j=0;j<3;j=j+1) begin
   wait(ready); @(negedge clk);char_id=expected[j];char_valid=1;
   @(negedge clk);char_valid=0;
   wait(busy);wait(!busy);
   repeat(40000+j*53) @(posedge clk);
   if(received!=j+1) $fatal(1,"MISSING_FRAME index=%d received=%d",j,received);
 end
 if(errors!=0) $fatal(1,"RX_ERRORS=%d",errors);
 $display("HIGHSPEED_LINK_PASS frames=%d blocks=%d overflow=0",received,blocks);$finish;
end
initial begin #50000000; $fatal(1,"TIMEOUT"); end
endmodule
