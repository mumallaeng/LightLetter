# LightLetter

## BFSK transmitter

The BFSK TX history is imported from Critical-mankind/BFSK_Tx.
Vitis TX application: `vitis/BFSK_TX_Test/`. Nucleo receiver test: `Nucleo_tx_test/`.
TX simulation sources and Vivado projects: `sim/tx/` and `tb_xpr/`.

# FPGA BFSK Optical Communication

FPGA-based optical communication project using BFSK modulation and 128-point FFT frequency detection.

## Current baseline

- Modulation: BFSK
- Bit 0: 10 kHz / FFT Bin 8
- Bit 1: 20 kHz / FFT Bin 16
- SYNC: 25 kHz / FFT Bin 20
- XADC Sampling Rate: 160 kSample/s
- FFT: 128-point
- Symbol: 256 samples / 1.6 ms
- Preamble: 4 SYNC symbols
- SFD: 0xD5
- CRC: CRC-8, polynomial 0x07
- RTL language: Verilog

The project status and interface decisions are tracked in `progress.md` as the Single Source of Truth.

## Directory structure

```text
rtl/
  common/
  tx/
  rx_fft/
  rx_top/
sim/
  tx/
  rx/
constraints/
docs/
hw/
progress.md
```

## Development rules

- RTL is written in Verilog.
- Do not delete previously PASSed code without a recorded reason.
- Keep PASSed testbenches for regression.
- Update `progress.md` first when hardware values or shared communication parameters change.
## RX UART to PC UI

The September 2026 RX build sends decoded packets to the PC through PS7 UART1 as CRLF-terminated CSV at 115200 baud. See `docs/rx_uart_protocol.md` for the wire format and `vitis/BFSK_RX_UART/` for the Vitis application source.
