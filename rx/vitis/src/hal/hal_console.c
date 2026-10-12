#include "hal_console.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xuartps_hw.h"
void hal_console_write(const char *text)
{
    xil_printf("%s", text);
}
size_t hal_console_try_write(const char *data, size_t count)
{
    size_t sent = 0;
    while (sent < count && !XUartPs_IsTransmitFull(STDOUT_BASEADDRESS)) {
        XUartPs_WriteReg(STDOUT_BASEADDRESS, XUARTPS_FIFO_OFFSET,
                        (unsigned char)data[sent++]);
    }
    return sent;
}
