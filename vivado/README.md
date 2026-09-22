# RX Vivado snapshot (2026-09-21)

This directory contains the `design_1` block design used with Vivado 2020.2 on the Zybo Z7-20 (`xc7z020clg400-1`). The top-level HDL is `design_1_wrapper.v`; its ports are `sys_clock`, `reset_rtl`, `adc_in_v_p`, and `adc_in_v_n`.

The signal path is XADC Wizard (VAUX14, event-driven sampling) -> `fft_input_adapter` -> 128-point XFFT -> `fft_power_calc` -> `rx_top`. The `xadc_sample_trigger` drives `CONVST` at 160 kS/s from the 100 MHz clock. The block design also includes clock/reset IP and an ILA.

`design_1.bd` and the matching `ip/*/*.xci` files preserve the IP configuration. `design_1_wrapper.v` is a generated snapshot; after importing the block design into Vivado, regenerate the wrapper and output products rather than editing generated files. Add the RTL under `rtl/rx/` and `constraints/rx_zybo_z7.xdc` to the project. Vivado's generated `.gen`, `.runs`, `.cache`, `.hw`, and bitstream files are intentionally not included.

The RTL in `rtl/rx/` matches the sources referenced by this block design. In particular, `rx_bin_detector.v` is the three-bin implementation imported into the project, not a later experimental local variant. The current receiver can still report packet errors at an FFT symbol boundary; this snapshot records the tested hardware configuration rather than a fix for that behavior.
