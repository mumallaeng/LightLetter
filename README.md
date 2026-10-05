# LightLetter

## How to run

Requires Vivado and Vitis 2020.2 on Windows. Run each board from its own folder.

### RX board

1. `rx\CREATE_VIVADO.cmd` creates `rx\vivado\project\rx_final.xpr`.
2. `rx\CREATE_VITIS.cmd` creates `rx\vitis\workspace` from `rx\vivado\export\FFT_RX_FINAL.xsa` and builds the `BFSK_RX_UART` app.
3. Open `rx\vitis\workspace` in Vitis and run `BFSK_RX_UART` on the board. Prebuilt files are in `rx\prebuilt\`.
4. `rx\START_UI.cmd` starts the PC UI at `http://127.0.0.1:8765`. Close any serial terminal that holds the board's COM port first.

### TX board

1. `tx\CREATE_VIVADO.cmd` creates `tx\vivado\project\tx_top.xpr`. Generate the bitstream, and export the XSA to `tx\vivado\export\` when the hardware changes.
2. Open `tx\vitis` as the Vitis workspace (File > Switch Workspace). If the projects do not show up, use File > Import > Existing Projects into Workspace and select `tx\vitis`. Build `tx_fpga` and run it on the board.
3. `tx\START_UI.cmd` starts the capture-board viewer. Install its packages once with `pip install -r tx\ui\requirements.txt`.

## BFSK transmitter

The BFSK TX history is imported from Critical-mankind/BFSK_Tx.
BFSK TX RTL and testbenches: `tx/bfsk_tx/`. Nucleo receiver test: `tx/bfsk_tx/nucleo_checker/`.
The standalone TX test application and Vivado test projects are kept in `legacy/`.

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

The project status and interface decisions are tracked in `docs/progress.md` as the Single Source of Truth.

## Directory structure

The two Zybo Z7-20 boards each have their own tree. Inside a board tree, each
function block keeps its RTL, testbenches and models together, and `vivado/` and
`vitis/` hold the board-level Vivado project scripts and Vitis workspace.

```text
tx/                 transmitter board: camera -> ArUco -> preprocess -> CNN -> BFSK TX
  cnn/              rtl/, ip/ (packaged cnn_ip), golden/ (C), model/ (Python), tb/
  preprocess/       img_preprocess RTL and testbenches
  aruco/            notebook, calibration, marker sheets, host test harness
  bfsk_tx/          BFSK TX RTL, testbenches, Nucleo checker firmware
  camera/           Digilent P-CAM / HDMI IP repository
  ui/               capture-board viewer (START_UI.cmd)
  vivado/           create_project.tcl, block design Tcl, constraints/, export/ (XSA)
  vitis/            Vitis workspace: tx_fpga, tx_fpga_system, tx_top_wrapper
rx/                 receiver board: XADC -> FFT -> BFSK RX -> UART -> PC UI
  fft/  bfsk_rx/  snapshot/  ui/
  vivado/  vitis/  prebuilt/  tools/  docs/
docs/               project-wide notes (progress.md)
legacy/             unused or superseded sources, kept under their old paths
```

Each board tree has `CREATE_VIVADO.cmd` (and `CREATE_VITIS.cmd` on RX) to rebuild the
project from scripts with paths relative to the script, so a fresh clone works on any PC.

## Development rules

- RTL is written in Verilog.
- Do not delete previously PASSed code without a recorded reason.
- Keep PASSed testbenches for regression.
- Update `docs/progress.md` first when hardware values or shared communication parameters change.
## RX UART to PC UI

The September 2026 RX build sends decoded packets to the PC through PS7 UART1 as CRLF-terminated CSV at 115200 baud. See `rx/docs/rx_uart_protocol.md` for the wire format and `rx/vitis/src/` for the Vitis application source.
