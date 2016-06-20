#ifndef USBASP_UART_H_
#define USBASP_UART_H_

#include <stdint.h>

#include "../firmware/usbasp.h"

#include <libusb-1.0/libusb.h>

#define USBASP_NO_CAPS (-4)

typedef struct USBasp_UART{
	libusb_device_handle* usbhandle;
} USBasp_UART;

#ifdef __cplusplus
extern "C"{
#endif

int usbasp_uart_config(USBasp_UART* usbasp, int baud, int flags);
void usbasp_uart_flushrx(USBasp_UART* usbasp);
void usbasp_uart_flushtx(USBasp_UART* usbasp);
void usbasp_uart_disable(USBasp_UART* usbasp);
int usbasp_uart_read(USBasp_UART* usbasp, uint8_t* buff, size_t len);
int usbasp_uart_write(USBasp_UART* usbasp, uint8_t* buff, size_t len);
int usbasp_uart_write_all(USBasp_UART* usbasp, uint8_t* buff, int len);

#ifdef __cplusplus
}
#endif

#endif
