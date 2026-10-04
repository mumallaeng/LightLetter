# BFSK RX UART interface

This interface forwards decoded BFSK packets from the Zybo Z7-20 receiver to a PC UI.

## Serial settings

- Port: Zybo Z7-20 PROG/UART USB port
- Baud rate: `115200`
- Data bits: `8`
- Parity: none
- Stop bits: `1`
- Line ending: `CRLF`

The application prints these two lines after startup:

```text
BFSK_RX_UART_READY
seq,frame_id,data_ascii,data_hex,crc_hex,packet_error,packet_overrun,raw_hex
```

Each received packet is then sent as one CSV record:

```text
0,71,"A",0x41,0x3c,0,0,0x8047413c
```

| Field | Meaning |
| --- | --- |
| `seq` | UART application sequence number, starting at zero |
| `frame_id` | Received 8-bit frame ID in decimal |
| `data_ascii` | Printable received byte; non-printable bytes are shown as `.` |
| `data_hex` | Received data byte in hexadecimal |
| `crc_hex` | CRC byte received in the BFSK frame |
| `packet_error` | Receiver error flag captured with the packet |
| `packet_overrun` | A new frame arrived before the previous mailbox entry was cleared |
| `raw_hex` | Complete 32-bit PL mailbox word |

An ASCII quote in `data_ascii` is escaped as `""` according to CSV rules.

## UI parsing

1. Open the serial port with the settings above.
2. Wait for `BFSK_RX_UART_READY`.
3. Treat the next line as the CSV header.
4. Parse every following CRLF-terminated line with a CSV parser.
5. Display or accept data only according to the UI policy for `packet_error` and `packet_overrun`.

Do not split a line with a plain string `split(',')`; use a CSV parser because `data_ascii` is a quoted field.

## PL mailbox layout

The `rx_latch` module stores a decoded packet until the processing system acknowledges it through AXI GPIO channel 2.

| Bits | Signal |
| --- | --- |
| `[31]` | `packet_ready` |
| `[30]` | `packet_error` |
| `[29]` | `packet_overrun` |
| `[28:24]` | reserved |
| `[23:16]` | `frame_id` |
| `[15:8]` | `data` |
| `[7:0]` | `received_crc` |

AXI GPIO channel 1 is the 32-bit input mailbox. Channel 2 bit 0 is `packet_clear`.
