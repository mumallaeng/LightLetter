`ifndef BFSK_PARAMS_VH
`define BFSK_PARAMS_VH

// Project-wide BFSK parameters.
// Keep synchronized with progress.md.

`define FS_HZ             160000
`define FFT_N             128

`define F0_HZ             10000
`define F1_HZ             20000
`define FSYNC_HZ          25000

`define F0_BIN            8
`define F1_BIN            16
`define FSYNC_BIN         20

`define FFT_BLOCK_SAMPLES 128
`define SYMBOL_SAMPLES    256
`define PREAMBLE_SYMBOLS  4

`define SFD_VALUE         8'hD5
`define CRC_POLY          8'h07
`define CRC_INIT          8'h00
`define CRC_XOROUT        8'h00

`endif
