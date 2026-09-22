`timescale 1ns / 1ps

module tb_power_sync;
    parameter ADC_FILE = "fft_input_25000hz.mem";
    localparam time TIME_LIMIT_NS = 500000;
    reg clk = 0;
    reg rst = 1;
    reg i_read_data_valid = 0;
    wire o_bf_out_valid;
    localparam integer MAX_WAIT_CYCLES = 64;
    always #10 clk = ~clk;

    reg signed [19:0] a_re=0, a_im=0, b_re=0, b_im=0;
    reg [5:0] twiddle_addr=0;
    wire [31:0] twiddle_factor;
    wire signed [19:0] y0_re,y0_im,y1_re,y1_im;
    twiddle_rom u_twiddle_rom (
        .clk(clk), .twiddle_addr(twiddle_addr), .twiddle_factor(twiddle_factor)
    );
    // Combinational datapath; only the valid-control FSM is clocked.
    butterfly dut (
        .clk(clk), .rst(rst),
        .i_read_data_valid(i_read_data_valid),
        .o_bf_out_valid(o_bf_out_valid),
        .a_re(a_re), .a_im(a_im), .b_re(b_re), .b_im(b_im),
        .twiddle_factor(twiddle_factor),
        .y0_re(y0_re), .y0_im(y0_im), .y1_re(y1_re), .y1_im(y1_im)
    );

    reg [39:0] adc_samples[0:127]; // {signed real20, signed imaginary20}
    reg [39:0] fft_mem[0:127];
    // Independent reference memory, initialized from the SAME current ADC file.
    reg [39:0] reference_mem[0:127];
    reg [39:0] expected_y0,expected_y1;
    reg signed [63:0] ar,ai,br,bi,wr,wi,tr,ti,r0,i0,r1,i1;
    reg [31:0] coefficients[0:63];
    reg [39:0] saved_y0,saved_y1;
    integer stage,half_size,group_base,j,n;
    integer a_addr,b_addr,w_addr,completed=0,stage_count;
    time start_time,elapsed,stage_start;
    reg started=0,finished=0;

    // Power DUT receives the actual FFT RTL output stored by this TB.
    reg [39:0] i_fft_core_data=0;
    reg i_fft_core_valid=0;
    wire [39:0] fft_mag;
    wire fft_mag_valid;
    power_top u_power_top (
        .clk(clk), .rst(rst), .i_fft_core_data(i_fft_core_data),
        .i_fft_core_valid(i_fft_core_valid),
        .fft_mag(fft_mag), .fft_mag_valid(fft_mag_valid)
    );
    reg [39:0] expected_power[0:127];
    reg [39:0] received_power[0:127];
    integer supplied=0, received=0, cycle=0;
    integer final_input_cycle=0, last_rx_cycle=0;
    integer peak_bin=0, csv_file;
    reg [39:0] peak_power=0;
    reg signed [63:0] ref_re, ref_im;

    // Capture pre-edge outputs, just as synchronous RX logic does.
    always @(posedge clk) begin
        cycle=cycle+1;
        if (!rst) begin
            if (fft_mag_valid !== 1'b0 && fft_mag_valid !== 1'b1)
                $fatal(1,"Unknown Power output valid");
            if (fft_mag_valid) begin
                if (supplied!=128 || received>=128)
                    $fatal(1,"Power frame length error");
                if (received==0 && cycle!=final_input_cycle+2)
                    $fatal(1,"First RX capture timing error");
                if (received>0 && cycle!=last_rx_cycle+1)
                    $fatal(1,"Gap in Power output stream");
                if (fft_mag !== expected_power[received])
                    $fatal(1,"Power mismatch bin=%0d expected=%0d got=%0d",
                           received,expected_power[received],fft_mag);
                received_power[received]=fft_mag;
                $fdisplay(csv_file,"%0d,%0d,%0d,%0d,%0d,PASS",received,
                    $signed(fft_mem[received][39:20]),$signed(fft_mem[received][19:0]),
                    expected_power[received],fft_mag);
                // Real sine has a mirror peak; select only positive frequencies.
                if (received>0 && received<64 && fft_mag>peak_power) begin
                    peak_power=fft_mag; peak_bin=received;
                end
                last_rx_cycle=cycle;
                received=received+1;
            end
        end
    end

    task check_power_frame;
        integer bin_id;
        begin
            csv_file=$fopen("power_sync_comparison.csv","w");
            if (!csv_file) $fatal(1,"Cannot open Power report");
            $fdisplay(csv_file,"bin,fft_re,fft_im,expected_power,actual_power,result");
            for(bin_id=0;bin_id<128;bin_id=bin_id+1) begin
                // Golden FFT memory is calculated independently of DUT outputs.
                ref_re=$signed(reference_mem[bin_id][39:20]);
                ref_im=$signed(reference_mem[bin_id][19:0]);
                expected_power[bin_id]=ref_re*ref_re+ref_im*ref_im;
            end
            for(bin_id=0;bin_id<128;bin_id=bin_id+1) begin
                @(negedge clk);
                i_fft_core_data=fft_mem[bin_id]; i_fft_core_valid=1;
                @(posedge clk); #1;
                supplied=supplied+1;
                if(bin_id==127) final_input_cycle=cycle;
                if(fft_mag_valid !== 0) $fatal(1,"Power output before full collection");
            end
            @(negedge clk); i_fft_core_valid=0;
            while(received<128) begin @(posedge clk); #1; end
            if(fft_mag_valid !== 0) $fatal(1,"Valid must fall after final RX capture");
            repeat(3) begin
                @(posedge clk); #1;
                if(fft_mag_valid !== 0) $fatal(1,"Extra Power output");
            end
            $fclose(csv_file);
            if(peak_bin!=20 || peak_power==0) $fatal(1,"Expected positive peak bin 20, got %0d",peak_bin);
            if(!(received_power[20]>received_power[8] && received_power[20]>received_power[16]))
                $fatal(1,"F_sync candidate is not dominant");
            $display("BIN POWER: P8=%0d P16=%0d P20=%0d mirror P108=%0d",
                received_power[8],received_power[16],received_power[20],received_power[108]);
            $display("TB_POWER_SYNC PASS: Fs=160000 Hz, N=128, peak bin=%0d, frequency=%0d Hz, expected symbol=F_sync",
                peak_bin,peak_bin*1250);
            $display("128/128 Power values matched. Symbol label is TB-only; RX decoder is not instantiated.");
        end
    endtask

    function [6:0] reverse7;
        input [6:0] value;
        begin
            reverse7={value[0],value[1],value[2],value[3],value[4],value[5],value[6]};
        end
    endfunction

    task check;
        input condition;
        input [8*100-1:0] message;
        begin
            if (condition !== 1'b1)
                $fatal(1,"FAIL stage=%0d A=%0d B=%0d W=%0d time=%0t: %0s",
                       stage,a_addr,b_addr,w_addr,$time,message);
        end
    endtask

    // Reference calculation uses its own memory, never DUT output as the answer.
    // Wide intermediates prevent accidental reference-expression truncation.
    task calculate_expected;
        begin
            ar=$signed(reference_mem[a_addr][39:20]);
            ai=$signed(reference_mem[a_addr][19:0]);
            br=$signed(reference_mem[b_addr][39:20]);
            bi=$signed(reference_mem[b_addr][19:0]);
            wr=$signed(coefficients[w_addr][31:16]);
            wi=$signed(coefficients[w_addr][15:0]);
            tr=(br*wr-bi*wi) >>> 14;
            ti=(br*wi+bi*wr) >>> 14;
            check(tr>=-524288 && tr<=524287 &&
                  ti>=-524288 && ti<=524287,"T exceeds signed 20-bit range");
            r0=ar+tr; i0=ai+ti;
            r1=ar-tr; i1=ai-ti;
            check(r0>=-524288 && r0<=524287 && i0>=-524288 && i0<=524287 &&
                  r1>=-524288 && r1<=524287 && i1>=-524288 && i1<=524287,
                  "Y exceeds signed 20-bit range; lower ADC amplitude or define scaling");
            expected_y0={r0[19:0],i0[19:0]};
            expected_y1={r1[19:0],i1[19:0]};
        end
    endtask

    // Same assumed memory schedule as the combinational TB:
    // issue address -> one-cycle two-word read -> compute -> capture -> two writes.
    // Hold A/B/W through BOTH external memory writes; wait for output valid.
    // Sample outputs BEFORE the clock-edge NBA updates, like a receiving register.
    task test_one_pair;
        integer wait_cycles;
        begin
            calculate_expected;
            check(o_bf_out_valid === 1'b0,"Stale output valid before request");
            twiddle_addr <= w_addr;
            @(posedge clk); // E1: A/B memory read and ROM output update
            {a_re,a_im} <= fft_mem[a_addr];
            {b_re,b_im} <= fft_mem[b_addr];
            i_read_data_valid <= 1'b1;
            @(posedge clk); // E2: IDLE FSM accepts request (no internal data capture)
            check(twiddle_factor === coefficients[w_addr],"External ROM mismatch");
            i_read_data_valid <= 1'b0;

            wait_cycles=0;
            begin : wait_for_result
                forever begin
                    @(posedge clk);
                    wait_cycles=wait_cycles+1;
                    check(o_bf_out_valid === 1'b0 ||
                          o_bf_out_valid === 1'b1,"Unknown output valid");
                    if(o_bf_out_valid === 1'b1)
                        disable wait_for_result;
                    check(wait_cycles < MAX_WAIT_CYCLES,"Output valid timeout");
                end
            end

            // External write-path buffer modeled by the TB, NOT a DUT register.
            saved_y0={y0_re,y0_im};
            saved_y1={y1_re,y1_im};
            check(saved_y0 === expected_y0,"Y0/reference mismatch");
            check(saved_y1 === expected_y1,"Y1/reference mismatch");
            @(posedge clk); // First write after result capture
            check(o_bf_out_valid === 1'b0,"Output valid wider than one clock");
            check({y0_re,y0_im,y1_re,y1_im} === {saved_y0,saved_y1},
                  "Y changed while external A/B/W remain held");
            fft_mem[a_addr]=saved_y0;
            @(posedge clk); // Second write; next request can issue here
            check({y0_re,y0_im,y1_re,y1_im} === {saved_y0,saved_y1},
                  "Y changed before external write completion");
            fft_mem[b_addr]=saved_y1;
            reference_mem[a_addr]=expected_y0;
            reference_mem[b_addr]=expected_y1;
            completed=completed+1;
            stage_count=stage_count+1;
        end
    endtask

    initial begin
        $readmemh(ADC_FILE,adc_samples);
        $readmemh("twiddle_128_q14.mem",coefficients);
        for(n=0;n<128;n=n+1) begin
            check((^adc_samples[n]) !== 1'bx,"Missing ADC data");
            fft_mem[n]=adc_samples[reverse7(n)];
            reference_mem[n]=adc_samples[reverse7(n)];
        end
        for(n=0;n<64;n=n+1)
            check((^coefficients[n]) !== 1'bx,"Missing coefficient data");

        $display("INPUT: %s; reference computed from this file, no expected MEM required.",ADC_FILE);

        // Reset only the valid-control FSM; combinational Y is not reset.
        repeat(3) @(posedge clk);
        @(negedge clk);
        check(o_bf_out_valid === 1'b0,"Reset valid mismatch");
        rst=0;

        // Reset, input loading and bit reversal are outside the measured interval.
        @(posedge clk);
        start_time=$time;
        started=1;
        for(stage=0;stage<7;stage=stage+1) begin
            stage_start=$time;
            stage_count=0;
            half_size=1<<stage;
            for(group_base=0;group_base<128;group_base=group_base+2*half_size)
                for(j=0;j<half_size;j=j+1) begin
                    a_addr=group_base+j;
                    b_addr=a_addr+half_size;
                    w_addr=j*(128/(2*half_size));
                    test_one_pair;
                end
            check(stage_count==64,"Stage must contain 64 butterflies");
            for(n=0;n<128;n=n+1)
                check(fft_mem[n] === reference_mem[n],"Stage memory mismatch");
            $display("STAGE %0d PASS: 64 butterflies, elapsed=%0d ns",stage,$time-stage_start);
            if(stage<6) @(posedge clk); // one cycle between stages
        end
        elapsed=$time-start_time;
        check(completed==448,"Expected 448 butterflies");
        check(elapsed<=TIME_LIMIT_NS,"500 us schedule budget exceeded");

        $display("FFT448 ARITHMETIC PASS: %s, 7 stages, 448 butterflies, 128 final bins",ADC_FILE);
        $display("SCHEDULE PASS: elapsed=%0d ns, limit=%0d ns, cycles=%0d",elapsed,TIME_LIMIT_NS,elapsed/20);
        $display("SCOPE: combinational data + valid FSM; A/B/W held through external writes, 50 MHz.");
        $display("FFT schedule above excludes Power, reset and input loading; physical FPGA timing is not verified.");
        check_power_frame;
        finished=1;
        $finish;
    end

    initial begin
        #(TIME_LIMIT_NS+1);
        check(finished,"Simulation timeout");
    end
endmodule
