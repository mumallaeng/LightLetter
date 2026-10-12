//Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
//Date        : Mon Sep 21 20:06:48 2026
//Host        : DESKTOP-7CFQ9ND running 64-bit major release  (build 9200)
//Command     : generate_target design_1_wrapper.bd
//Design      : design_1_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module design_1_wrapper
   (adc_in_v_n,
    adc_in_v_p,
    reset_rtl,
    sys_clock);
  input adc_in_v_n;
  input adc_in_v_p;
  input reset_rtl;
  input sys_clock;

  wire adc_in_v_n;
  wire adc_in_v_p;
  wire reset_rtl;
  wire sys_clock;

  design_1 design_1_i
       (.adc_in_v_n(adc_in_v_n),
        .adc_in_v_p(adc_in_v_p),
        .reset_rtl(reset_rtl),
        .sys_clock(sys_clock));
endmodule
