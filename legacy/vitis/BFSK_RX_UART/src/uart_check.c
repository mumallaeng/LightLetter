#include "platform.h"
#include "xgpio.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xstatus.h"

#define PACKET_GPIO_DEVICE_ID  XPAR_AXI_GPIO_0_DEVICE_ID
#define PACKET_INPUT_CHANNEL   1U
#define PACKET_CLEAR_CHANNEL   2U

#define PACKET_READY_MASK      0x80000000U
#define PACKET_ERROR_MASK      0x40000000U
#define PACKET_OVERRUN_MASK    0x20000000U

static XGpio packet_gpio;

/*
 * Channel 1 packet layout from rx_latch:
 * [31]    packet_ready
 * [30]    packet_error
 * [29]    packet_overrun
 * [28:24] reserved
 * [23:16] frame_id
 * [15:8]  data
 * [7:0]   received_crc
 */
static void clear_packet_mailbox(void)
{
    XGpio_DiscreteWrite(&packet_gpio, PACKET_CLEAR_CHANNEL, 1U);

    while ((XGpio_DiscreteRead(&packet_gpio, PACKET_INPUT_CHANNEL) &
            PACKET_READY_MASK) != 0U) {
        /* Wait until rx_latch observes packet_clear. */
    }

    XGpio_DiscreteWrite(&packet_gpio, PACKET_CLEAR_CHANNEL, 0U);
}

/* Print one valid CSV field containing the received ASCII byte. */
static void print_ascii_csv(u8 data)
{
    xil_printf("\"");

    if (data == (u8)'\"') {
        /* A quote inside a CSV field is written twice. */
        xil_printf("\"\"");
    } else if ((data >= 0x20U) && (data <= 0x7EU)) {
        xil_printf("%c", (char)data);
    } else {
        /* Keep control characters from breaking the UART CSV stream. */
        xil_printf(".");
    }

    xil_printf("\"");
}

int main(void)
{
    u32 packet_word;
    u32 sequence = 0U;
    u8 frame_id;
    u8 data;
    u8 received_crc;
    u8 packet_error;
    u8 packet_overrun;
    int status;

    init_platform();

    status = XGpio_Initialize(&packet_gpio, PACKET_GPIO_DEVICE_ID);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR,AXI_GPIO_INIT,%d\r\n", status);
        cleanup_platform();
        return XST_FAILURE;
    }

    XGpio_SetDataDirection(&packet_gpio, PACKET_INPUT_CHANNEL, 0xFFFFFFFFU);
    XGpio_SetDataDirection(&packet_gpio, PACKET_CLEAR_CHANNEL, 0x00000000U);
    XGpio_DiscreteWrite(&packet_gpio, PACKET_CLEAR_CHANNEL, 0U);

    xil_printf("BFSK_RX_UART_READY\r\n");
    xil_printf("seq,frame_id,data_ascii,data_hex,crc_hex,packet_error,packet_overrun,raw_hex\r\n");

    while (1) {
        packet_word = XGpio_DiscreteRead(&packet_gpio,
                                         PACKET_INPUT_CHANNEL);

        if ((packet_word & PACKET_READY_MASK) != 0U) {
            frame_id       = (u8)((packet_word >> 16) & 0xFFU);
            data           = (u8)((packet_word >> 8) & 0xFFU);
            received_crc   = (u8)(packet_word & 0xFFU);
            packet_error   = (u8)((packet_word & PACKET_ERROR_MASK) != 0U);
            packet_overrun = (u8)((packet_word & PACKET_OVERRUN_MASK) != 0U);

            /* Copy first, then clear before the relatively slow UART print. */
            clear_packet_mailbox();

            xil_printf("%u,%u,", (unsigned int)sequence,
                       (unsigned int)frame_id);
            print_ascii_csv(data);
            xil_printf(",0x%02x,0x%02x,%u,%u,0x%08x\r\n",
                       (unsigned int)data,
                       (unsigned int)received_crc,
                       (unsigned int)packet_error,
                       (unsigned int)packet_overrun,
                       (unsigned int)packet_word);

            sequence++;
        }
    }

    cleanup_platform();
    return XST_SUCCESS;
}
