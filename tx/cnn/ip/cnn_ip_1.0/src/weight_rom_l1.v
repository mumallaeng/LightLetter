`timescale 1ns / 1ps

module weight_rom_l1 #(
    parameter OCH = 6
) (
    input      [$clog2(OCH)-1:0] out_ch_sel,
    output reg [          143:0] weight_out
);
    always @(*) begin
        case (out_ch_sel)
            0: weight_out = 144'h04ecf8e5b7c62f8dee67f0f2098c27b32b90;
            1: weight_out = 144'hc4ddc3c3c82cf0910647e6cc22aa1f2e14c2;
            2: weight_out = 144'hbf78e9e8fd88e533fc3536820ceb1e69173d;
            3: weight_out = 144'h1ff5185d21cde0ac001f1f34c2b8e3b70029;
            4: weight_out = 144'hfc941a97fd9618cb12840ebf12ef0d69f090;
            5: weight_out = 144'h1149fafaeda41d54e76ef5a821220c37d75f;
            default: weight_out = 0;
        endcase
    end
endmodule
