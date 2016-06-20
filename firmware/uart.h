#ifndef UART_H
#define UART_H

#include "usbasp.h"
#include <stdint.h>

#define RINGBUFFER_TX_SIZE 256
#define RINGBUFFER_RX_SIZE 256

void uart_config(uint16_t baud, uint8_t par, uint8_t stop, uint8_t bytes);
void uart_disable();
void uart_flush_tx();
void uart_flush_rx();

uint8_t uart_putc(uint8_t c);
uint8_t uart_getc(uint8_t* c);
uint8_t uart_putsn(uint8_t* data, uint8_t len);

uint16_t uart_tx_freeplaces();
void uart_dbg();

#endif // UART_H
