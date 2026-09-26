# BFSK RX UART application

This is the source-only Vitis application used by the September 2026 final RX project.

- Hardware platform used for the tested build: `FFT_RX_FINAL`
- Processor: `ps7_cortexa9_0`
- OS: standalone
- UART: PS7 UART1 on MIO 48-49, 115200 baud, 8-N-1
- Packet input: AXI GPIO device 0

Create or select an application targeting the `FFT_RX_FINAL` hardware platform, then add the files under `src/`. The entry point is `src/uart_check.c`.

The tested local build produced `BFSK_RX_UART.elf` successfully on 2026-09-23. Generated BSP, platform export, ELF, bitstream, and IDE directories are intentionally excluded from Git.

See [`docs/rx_uart_protocol.md`](../../docs/rx_uart_protocol.md) for the PC UI protocol.
