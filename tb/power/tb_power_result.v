`timescale 1ns / 1ps
module tb_power_result();

    // ADC 128, re/im 20bit
    parameter ADC_FILE = "fft_input_10000hz.mem";

    reg clk=0, rst=1, i_read_data_valid=0;
    wire o_bf_out_valid;
    always #10 clk = ~clk; // 10 ns reverse
    reg signed [19:0] a_re=0, a_im=0, b_re=0, b_im=0;
    reg [5:0] twiddle_addr=0;
    wire [31:0] twiddle_factor;
    wire signed [19:0] y0_re,y0_im,y1_re,y1_im;

    // tb_twiddle rom

    twiddle_rom u_twiddle_rom (
        .clk(clk), .twiddle_addr(twiddle_addr), .twiddle_factor(twiddle_factor)
    );
    butterfly dut (
        .clk(clk), .rst(rst), .i_read_data_valid(i_read_data_valid),
        .o_bf_out_valid(o_bf_out_valid),
        .a_re(a_re), .a_im(a_im), .b_re(b_re), .b_im(b_im),
        .twiddle_factor(twiddle_factor),
        .y0_re(y0_re), .y0_im(y0_im), .y1_re(y1_re), .y1_im(y1_im));

    //************ make for test fft************//
    //adc
    reg [39:0] adc_samples[0:127], fft_mem[0:127], results[0:127];
    //y0,y1, fft_core
    reg [39:0] saved_y0, saved_y1, i_fft_core_data=0;

    reg i_fft_core_valid=0;
    wire [39:0] fft_mag;
    wire fft_mag_valid;

    // fft 7stage test

    integer stage, half_size, group_base, j, n, a_addr, b_addr;
    integer supplied=0, received=0, fout;
    parameter OUTPUT_FILE = "power_result.csv";

    // power top instance
    // 128 save out RTL
    power_top u_power_top (
        .clk(clk), .rst(rst), .i_fft_core_data(i_fft_core_data),
        .i_fft_core_valid(i_fft_core_valid),
        .fft_mag(fft_mag), .fft_mag_valid(fft_mag_valid));

    // virtual reverse bit
    function [6:0] reverse7;
        input [6:0] x;
        begin reverse7={x[0],x[1],x[2],x[3],x[4],x[5],x[6]}; end
    endfunction

    // Capture the pre-edge value, just as a synchronous RX would.
    // No arithmetic reference or expected-bin comparison is performed.
    always @(posedge clk) begin
        if (!rst) begin
            if (fft_mag_valid !== 0 && fft_mag_valid !== 1)
                $fatal(1,"Unknown output valid");
            if (fft_mag_valid === 1'b1) begin
                if (supplied!=128 || received>=128)
                    $fatal(1,"Early or extra Power output");
                if ((^fft_mag) === 1'bx) $fatal(1,"Unknown Power data");
                results[received]=fft_mag;
                $fdisplay(fout,"%0d,%0d",received,fft_mag);
                received=received+1;
            end
        end
    end

    // TB supplies the FFT addresses and holds A/B/W until both writes finish.
    task run_pair;
        begin
            twiddle_addr <= j*(128/(2*half_size));
            @(posedge clk);
            {a_re,a_im} <= fft_mem[a_addr];
            {b_re,b_im} <= fft_mem[b_addr];
            i_read_data_valid <= 1;
            @(posedge clk); i_read_data_valid <= 0;
            begin : wait_result
                forever begin
                    @(posedge clk);
                    if (o_bf_out_valid === 1'b1) disable wait_result;
                end
            end
            saved_y0={y0_re,y0_im}; saved_y1={y1_re,y1_im};
            @(posedge clk); fft_mem[a_addr]=saved_y0;
            @(posedge clk); fft_mem[b_addr]=saved_y1;
        end
    endtask

    initial begin
        $readmemh(ADC_FILE,adc_samples);
        for(n=0;n<128;n=n+1) begin
            if ((^adc_samples[n]) === 1'bx) $fatal(1,"Missing ADC sample %0d",n);
            fft_mem[n]=adc_samples[reverse7(n)];
        end
        fout=$fopen(OUTPUT_FILE,"w");
        if (!fout) $fatal(1,"Cannot open output CSV");
        $fdisplay(fout,"output_index,power");
        repeat(3) @(posedge clk);
        @(negedge clk); rst=0;
        @(posedge clk);
        for(stage=0;stage<7;stage=stage+1) begin
            half_size=1<<stage;
            for(group_base=0;group_base<128;group_base=group_base+2*half_size)
                for(j=0;j<half_size;j=j+1) begin
                    a_addr=group_base+j; b_addr=a_addr+half_size;
                    run_pair;
                end
            if(stage<6) @(posedge clk);
        end
        for(n=0;n<128;n=n+1) begin
            @(negedge clk); i_fft_core_data=fft_mem[n]; i_fft_core_valid=1;
            @(posedge clk); #1; supplied=supplied+1;
        end
        @(negedge clk); i_fft_core_valid=0;
        while(received<128) begin @(posedge clk); #1; end
        repeat(3) begin
            @(posedge clk); #1;
            if(fft_mag_valid !== 0) $fatal(1,"Extra or unknown output valid");
        end
        $fclose(fout);
        $display("TB_POWER_RESULT COLLECTION PASS: received 128 known values; file=%s",OUTPUT_FILE);
        $display("Collection only: arithmetic, bin order and exact latency are NOT verified.");
        $finish;
    end

    initial begin
        #500000;
        $fatal(1,"Timeout: supplied=%0d received=%0d",supplied,received);
    end
endmodule
