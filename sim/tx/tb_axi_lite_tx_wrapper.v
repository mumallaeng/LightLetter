`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Engineer: Jong.W.Park
// Module Name: tb_axi_lite_tx_wrapper
//
// 검증 항목:
//   1. Reset
//   2. TX_DATA Write / Readback
//   3. STATUS Read
//   4. START Write -> char_valid 1 Clock Pulse
//   5. Busy 중 START Ignore
//   6. AW 먼저 / W 나중 Write
//   7. W 먼저 / AW 나중 Write
//////////////////////////////////////////////////////////////////////////////////

module tb_axi_lite_tx_wrapper;

    localparam integer DATA_WIDTH = 32;
    localparam integer ADDR_WIDTH = 4;

    localparam [3:0] ADDR_TX_DATA = 4'h0;
    localparam [3:0] ADDR_TX_CTRL = 4'h4;
    localparam [3:0] ADDR_TX_STATUS = 4'h8;

    reg                          clk;
    reg                          aresetn;

    reg     [    ADDR_WIDTH-1:0] awaddr;
    reg                          awvalid;
    wire                         awready;

    reg     [    DATA_WIDTH-1:0] wdata;
    reg     [(DATA_WIDTH/8)-1:0] wstrb;
    reg                          wvalid;
    wire                         wready;

    wire    [               1:0] bresp;
    wire                         bvalid;
    reg                          bready;

    reg     [    ADDR_WIDTH-1:0] araddr;
    reg                          arvalid;
    wire                         arready;

    wire    [    DATA_WIDTH-1:0] rdata;
    wire    [               1:0] rresp;
    wire                         rvalid;
    reg                          rready;

    wire    [               7:0] char_id;
    wire                         char_valid;
    reg                          char_ready;
    reg                          tx_busy;

    integer                      pass_count;
    integer                      fail_count;
    integer                      char_valid_pulse_count;
    integer                      pulse_before;

    reg     [              31:0] read_value;

    axi_lite_tx_wrapper #(
        .C_S_AXI_DATA_WIDTH(DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH(ADDR_WIDTH)
    ) dut (
        .s_axi_aclk   (clk),
        .s_axi_aresetn(aresetn),

        .s_axi_awaddr (awaddr),
        .s_axi_awvalid(awvalid),
        .s_axi_awready(awready),

        .s_axi_wdata (wdata),
        .s_axi_wstrb (wstrb),
        .s_axi_wvalid(wvalid),
        .s_axi_wready(wready),

        .s_axi_bresp (bresp),
        .s_axi_bvalid(bvalid),
        .s_axi_bready(bready),

        .s_axi_araddr (araddr),
        .s_axi_arvalid(arvalid),
        .s_axi_arready(arready),

        .s_axi_rdata (rdata),
        .s_axi_rresp (rresp),
        .s_axi_rvalid(rvalid),
        .s_axi_rready(rready),

        .char_id   (char_id),
        .char_valid(char_valid),
        .char_ready(char_ready),
        .tx_busy   (tx_busy)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    always @(posedge clk) begin
        if (aresetn && char_valid)
            char_valid_pulse_count = char_valid_pulse_count + 1;
    end

    task check_1bit;
        input actual;
        input expected;
        input [8*64-1:0] test_name;
        begin
            if (actual === expected) begin
                $display("[PASS] %0s", test_name);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=%b expected=%b", test_name,
                         actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    task check_8bit;
        input [7:0] actual;
        input [7:0] expected;
        input [8*64-1:0] test_name;
        begin
            if (actual === expected) begin
                $display("[PASS] %0s : 0x%02h", test_name, actual);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=0x%02h expected=0x%02h",
                         test_name, actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    task check_32bit;
        input [31:0] actual;
        input [31:0] expected;
        input [8*64-1:0] test_name;
        begin
            if (actual === expected) begin
                $display("[PASS] %0s : 0x%08h", test_name, actual);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=0x%08h expected=0x%08h",
                         test_name, actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    task check_integer;
        input integer actual;
        input integer expected;
        input [8*64-1:0] test_name;
        begin
            if (actual == expected) begin
                $display("[PASS] %0s : %0d", test_name, actual);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s : actual=%0d expected=%0d", test_name,
                         actual, expected);
                fail_count = fail_count + 1;
            end
        end
    endtask

    // AW/W 동시 Write
    task axi_write;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
        begin
            @(negedge clk);

            awaddr  = address;
            awvalid = 1'b1;

            wdata   = data;
            wstrb   = 4'hF;
            wvalid  = 1'b1;

            // 현재 DUT에서는 AW/W를 동시에 받을 수 있으므로
            // 두 Ready가 모두 올라온 상태에서 한 번에 Handshake
            while (!(awready && wready)) @(negedge clk);

            // 다음 Rising Edge에서 실제 AXI Handshake 발생
            @(posedge clk);
            #1;

            // Handshake 완료 후 Valid 해제
            @(negedge clk);
            awvalid = 1'b0;
            wvalid  = 1'b0;

            // Write Response 수락
            bready  = 1'b1;

            while (!bvalid) @(negedge clk);

            check_1bit((bresp == 2'b00), 1'b1, "AXI write response OKAY");

            @(negedge clk);
            bready = 1'b0;
        end
    endtask

    // AW 먼저
    task axi_write_aw_first;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
        begin
            @(negedge clk);
            awaddr  = address;
            awvalid = 1'b1;

            while (!(awvalid && awready)) @(negedge clk);
            @(negedge clk);
            awvalid = 1'b0;

            repeat (2) @(negedge clk);

            wdata  = data;
            wstrb  = 4'hF;
            wvalid = 1'b1;

            while (!(wvalid && wready)) @(negedge clk);
            @(negedge clk);
            wvalid = 1'b0;

            bready = 1'b1;
            while (!bvalid) @(negedge clk);
            @(negedge clk);
            bready = 1'b0;
        end
    endtask

    // W 먼저
    task axi_write_w_first;
        input [ADDR_WIDTH-1:0] address;
        input [DATA_WIDTH-1:0] data;
        begin
            @(negedge clk);
            wdata  = data;
            wstrb  = 4'hF;
            wvalid = 1'b1;

            while (!(wvalid && wready)) @(negedge clk);
            @(negedge clk);
            wvalid = 1'b0;

            repeat (2) @(negedge clk);

            awaddr  = address;
            awvalid = 1'b1;

            while (!(awvalid && awready)) @(negedge clk);
            @(negedge clk);
            awvalid = 1'b0;

            bready  = 1'b1;
            while (!bvalid) @(negedge clk);
            @(negedge clk);
            bready = 1'b0;
        end
    endtask

    task axi_read;
        input [ADDR_WIDTH-1:0] address;
        output [DATA_WIDTH-1:0] data;
        begin
            @(negedge clk);
            araddr  = address;
            arvalid = 1'b1;

            while (!(arvalid && arready)) @(negedge clk);
            @(negedge clk);
            arvalid = 1'b0;

            rready  = 1'b1;
            while (!rvalid) @(negedge clk);

            data = rdata;
            check_1bit((rresp == 2'b00), 1'b1, "AXI read response OKAY");

            @(negedge clk);
            rready = 1'b0;
        end
    endtask

    initial begin
        aresetn                = 1'b0;

        awaddr                 = 4'h0;
        awvalid                = 1'b0;
        wdata                  = 32'h0;
        wstrb                  = 4'h0;
        wvalid                 = 1'b0;
        bready                 = 1'b0;

        araddr                 = 4'h0;
        arvalid                = 1'b0;
        rready                 = 1'b0;

        char_ready             = 1'b1;
        tx_busy                = 1'b0;

        pass_count             = 0;
        fail_count             = 0;
        char_valid_pulse_count = 0;

        repeat (4) @(negedge clk);
        aresetn = 1'b1;

        @(posedge clk);
        #1;

        $display("----------------------------------------");
        $display("TEST 1 : RESET");
        $display("----------------------------------------");
        check_8bit(char_id, 8'h00, "Reset -> char_id=00");
        check_1bit(char_valid, 1'b0, "Reset -> char_valid=0");

        $display("----------------------------------------");
        $display("TEST 2 : TX_DATA WRITE / READ");
        $display("----------------------------------------");
        axi_write(ADDR_TX_DATA, 32'h00000041);
        check_8bit(char_id, 8'h41, "TX_DATA write -> char_id=41");

        axi_read(ADDR_TX_DATA, read_value);
        check_32bit(read_value, 32'h00000041, "TX_DATA readback");

        $display("----------------------------------------");
        $display("TEST 3 : STATUS READY");
        $display("----------------------------------------");
        axi_read(ADDR_TX_STATUS, read_value);
        check_32bit(read_value, 32'h00000001, "STATUS READY=1 BUSY=0");

        $display("----------------------------------------");
        $display("TEST 4 : START -> char_valid");
        $display("----------------------------------------");
        pulse_before = char_valid_pulse_count;
        axi_write(ADDR_TX_CTRL, 32'h00000001);
        repeat (2) @(posedge clk);
        check_integer(char_valid_pulse_count, pulse_before + 1,
                      "START accepted -> char_valid pulse count +1");
        check_8bit(char_id, 8'h41, "START keeps char_id=41");

        $display("----------------------------------------");
        $display("TEST 5 : BUSY / START IGNORE");
        $display("----------------------------------------");
        char_ready = 1'b0;
        tx_busy    = 1'b1;

        axi_read(ADDR_TX_STATUS, read_value);
        check_32bit(read_value, 32'h00000002, "STATUS READY=0 BUSY=1");

        pulse_before = char_valid_pulse_count;
        axi_write(ADDR_TX_CTRL, 32'h00000001);
        repeat (2) @(posedge clk);
        check_integer(char_valid_pulse_count, pulse_before,
                      "Busy START ignored");

        $display("----------------------------------------");
        $display("TEST 6 : AW FIRST");
        $display("----------------------------------------");
        char_ready = 1'b1;
        tx_busy    = 1'b0;

        axi_write_aw_first(ADDR_TX_DATA, 32'h00000042);
        check_8bit(char_id, 8'h42, "AW first write -> char_id=42");

        $display("----------------------------------------");
        $display("TEST 7 : W FIRST");
        $display("----------------------------------------");
        axi_write_w_first(ADDR_TX_DATA, 32'h00000043);
        check_8bit(char_id, 8'h43, "W first write -> char_id=43");

        $display("----------------------------------------");
        if (fail_count == 0) $display("AXI LITE TX WRAPPER TEST RESULT : PASS");
        else $display("AXI LITE TX WRAPPER TEST RESULT : FAIL");

        $display("PASS = %0d, FAIL = %0d", pass_count, fail_count);
        $display("----------------------------------------");

        #50;
        $finish;
    end

endmodule
