`timescale 1ns / 1ps

module rx_bin_detector #(
    parameter MAG_W = 24
) (
    input  wire                 clk,
    input  wire                 rst_n,

    // FFT는 bin 0부터 bin 127까지 Power를 순서대로 출력한다.
    input  wire [MAG_W-1:0]     fft_mag,
    input  wire                 fft_mag_valid,

    output wire [MAG_W-1:0]     bin8_power,
    output wire [MAG_W-1:0]     bin16_power,
    output wire [MAG_W-1:0]     bin20_power,
    output wire                 fft_block_done
);

    // 한 번의 128-point FFT에서 출력되는 bin별 Power 저장 공간
    reg [MAG_W-1:0] fft_power_mem [0:127];

    // valid가 들어온 순서가 곧 bin 번호다.
    reg [6:0] fft_counter;
    reg       fft_block_done_reg;

    assign bin8_power     = fft_power_mem[8];
    assign bin16_power    = fft_power_mem[16];
    assign bin20_power    = fft_power_mem[20];
    assign fft_block_done = fft_block_done_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fft_counter        <= 7'd0;
            fft_block_done_reg <= 1'b0;

            // 출력으로 사용하는 위치만 초기화한다.
            fft_power_mem[8]   <= {MAG_W{1'b0}};
            fft_power_mem[16]  <= {MAG_W{1'b0}};
            fft_power_mem[20]  <= {MAG_W{1'b0}};
        end
        else begin
            // 128번째 유효 Power를 받은 클럭에만 한 클럭 발생한다.
            fft_block_done_reg <= 1'b0;

            if (fft_mag_valid) begin
                fft_power_mem[fft_counter] <= fft_mag;

                if (fft_counter == 7'd127) begin
                    fft_counter        <= 7'd0;
                    fft_block_done_reg <= 1'b1;
                end
                else begin
                    fft_counter <= fft_counter + 7'd1;
                end
            end
        end
    end

endmodule
