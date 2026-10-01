#include "app_csv.h"
#include "../hal/hal_console.h"
#include <stdio.h>

void app_csv_header(void)
{
    hal_console_write("BFSK_RX_UART_READY\r\n");
    hal_console_write("seq,frame_id,data_ascii,data_hex,crc_hex,packet_error,packet_overrun,raw_hex\r\n");
}

void app_csv_init_error(int status)
{
    char line[48];
    (void)snprintf(line, sizeof line, "ERROR,AXI_GPIO_INIT,%d\r\n", status);
    hal_console_write(line);
}

void app_csv_packet(uint32_t sequence, const bfsk_packet_t *packet)
{
    char line[96];
    char ascii[3] = {'.', '\0', '\0'};
    if (packet->data == (uint8_t)'"') {
        ascii[0] = '"';
        ascii[1] = '"';
    } else if (packet->data >= 0x20U && packet->data <= 0x7EU) {
        ascii[0] = (char)packet->data;
    }
    (void)snprintf(line, sizeof line,
        "%lu,%u,\"%s\",0x%02x,0x%02x,%u,%u,0x%08lx\r\n",
        (unsigned long)sequence, (unsigned int)packet->frame_id, ascii,
        (unsigned int)packet->data, (unsigned int)packet->received_crc,
        (unsigned int)packet->error, (unsigned int)packet->overrun,
        (unsigned long)packet->raw);
    hal_console_write(line);
}
